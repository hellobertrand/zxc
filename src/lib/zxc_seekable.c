/*
 * ZXC - High-performance lossless compression
 *
 * Copyright (c) 2025-2026 Bertrand Lebonnois and contributors.
 * SPDX-License-Identifier: BSD-3-Clause
 */

/**
 * @file zxc_seekable.c
 * @brief Seekable archive reader (random-access decompression) and seek table writer.
 *
 * The seek table is a standard ZXC block (type = ZXC_BLOCK_SEK) appended
 * between the EOF block and the file footer. It records where every block
 * starts, so a byte range costs one small table read plus the blocks it
 * covers; nothing of the table stays resident and opening is O(1).
 *
 * On-disk layout of a SEK block, for N blocks in G = ceil(N / 64) groups:
 *
 *   [Block Header (8B)]   block_type=SEK, block_flags=0, comp_size=(u32)(T ^ T >> 32),
 *                         T = G*8 + N*4 the table's byte size
 *   G x group:
 *     [Anchor (8B)]       byte offset (u64 LE) of the group's first block
 *     [<= 64 x Size (4B)] on-disk size (u32 LE) of each of its blocks
 *
 * Anchor + sizes lands on the next anchor (the EOF block for the last group); the
 * reader checks each group alone. N comes from the footer: the header's 32-bit
 * field does not bound it.
 *
 * Detection from end of file:
 *   1. Read file header (first 16 bytes) => block_size, and HAS_SEEK_TABLE:
 *      clear, the archive is not seekable
 *   2. Read file footer (last 8 bytes) => total_decompressed_size
 *   3. Derive num_blocks = ceil(total_decomp / block_size)
 *   4. Read the EOF and SEK block headers in one go, validate both
 *   5. Groups are read and checked on access
 */

#include "../../include/zxc_seekable.h"

#include "../../include/zxc_dict.h"
#include "../../include/zxc_error.h"
#include "zxc_internal.h"
#include "zxc_threads.h"

// =========================================================================
// Seekable Reader (Opaque Handle)
// =========================================================================

struct zxc_seekable_s {
    // Source - exactly one of {src, reader.read_at} is set. The FILE* variant
    // wraps pread() in its own reader ctx, indistinguishable from here.
    const uint8_t* src;
    uint64_t src_size;
    zxc_reader_t reader; /* user-supplied callback reader; read_at == NULL when unused */

    // Reader context owned by the handle and freed in zxc_seekable_free, set by
    // the thin wrappers. NULL when the caller owns reader.ctx itself.
    void* owned_reader_ctx;

    // Seek table geometry; the entries themselves stay on disk.
    uint64_t num_blocks;
    uint64_t total_decomp; /* total decompressed size (from footer) */
    uint64_t table_off;    /* first entry, just past the SEK block header */
    uint64_t eof_off;      /* EOF block header: where the last block ends */
    uint64_t entry_max;    /* largest legal block on disk: header + block_size + checksum */

    // File header info - block_size is always a power of 2 in [4KB, 2MB],
    // fits in 21 bits.
    uint32_t block_size;
    int file_has_checksums;
    int verify_checksums;      /* caller's switch; needs file_has_checksums too */
    uint32_t expected_dict_id; /* dict_id from the file header; 0 = no dictionary */

    // Reusable decompression context and compressed-block scratch. Both belong
    // to the single-threaded path, which is already not reentrant per handle;
    // the multi-threaded path gives each worker its own.
    zxc_cctx_t dctx;
    int dctx_initialized;
    uint8_t* read_buf;
    size_t read_buf_cap;

    // Dictionary (owned copy, freed in zxc_seekable_free).
    uint8_t* dict;
    size_t dict_size;
    // Shared literal Huffman table (owned copy; meaningful when has_dict_huf).
    uint8_t dict_huf[ZXC_HUF_TABLE_SIZE];
    int has_dict_huf;
};

/**
 * @struct zxc_seek_source_t
 * @brief Where the archive bytes come from during parsing.
 *
 * The two public entry points differ only in this: @ref zxc_seekable_open holds
 * the whole archive in memory, @ref zxc_seekable_open_reader reaches it through
 * a positioned callback. Everything after the first read is common, so the
 * parser below takes a source instead of being written twice.
 */
typedef struct {
    const uint8_t* data;     /* in-memory archive, NULL in callback mode */
    const zxc_reader_t* rdr; /* callback mode, NULL in buffer mode */
    uint64_t size;           /* archive size, both modes */
} zxc_seek_source_t;

/**
 * @brief Reads a byte range from the archive, whatever backs it: the one bounds
 *        check every parsed offset goes through, written so it cannot wrap.
 * @return ZXC_OK; @ref ZXC_ERROR_SRC_TOO_SMALL outside the archive; the reader's
 *         code, or @ref ZXC_ERROR_IO, on a short read.
 */
static int zxc_seek_source_read(const zxc_seek_source_t* src, void* dst, const size_t len,
                                const uint64_t off) {
    if (UNLIKELY(off > src->size || (uint64_t)len > src->size - off))
        return ZXC_ERROR_SRC_TOO_SMALL;
    if (src->data) {
        ZXC_MEMCPY(dst, src->data + off, len);
        return ZXC_OK;
    }
    const int64_t r = src->rdr->read_at(src->rdr->ctx, dst, len, off);
    // A code outside int would truncate, possibly to ZXC_OK.
    if (UNLIKELY(r != (int64_t)len)) return (r < 0 && r >= INT_MIN) ? (int)r : ZXC_ERROR_IO;
    return ZXC_OK;
}

/**
 * @brief The handle's archive as a read source, for the per-block readers.
 */
static zxc_seek_source_t zxc_seek_source_of(const zxc_seekable* s) {
    const zxc_seek_source_t src = {s->src, s->src ? NULL : &s->reader, s->src_size};
    return src;
}

/**
 * @brief Locates and validates the seek table at the end of the archive.
 *
 * Three reads whatever the block count: file header, footer, then the EOF and
 * SEK block headers together. Entries are checked on access by
 * @ref zxc_seek_load_spans. Returns a handle to free via @ref zxc_seekable_free,
 * or NULL if the archive is too small or the seek table is missing / malformed.
 */
static zxc_seekable* zxc_seekable_parse(const zxc_seek_source_t* src) {
    // Step 1: validate file header => block_size
    uint8_t header[ZXC_FILE_HEADER_SIZE];
    if (UNLIKELY(zxc_seek_source_read(src, header, sizeof(header), 0) != ZXC_OK)) return NULL;

    size_t block_size_sz = 0;
    int file_has_chk = 0;
    int file_has_seek = 0;
    uint32_t header_dict_id = 0;
    if (UNLIKELY(zxc_read_file_header(header, sizeof(header), &block_size_sz, &file_has_chk,
                                      &header_dict_id, &file_has_seek) != ZXC_OK))
        return NULL;  // LCOV_EXCL_LINE
    // No table announced: nothing to look for at the end.
    if (!file_has_seek) return NULL;
    const uint32_t block_size = (uint32_t)block_size_sz;
    if (UNLIKELY(block_size == 0)) return NULL;  // LCOV_EXCL_LINE

    // Minimum: file_header(16) + eof_block(8) + seek_block_header(8) + footer,
    // 8 bytes or 16 with a digest: 40 or 48. Only the header says which.
    const uint64_t footer_len = zxc_footer_bytes(file_has_chk);
    if (UNLIKELY(src->size < ZXC_FILE_HEADER_SIZE + 2 * ZXC_BLOCK_HEADER_SIZE + footer_len))
        return NULL;

    // Step 2: read the source size, the first 8 bytes of the footer.
    uint8_t footer[ZXC_FILE_FOOTER_SIZE];
    if (UNLIKELY(zxc_seek_source_read(src, footer, sizeof(footer), src->size - footer_len) !=
                 ZXC_OK))
        return NULL;
    const uint64_t total_decomp = zxc_le64(footer);

    // Step 3: derive num_blocks = ceil(total_decomp / block_size)
    const uint64_t num_blocks = zxc_seek_block_count(total_decomp, block_size);

    // Step 4: locate the seek block, in 64 bits: the header's field only holds
    // the size modulo 2^32.
    const uint64_t seek_block_total = ZXC_BLOCK_HEADER_SIZE + zxc_seek_table_bytes(num_blocks);
    if (UNLIKELY(seek_block_total + footer_len > src->size)) return NULL;
    const uint64_t seek_off = src->size - footer_len - seek_block_total;
    // Layout: [header 16][data blocks][EOF 8][SEK block][footer]
    if (UNLIKELY(seek_off < ZXC_FILE_HEADER_SIZE + ZXC_BLOCK_HEADER_SIZE)) return NULL;
    const uint64_t eof_off = seek_off - ZXC_BLOCK_HEADER_SIZE;

    // Geometry only, no table read: num_blocks blocks of [header, entry_max] bytes
    // must fit the data area. Groups are checked on access.
    const uint64_t entry_max = (uint64_t)ZXC_BLOCK_HEADER_SIZE + block_size +
                               (file_has_chk ? ZXC_BLOCK_CHECKSUM_SIZE : 0U);
    const uint64_t data_area = eof_off - ZXC_FILE_HEADER_SIZE;

    const uint64_t min_span = num_blocks * ZXC_BLOCK_HEADER_SIZE;
    const uint64_t max_span =
        num_blocks > UINT64_MAX / entry_max ? UINT64_MAX : num_blocks * entry_max;
    if (UNLIKELY(data_area < min_span || data_area > max_span)) return NULL;

    // One read covers the EOF header and the SEK header behind it.
    uint8_t tail[2 * ZXC_BLOCK_HEADER_SIZE];
    if (UNLIKELY(zxc_seek_source_read(src, tail, sizeof(tail), eof_off) != ZXC_OK)) return NULL;

    zxc_block_header_t eof_bh;
    zxc_block_header_t bh;
    if (UNLIKELY(zxc_read_block_header(tail, ZXC_BLOCK_HEADER_SIZE, &eof_bh) != ZXC_OK ||
                 eof_bh.block_type != ZXC_BLOCK_EOF || eof_bh.comp_size != 0))
        return NULL;
    if (UNLIKELY(zxc_read_block_header(tail + ZXC_BLOCK_HEADER_SIZE, ZXC_BLOCK_HEADER_SIZE, &bh) !=
                     ZXC_OK ||
                 bh.block_type != ZXC_BLOCK_SEK ||
                 bh.comp_size != zxc_seek_size_field(zxc_seek_table_bytes(num_blocks))))
        return NULL;

    zxc_seekable* const s = (zxc_seekable*)ZXC_CALLOC(1, sizeof(zxc_seekable));
    if (UNLIKELY(!s)) return NULL;  // LCOV_EXCL_LINE

    if (src->rdr) s->reader = *src->rdr;
    s->src = src->data;
    s->src_size = src->size;
    s->num_blocks = num_blocks;
    s->total_decomp = total_decomp;
    s->table_off = seek_off + ZXC_BLOCK_HEADER_SIZE;
    s->eof_off = eof_off;
    s->entry_max = entry_max;
    s->block_size = block_size;
    s->file_has_checksums = file_has_chk;
    s->verify_checksums = 0; /* opt-in, see zxc_seekable_set_checksum */
    s->expected_dict_id = header_dict_id;
    return s;
}

/** @brief Scratch bound for @ref zxc_seek_load_spans: the groups blocks [@p first,
 *  @p first + @p n) touch. */
static size_t zxc_seek_spans_raw_max(const uint64_t first, const uint32_t n) {
    const uint64_t groups = (first + n - 1) / ZXC_SEEK_GROUP - first / ZXC_SEEK_GROUP + 1;
    return (size_t)groups * ZXC_SEEK_GROUP_BYTES;
}

/**
 * @brief Loads where each block of [@p first, @p first + @p n) starts and its on-disk
 *        size, both taken from the block's own group.
 *
 * One read of the groups the range touches, each checked alone: anchor in the data
 * area, sizes in [header, entry_max], end within the data area and, for the last
 * group, on the EOF block. The next anchor is not read, so a bad one costs only
 * its own group. A block's size is its own entry, never the gap to the next anchor,
 * so a range spanning groups gets the verdict each group gives alone.
 *
 * @param[in]  s       Handle; @p first + @p n must not exceed its block count.
 * @param[in]  first   First block index.
 * @param[in]  n       Block count, at least 1.
 * @param[out] starts  Room for @p n offsets.
 * @param[out] sizes   Room for @p n sizes, each in [header, entry_max].
 * @param[out] raw     Scratch of @ref zxc_seek_spans_raw_max bytes.
 * @return ZXC_OK, @ref ZXC_ERROR_IO (short read), @ref ZXC_ERROR_CORRUPT_DATA.
 */
static int zxc_seek_load_spans(const zxc_seekable* s, const uint64_t first, const uint32_t n,
                               uint64_t* RESTRICT starts, uint32_t* RESTRICT sizes,
                               uint8_t* RESTRICT raw) {
    const uint64_t last_group = zxc_seek_group_count(s->num_blocks) - 1;
    const uint64_t g0 = first / ZXC_SEEK_GROUP;
    const uint64_t g1 = (first + n - 1) / ZXC_SEEK_GROUP;

    size_t need = 0;
    for (uint64_t g = g0; g <= g1; g++)
        need += ZXC_SEEK_ANCHOR_SIZE +
                (size_t)zxc_seek_group_len(s->num_blocks, g) * ZXC_SEEK_SIZE_ENTRY;
    const zxc_seek_source_t src = zxc_seek_source_of(s);
    const int rc = zxc_seek_source_read(&src, raw, need, s->table_off + g0 * ZXC_SEEK_GROUP_BYTES);
    if (UNLIKELY(rc != ZXC_OK)) return rc;

    const uint8_t* p = raw;
    for (uint64_t g = g0; g <= g1; g++) {
        uint64_t pos = zxc_le64(p);
        p += ZXC_SEEK_ANCHOR_SIZE;
        if (UNLIKELY(g == 0 ? pos != ZXC_FILE_HEADER_SIZE
                            : pos < ZXC_FILE_HEADER_SIZE || pos > s->eof_off))
            return ZXC_ERROR_CORRUPT_DATA;

        const uint32_t cnt = zxc_seek_group_len(s->num_blocks, g);
        for (uint32_t k = 0; k < cnt; k++, p += ZXC_SEEK_SIZE_ENTRY) {
            const uint32_t sz = zxc_le32(p);
            if (UNLIKELY(sz < ZXC_BLOCK_HEADER_SIZE || sz > s->entry_max))
                return ZXC_ERROR_CORRUPT_DATA;
            const uint64_t idx = g * ZXC_SEEK_GROUP + k;
            if (idx >= first && idx < first + n) {
                starts[idx - first] = pos;
                sizes[idx - first] = sz;
            }
            pos += sz;  // no wrap: at most ZXC_SEEK_GROUP sizes of at most entry_max
        }
        // Within the data area; the last group, exactly on the EOF block.
        if (UNLIKELY(pos > s->eof_off || (g == last_group && pos != s->eof_off)))
            return ZXC_ERROR_CORRUPT_DATA;
    }
    return ZXC_OK;
}

/**
 * @brief Opens a seekable archive held entirely in a memory buffer.
 *
 * Public API; see @c zxc_seekable.h. Thin guard around
 * @ref zxc_seekable_parse, which detects and validates the trailing seek table.
 */
zxc_seekable* zxc_seekable_open(const void* src, const size_t src_size) {
    if (UNLIKELY(!src || src_size == 0)) return NULL;
    const zxc_seek_source_t source = {(const uint8_t*)src, NULL, (uint64_t)src_size};
    return zxc_seekable_parse(&source);
}

// zxc_seekable_open_file lives elsewhere: it builds a zxc_reader_t over pread()
// and delegates below, keeping this TU free of <stdio.h>.

/**
 * @brief Opens a seekable archive over a caller-supplied random-access reader.
 *
 * Public API; see @c zxc_seekable.h. Reads the file header, footer and the
 * EOF/SEK block headers through @p r->read_at (the FILE* variant wraps @c pread
 * this way). The archive is never mapped whole; entries are read as blocks are
 * accessed.
 */
zxc_seekable* zxc_seekable_open_reader(const zxc_reader_t* r) {
    if (UNLIKELY(!r || !r->read_at || r->size == 0)) return NULL;
    const zxc_seek_source_t source = {NULL, r, r->size};
    return zxc_seekable_parse(&source);
}

/**
 * @brief Number of blocks in the archive.
 */
uint64_t zxc_seekable_get_num_blocks(const zxc_seekable* s) { return s ? s->num_blocks : 0; }

/**
 * @brief Total decompressed size of the archive.
 */
uint64_t zxc_seekable_get_decompressed_size(const zxc_seekable* s) {
    return s ? s->total_decomp : 0;
}

/**
 * @brief Compressed byte size of a given block, from its seek table entry.
 */
uint32_t zxc_seekable_get_block_comp_size(const zxc_seekable* s, const uint64_t block_idx) {
    if (UNLIKELY(!s || block_idx >= s->num_blocks)) return 0;
    uint64_t start = 0;
    uint32_t size = 0;
    uint8_t raw[ZXC_SEEK_GROUP_BYTES];
    if (UNLIKELY(zxc_seek_load_spans(s, block_idx, 1, &start, &size, raw) != ZXC_OK)) return 0;
    return size;
}

/**
 * @brief Decompressed size of block @p idx (O(1)).
 *
 * Returns @p block_size for every block except the last, which holds the
 * remainder of @p total_decomp.
 *
 * @param[in] block_size    Fixed decompressed block size.
 * @param[in] total_decomp  Total decompressed archive size.
 * @param[in] idx           Zero-based block index.
 * @return Decompressed byte size of block @p idx.
 */
static uint32_t zxc_seek_decomp_size(const uint32_t block_size, const uint64_t total_decomp,
                                     const uint64_t idx) {
    const uint64_t start = idx * (uint64_t)block_size;
    const uint64_t remaining = total_decomp - start;
    return (remaining >= (uint64_t)block_size) ? block_size : (uint32_t)remaining;
}

/** @brief Decompressed byte size of a given block. */
uint32_t zxc_seekable_get_block_decomp_size(const zxc_seekable* s, const uint64_t block_idx) {
    if (UNLIKELY(!s || block_idx >= s->num_blocks)) return 0;
    return zxc_seek_decomp_size(s->block_size, s->total_decomp, block_idx);
}

// =========================================================================
// Random-Access Decompression
// =========================================================================

/**
 * @brief Maps a decompressed @p offset to its containing block index (O(1)).
 * @param[in] block_size  Fixed decompressed block size (a power of two).
 * @param[in] offset      Absolute decompressed byte offset.
 * @return Zero-based index of the block that holds @p offset.
 */
static uint64_t zxc_seek_find_block(const uint32_t block_size, const uint64_t offset) {
    return offset >> zxc_ctz32(block_size);
}

/**
 * @brief Decompressed start offset of block @p idx (O(1)).
 * @param[in] block_size  Fixed decompressed block size.
 * @param[in] idx         Zero-based block index.
 * @return Absolute decompressed byte offset where block @p idx begins.
 */
static uint64_t zxc_seek_decomp_offset(const uint32_t block_size, const uint64_t idx) {
    return idx * (uint64_t)block_size;
}

/**
 * @brief Reads a compressed block into @p buf from the memory buffer or reader.
 *
 * @p off and @p csz come from @ref zxc_seek_load_spans. What @p off points at must parse
 * as a block header, checksum included, and agree with @p csz. An entry pointing into a
 * block is refused; one moved onto a block of the same size is not, and only the
 * position-seeded checksum catches that.
 *
 * @param[in]  s        Seekable handle.
 * @param[in]  off      Byte offset of the block in the archive.
 * @param[in]  csz      On-disk size of the block, at least a header.
 * @param[out] buf      Destination buffer.
 * @param[in]  buf_cap  Capacity of @p buf in bytes.
 * @return @p csz, or a negative @ref zxc_error_t (@ref ZXC_ERROR_DST_TOO_SMALL,
 *         @ref ZXC_ERROR_SRC_TOO_SMALL, @ref ZXC_ERROR_IO, @ref ZXC_ERROR_BAD_HEADER,
 *         @ref ZXC_ERROR_CORRUPT_DATA).
 */
static int zxc_seek_read_block(const zxc_seekable* s, const uint64_t off, const uint32_t csz,
                               uint8_t* buf, const size_t buf_cap) {
    if (UNLIKELY(csz > buf_cap)) return ZXC_ERROR_DST_TOO_SMALL;
    const zxc_seek_source_t src = zxc_seek_source_of(s);
    const int rc = zxc_seek_source_read(&src, buf, csz, off);
    if (UNLIKELY(rc != ZXC_OK)) return rc;

    zxc_block_header_t bh;
    const int hdr_res = zxc_read_block_header(buf, csz, &bh);
    if (UNLIKELY(hdr_res != ZXC_OK)) return hdr_res;
    const uint64_t on_disk = (uint64_t)ZXC_BLOCK_HEADER_SIZE + bh.comp_size +
                             (s->file_has_checksums ? ZXC_BLOCK_CHECKSUM_SIZE : 0U);
    if (UNLIKELY(on_disk != csz)) return ZXC_ERROR_CORRUPT_DATA;
    return (int)csz;
}

/**
 * @brief Decompresses the byte range [@p offset, @p offset + @p len) into @p dst.
 *
 * Public API; full contract in @c zxc_seekable.h. Maps the range to its block
 * span via O(1) division, decodes each covered block through a reusable,
 * lazily-initialised, dictionary-aware context, and copies out only the
 * requested sub-range. Single-threaded; see @ref zxc_seekable_decompress_range_mt
 * for the parallel variant.
 */
int64_t zxc_seekable_decompress_range(zxc_seekable* s, void* dst, const size_t dst_capacity,
                                      const uint64_t offset, const size_t len) {
    if (UNLIKELY(len == 0)) return 0;
    if (UNLIKELY(!s || !dst)) return ZXC_ERROR_NULL_INPUT;
    if (UNLIKELY(dst_capacity < len)) return ZXC_ERROR_DST_TOO_SMALL;
    if (UNLIKELY(offset > s->total_decomp || len > s->total_decomp - offset))
        return ZXC_ERROR_SRC_TOO_SMALL;
    if (UNLIKELY(s->expected_dict_id != 0 && (!s->dict || s->dict_size == 0)))
        return ZXC_ERROR_DICT_REQUIRED;

    // Initialize decompression context on first use.
    if (!s->dctx_initialized) {
        // LCOV_EXCL_START
        if (UNLIKELY(zxc_cctx_init(&s->dctx, (size_t)s->block_size, 0, 0, 0, s->dict_size) !=
                     ZXC_OK))
            return ZXC_ERROR_MEMORY;
        // LCOV_EXCL_STOP
        if (UNLIKELY(zxc_cctx_attach_dict_huf(&s->dctx, s->has_dict_huf ? s->dict_huf : NULL) !=
                     ZXC_OK)) {
            // LCOV_EXCL_START
            zxc_cctx_free(&s->dctx);
            return ZXC_ERROR_CORRUPT_DATA;
            // LCOV_EXCL_STOP
        }
        s->dctx_initialized = 1;
        if (s->dict_size > 0) ZXC_MEMCPY(s->dctx.dict_buffer, s->dict, s->dict_size);
    }
    s->dctx.dict_size = s->dict_size;
    s->dctx.checksum_enabled = s->file_has_checksums && s->verify_checksums;

    // work_buf is pre-sized to block_size + ZXC_DECOMPRESS_TAIL_PAD by the
    // matching zxc_cctx_init above.
    const size_t work_sz = (size_t)s->block_size + ZXC_DECOMPRESS_TAIL_PAD;

    // Find block range - O(1) division
    const uint64_t blk_start = zxc_seek_find_block(s->block_size, offset);
    const uint64_t blk_end = zxc_seek_find_block(s->block_size, offset + len - 1);

    uint8_t* out = (uint8_t*)dst;
    size_t remaining = len;

    // One slice, and one read, per table group.
    uint64_t starts[ZXC_SEEK_GROUP] = {0};
    uint32_t sizes[ZXC_SEEK_GROUP] = {0};
    uint8_t raw[ZXC_SEEK_GROUP_BYTES];
    uint64_t bi = blk_start;
    while (bi <= blk_end) {
        const uint64_t left = blk_end - bi + 1;
        const uint32_t to_group_end = ZXC_SEEK_GROUP - (uint32_t)(bi % ZXC_SEEK_GROUP);
        const uint32_t n = left < to_group_end ? (uint32_t)left : to_group_end;
        const int span_res = zxc_seek_load_spans(s, bi, n, starts, sizes, raw);
        if (UNLIKELY(span_res < 0)) return span_res;

        // Block scratch kept on the handle, sized to the slice's largest block, not entry_max.
        uint64_t need = 0;
        for (uint32_t k = 0; k < n; k++)
            if (sizes[k] > need) need = sizes[k];
        need += ZXC_PAD_SIZE;
        if (s->read_buf_cap < need) {
            uint8_t* const nb = (uint8_t*)ZXC_REALLOC(s->read_buf, (size_t)need);
            if (UNLIKELY(!nb)) return ZXC_ERROR_MEMORY;  // LCOV_EXCL_LINE
            s->read_buf = nb;
            s->read_buf_cap = (size_t)need;
        }
        uint8_t* const read_buf = s->read_buf;

        for (uint32_t k = 0; k < n; k++, bi++) {
            // Read compressed block data
            const int read_res =
                zxc_seek_read_block(s, starts[k], sizes[k], read_buf, s->read_buf_cap);
            if (UNLIKELY(read_res < 0)) return read_res;

            // Decompress the block: when a dictionary is active, decode into the
            // cctx-owned dict_buffer (which has dict content prepended) so that
            // match copies referencing dictionary bytes resolve naturally.
            uint8_t* dec_dst =
                s->dctx.dict_buffer ? s->dctx.dict_buffer + s->dict_size : s->dctx.work_buf;
            const int dec_res = zxc_decompress_chunk_wrapper(&s->dctx, read_buf, (size_t)read_res,
                                                             dec_dst, work_sz, bi);
            if (UNLIKELY(dec_res < 0)) return dec_res;

            // Calculate which portion of this block's decompressed data we need
            const uint64_t blk_decomp_start = zxc_seek_decomp_offset(s->block_size, bi);
            const size_t skip =
                (offset > blk_decomp_start) ? (size_t)(offset - blk_decomp_start) : 0;
            if (UNLIKELY((size_t)dec_res < skip)) return ZXC_ERROR_CORRUPT_DATA;  // LCOV_EXCL_LINE
            const size_t avail = (size_t)dec_res - skip;
            const size_t copy = (avail < remaining) ? avail : remaining;

            ZXC_MEMCPY(out, dec_dst + skip, copy);
            out += copy;
            remaining -= copy;
        }
    }

    if (UNLIKELY(remaining != 0)) return ZXC_ERROR_CORRUPT_DATA;
    return (int64_t)len;
}

// =========================================================================
// Multi-Threaded Random-Access Decompression (Fork-Join)
// =========================================================================

/**
 * @brief Per-block job descriptor for multi-threaded decompression.
 *
 * Each worker thread receives a pointer to one of these, performs the read +
 * decompress + memcpy sequence, and writes the result code into @c result.
 * The main thread inspects @c result after join.
 */
typedef struct {
    uint64_t block_idx; /* frame position: the checksum seed */
    uint64_t off;       /* where it starts in the archive (validated span) */
    size_t dst_off;     /* output offset from the caller's buffer start */
    size_t skip;        /* bytes to skip at start of decompressed block */
    size_t copy_len;    /* bytes to copy out */
    uint32_t csz;       /* its on-disk size */
    int result;         /* 0 = OK, < 0 = error; starts negative, see the launch loop */
} zxc_seek_mt_job_t;

/**
 * @struct zxc_seek_mt_shared_t
 * @brief State shared by every worker of one multi-threaded range read.
 *
 * Worker @c k owns the job subset {k, k+stride, k+2*stride, ...} and reuses one
 * decompression context, one dictionary copy and one read buffer across all of
 * them. Stripes are pairwise disjoint, so workers touch distinct jobs and
 * distinct output ranges and need no synchronisation beyond claiming their
 * index at start-up and the final join.
 *
 * Lives on the caller's stack: the caller's output pointer therefore never
 * reaches heap memory (jobs carry offsets into it), and there is no per-thread
 * array to size for @c ZXC_MAX_THREADS.
 *
 * @var zxc_seek_mt_shared_t::s          Handle being read (read-only).
 * @var zxc_seek_mt_shared_t::jobs       Job array; each worker touches only its stripe.
 * @var zxc_seek_mt_shared_t::dst_base   Start of the caller's output buffer.
 * @var zxc_seek_mt_shared_t::num_jobs   Job count (stripe iteration bound).
 * @var zxc_seek_mt_shared_t::stride     Worker count attempted; stripes partition by it.
 * @var zxc_seek_mt_shared_t::next       Next stripe index to hand out, under @c lock.
 * @var zxc_seek_mt_shared_t::lock       Guards @c next.
 */
typedef struct {
    const zxc_seekable* s;
    zxc_seek_mt_job_t* jobs;
    uint8_t* dst_base;
    uint32_t num_jobs;
    uint32_t stride;
    uint32_t next;
    pthread_mutex_t lock;
} zxc_seek_mt_shared_t;

/**
 * @brief Marks every job of stripe @p first with @p code (setup-failure path).
 *
 * @param[in,out] sh    Shared state.
 * @param[in]     first Stripe index.
 * @param[in]     code  Negative @ref zxc_error_t value.
 */
static void zxc_seek_mt_fail_stripe(const zxc_seek_mt_shared_t* sh, const uint32_t first,
                                    const int code) {
    for (uint32_t i = first; i < sh->num_jobs; i += sh->stride) sh->jobs[i].result = code;
}

/**
 * @brief Worker thread entry point for multi-threaded seekable decompression.
 *
 * Sets up its context, dictionary copy and read buffer once, then for each
 * block of its stripe: read (thread-safe pread), decompress, copy the
 * requested sub-range into the caller's output.  The dict prefix survives
 * across blocks because the decoder never writes below its dst.
 *
 * Each job's outcome goes into its @c result (read by the main thread after
 * join); on error the worker abandons the rest of its stripe.
 *
 * @param[in,out] arg  Pointer to the read's `zxc_seek_mt_shared_t`.
 * @return Always NULL (result codes are reported via the jobs).
 */
static void* zxc_seek_mt_worker(void* arg) {
    zxc_seek_mt_shared_t* const sh = (zxc_seek_mt_shared_t*)arg;
    zxc_seek_mt_job_t* const jobs = sh->jobs;
    const zxc_seekable* const s = sh->s;

    // Claim a stripe: the main thread hands every worker the same argument.
    pthread_mutex_lock(&sh->lock);
    const uint32_t first = sh->next++;
    pthread_mutex_unlock(&sh->lock);

    // Thread-local decompression context (mode=0 for decompress-only)
    zxc_cctx_t dctx;
    if (UNLIKELY(zxc_cctx_init(&dctx, (size_t)s->block_size, 0, 0,
                               s->file_has_checksums && s->verify_checksums,
                               s->dict_size) != ZXC_OK)) {
        // LCOV_EXCL_START
        zxc_seek_mt_fail_stripe(sh, first, ZXC_ERROR_MEMORY);
        return NULL;
        // LCOV_EXCL_STOP
    }

    if (UNLIKELY(zxc_cctx_attach_dict_huf(&dctx, s->has_dict_huf ? s->dict_huf : NULL) != ZXC_OK)) {
        // LCOV_EXCL_START
        zxc_cctx_free(&dctx);
        zxc_seek_mt_fail_stripe(sh, first, ZXC_ERROR_CORRUPT_DATA);
        return NULL;
        // LCOV_EXCL_STOP
    }
    const size_t work_sz = (size_t)s->block_size + ZXC_DECOMPRESS_TAIL_PAD;

    uint8_t* const dict_work = dctx.dict_buffer;
    if (dict_work) ZXC_MEMCPY(dict_work, s->dict, s->dict_size);

    // Read buffer sized for the largest compressed block of the stripe.
    size_t max_csz = 0;
    for (uint32_t i = first; i < sh->num_jobs; i += sh->stride) {
        if (jobs[i].csz > max_csz) max_csz = jobs[i].csz;
    }
    uint8_t* const read_buf = (uint8_t*)ZXC_MALLOC(max_csz + ZXC_PAD_SIZE);
    if (UNLIKELY(!read_buf)) {
        // LCOV_EXCL_START
        zxc_cctx_free(&dctx);
        zxc_seek_mt_fail_stripe(sh, first, ZXC_ERROR_MEMORY);
        return NULL;
        // LCOV_EXCL_STOP
    }

    for (uint32_t i = first; i < sh->num_jobs; i += sh->stride) {
        zxc_seek_mt_job_t* const job = &jobs[i];

        const int read_res =
            zxc_seek_read_block(s, job->off, job->csz, read_buf, max_csz + ZXC_PAD_SIZE);
        if (UNLIKELY(read_res < 0)) {
            job->result = read_res;
            break;
        }

        // Decompress: use dict bounce buffer when dictionary is active
        uint8_t* dec_dst = dict_work ? dict_work + s->dict_size : dctx.work_buf;
        const int dec_res = zxc_decompress_chunk_wrapper(&dctx, read_buf, (size_t)read_res, dec_dst,
                                                         work_sz, job->block_idx);

        if (UNLIKELY(dec_res < 0)) {
            job->result = dec_res;
            break;
        }
        if (UNLIKELY((size_t)dec_res < job->skip + job->copy_len)) {
            job->result = ZXC_ERROR_CORRUPT_DATA;
            break;
        }

        // Copy the requested portion directly into the caller's output buffer
        ZXC_MEMCPY(sh->dst_base + job->dst_off, dec_dst + job->skip, job->copy_len);
        job->result = 0;
    }

    ZXC_FREE(read_buf);
    zxc_cctx_free(&dctx);
    return NULL;
}

/**
 * @brief Multi-threaded variant of @ref zxc_seekable_decompress_range.
 *
 * Public API; full contract in @c zxc_seekable.h. Plans one job per covered
 * block (each with its own thread-local context and read buffer) and runs them
 * fork-join in waves of up to @p n_threads. Falls back to the single-threaded
 * path for trivial spans. @p n_threads == 0 auto-detects the core count.
 */
int64_t zxc_seekable_decompress_range_mt(zxc_seekable* s, void* dst, const size_t dst_capacity,
                                         const uint64_t offset, const size_t len, int n_threads) {
    if (UNLIKELY(len == 0)) return 0;
    if (UNLIKELY(!s || !dst)) return ZXC_ERROR_NULL_INPUT;
    if (UNLIKELY(dst_capacity < len)) return ZXC_ERROR_DST_TOO_SMALL;
    if (UNLIKELY(offset > s->total_decomp || len > s->total_decomp - offset))
        return ZXC_ERROR_SRC_TOO_SMALL;
    if (UNLIKELY(s->expected_dict_id != 0 && (!s->dict || s->dict_size == 0)))
        return ZXC_ERROR_DICT_REQUIRED;

    // Find block range - O(1) division
    const uint64_t blk_start = zxc_seek_find_block(s->block_size, offset);
    const uint64_t blk_end = zxc_seek_find_block(s->block_size, offset + len - 1);
    const uint64_t jobs_64 = blk_end - blk_start + 1;

    // Auto-detect thread count (0 = use all available cores)
    if (n_threads == 0) n_threads = zxc_num_procs();

    // Fallback to single-threaded path for trivial cases, and for a range holding
    // more blocks than one job table can index (4 G blocks is 16 TiB of dst).
    if (n_threads <= 1 || jobs_64 <= 1 || jobs_64 > UINT32_MAX) {
        return zxc_seekable_decompress_range(s, dst, dst_capacity, offset, len);
    }
    const uint32_t num_jobs = (uint32_t)jobs_64;

    // Cap threads to number of blocks and max limit
    if ((uint32_t)n_threads > num_jobs) n_threads = (int)num_jobs;
    if (n_threads > ZXC_MAX_THREADS) n_threads = ZXC_MAX_THREADS;

    // Allocate job descriptors, and the span's table groups in one read.
    zxc_seek_mt_job_t* const jobs =
        (zxc_seek_mt_job_t*)ZXC_CALLOC(num_jobs, sizeof(zxc_seek_mt_job_t));
    uint64_t* const starts = (uint64_t*)ZXC_CALLOC(num_jobs, sizeof(uint64_t));
    uint32_t* const sizes = (uint32_t*)ZXC_CALLOC(num_jobs, sizeof(uint32_t));
    uint8_t* const raw = (uint8_t*)ZXC_MALLOC(zxc_seek_spans_raw_max(blk_start, num_jobs));
    if (UNLIKELY(!jobs || !starts || !sizes || !raw)) {
        // LCOV_EXCL_START
        ZXC_FREE(jobs);
        ZXC_FREE(starts);
        ZXC_FREE(sizes);
        ZXC_FREE(raw);
        return ZXC_ERROR_MEMORY;
        // LCOV_EXCL_STOP
    }
    const int span_res = zxc_seek_load_spans(s, blk_start, num_jobs, starts, sizes, raw);
    ZXC_FREE(raw);
    if (UNLIKELY(span_res < 0)) {
        ZXC_FREE(jobs);
        ZXC_FREE(starts);
        ZXC_FREE(sizes);
        return span_res;
    }

    // Plan jobs: compute skip, copy_len and output offset for each block
    size_t out_off = 0;
    size_t remaining = len;
    for (uint32_t i = 0; i < num_jobs; i++) {
        const uint64_t bi = blk_start + i;
        const uint64_t blk_decomp_start = zxc_seek_decomp_offset(s->block_size, bi);
        const size_t skip = (offset > blk_decomp_start) ? (size_t)(offset - blk_decomp_start) : 0;
        const size_t blk_decomp_sz = zxc_seek_decomp_size(s->block_size, s->total_decomp, bi);
        if (UNLIKELY(blk_decomp_sz < skip)) {
            // LCOV_EXCL_START
            ZXC_FREE(jobs);
            ZXC_FREE(starts);
            ZXC_FREE(sizes);
            return ZXC_ERROR_CORRUPT_DATA;
            // LCOV_EXCL_STOP
        }
        const size_t avail = blk_decomp_sz - skip;
        const size_t copy = (avail < remaining) ? avail : remaining;

        jobs[i].block_idx = bi;
        jobs[i].off = starts[i];
        jobs[i].csz = sizes[i];
        jobs[i].dst_off = out_off;
        jobs[i].skip = skip;
        jobs[i].copy_len = copy;
        // Negative until a worker completes it: a stripe whose thread failed to
        // start is then reported without anyone having to know which one.
        jobs[i].result = ZXC_ERROR_MEMORY;

        out_off += copy;
        remaining -= copy;
    }
    ZXC_FREE(starts);
    ZXC_FREE(sizes);

    // Launch one persistent worker per thread
    pthread_t* const threads = (pthread_t*)ZXC_MALLOC((size_t)n_threads * sizeof(pthread_t));
    if (UNLIKELY(!threads)) {
        // LCOV_EXCL_START
        ZXC_FREE(jobs);
        return ZXC_ERROR_MEMORY;
        // LCOV_EXCL_STOP
    }

    zxc_seek_mt_shared_t sh;
    sh.s = s;
    sh.jobs = jobs;
    sh.dst_base = (uint8_t*)dst;
    sh.num_jobs = num_jobs;
    sh.stride = (uint32_t)n_threads;
    sh.next = 0;
    pthread_mutex_init(&sh.lock, NULL);

    int launched = 0;
    for (int t = 0; t < n_threads; t++) {
        // A failed start leaves one stripe unclaimed; its jobs keep their
        // negative result and the read reports it after the join.
        if (UNLIKELY(pthread_create(&threads[launched], NULL, zxc_seek_mt_worker, &sh) != 0))
            continue;  // LCOV_EXCL_LINE
        launched++;
    }

    // Join phase
    for (int t = 0; t < launched; t++) pthread_join(threads[t], NULL);

    pthread_mutex_destroy(&sh.lock);
    ZXC_FREE(threads);

    // Report the first error in job order, if any.
    int64_t result = (int64_t)len;
    for (uint32_t i = 0; i < num_jobs; i++) {
        if (jobs[i].result < 0) {
            result = (int64_t)jobs[i].result;
            break;
        }
    }

    ZXC_FREE(jobs);
    return result;
}

/**
 * @brief Releases a seekable handle and every resource it owns.
 *
 * Public API; see @c zxc_seekable.h. Tears down the reusable context, the
 * owned dictionary copy and any attached reader context. NULL-safe.
 */
void zxc_seekable_free(zxc_seekable* s) {
    if (UNLIKELY(!s)) return;
    if (s->dctx_initialized) zxc_cctx_free(&s->dctx);
    ZXC_FREE(s->dict);
    ZXC_FREE(s->read_buf);
    ZXC_FREE(s->owned_reader_ctx);
    ZXC_FREE(s);
}

/**
 * @brief Turns per-block checksum verification on or off.
 *
 * Public API; see @c zxc_seekable.h. Only records the wish; the decode path
 * pushes it into the context on every call, like @c dict_size.
 */
int zxc_seekable_set_checksum(zxc_seekable* s, const int enabled) {
    if (UNLIKELY(!s)) return ZXC_ERROR_NULL_INPUT;
    s->verify_checksums = enabled ? 1 : 0;
    return ZXC_OK;
}

/**
 * @brief Installs the dictionary needed to decode a dict-compressed archive.
 *
 * Public API; full contract in @c zxc_seekable.h. Validates the dict_id against
 * the file header, then takes an owned copy of @p dict (and the optional shared
 * literal Huffman table @p dict_huf). Drops any context already built so the
 * [dict | decode] bounce buffer is re-carved on the next decompress.
 */
int zxc_seekable_set_dict(zxc_seekable* s, const void* dict, const size_t dict_size,
                          const void* dict_huf) {
    if (UNLIKELY(!s || !dict || dict_size == 0)) return ZXC_ERROR_NULL_INPUT;
    if (UNLIKELY(dict_size > ZXC_DICT_SIZE_MAX)) return ZXC_ERROR_DICT_TOO_LARGE;
    if (UNLIKELY(s->expected_dict_id != 0 &&
                 zxc_dict_id(dict, dict_size, (const uint8_t*)dict_huf) != s->expected_dict_id))
        return ZXC_ERROR_DICT_MISMATCH;

    ZXC_FREE(s->dict);
    s->dict = NULL;
    s->dict_size = 0;
    s->has_dict_huf = 0;

    s->dict = (uint8_t*)ZXC_MALLOC(dict_size);
    if (UNLIKELY(!s->dict)) return ZXC_ERROR_MEMORY;
    ZXC_MEMCPY(s->dict, dict, dict_size);
    s->dict_size = dict_size;
    if (dict_huf) {
        ZXC_MEMCPY(s->dict_huf, dict_huf, ZXC_HUF_TABLE_SIZE);
        s->has_dict_huf = 1;
    }

    // The [dict | decode] bounce buffer is carved into the dctx workspace.
    // Drop any context built without it (or for a different dict size) so it is
    // re-carved with the new dict on the next decompress.
    if (s->dctx_initialized) {
        zxc_cctx_free(&s->dctx);
        s->dctx_initialized = 0;
    }
    return ZXC_OK;
}

/**
 * @brief Transfers ownership of a heap reader context to the handle.
 *
 * Cross-TU hook (declared in @c zxc_internal.h): @p ctx is released via
 * @c ZXC_FREE when @ref zxc_seekable_free runs. Used by
 * @ref zxc_seekable_open_file so its allocated reader state outlives the open
 * call. NULL-safe on @p s.
 */
void zxc_seekable_attach_owned_ctx(zxc_seekable* s, void* ctx) {
    if (s) s->owned_reader_ctx = ctx;
}
