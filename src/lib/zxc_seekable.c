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
 * between the EOF block and the file footer.  It records the compressed and
 * decompressed sizes of every block, enabling O(log N) lookup + O(block_size)
 * decompression for any byte range.
 *
 * On-disk layout of a SEEK block:
 *
 *   [Block Header (8B)]   block_type=SEEK, block_flags, comp_size
 *   [N × Entry]           comp_size(4) + decomp_size(4) [+ checksum(4)]
 *   [num_blocks (4B LE)]  tail for backward detection
 *
 * Detection from end of file:
 *   1. Read file footer (last 12 bytes)
 *   2. Read 4 bytes before footer → num_blocks (u32 LE)
 *   3. Compute seek block size, read backward to the block header
 *   4. Validate block_type == ZXC_BLOCK_SEK
 */

#include "../../include/zxc_seekable.h"

#include "../../include/zxc_error.h"
#include "../../include/zxc_sans_io.h"
#include "zxc_internal.h"

/* ========================================================================= */
/*  Seek Table Writer                                                        */
/* ========================================================================= */

size_t zxc_seek_table_size(const uint32_t num_blocks, const int has_checksums) {
    const size_t entry_sz = has_checksums ? ZXC_SEEK_ENTRY_SIZE_CRC : ZXC_SEEK_ENTRY_SIZE;
    return ZXC_BLOCK_HEADER_SIZE + (size_t)num_blocks * entry_sz + ZXC_SEEK_TAIL_SIZE;
}

int64_t zxc_write_seek_table(uint8_t* dst, const size_t dst_capacity, const uint32_t* comp_sizes,
                             const uint32_t* decomp_sizes, const uint32_t num_blocks,
                             const int has_checksums) {
    const size_t total = zxc_seek_table_size(num_blocks, has_checksums);
    if (UNLIKELY(dst_capacity < total)) return ZXC_ERROR_DST_TOO_SMALL;
    if (UNLIKELY(!dst || !comp_sizes || !decomp_sizes)) return ZXC_ERROR_NULL_INPUT;

    const size_t entry_sz = has_checksums ? ZXC_SEEK_ENTRY_SIZE_CRC : ZXC_SEEK_ENTRY_SIZE;
    const uint32_t payload_size = (uint32_t)(num_blocks * entry_sz + ZXC_SEEK_TAIL_SIZE);

    /* Write standard ZXC block header */
    const zxc_block_header_t bh = {.block_type = ZXC_BLOCK_SEK,
                                   .block_flags = has_checksums ? ZXC_SEEK_FLAG_CHECKSUM : 0,
                                   .reserved = 0,
                                   .comp_size = payload_size};
    const int hdr_res = zxc_write_block_header(dst, dst_capacity, &bh);
    if (UNLIKELY(hdr_res < 0)) return hdr_res;
    uint8_t* p = dst + hdr_res;

    /* Write entries: comp_size(4) + decomp_size(4) [+ checksum(4)] */
    for (uint32_t i = 0; i < num_blocks; i++) {
        zxc_store_le32(p, comp_sizes[i]);
        p += sizeof(uint32_t);
        zxc_store_le32(p, decomp_sizes[i]);
        p += sizeof(uint32_t);
        if (has_checksums) {
            /* Checksum slot — zeroed for now (can be extended later) */
            zxc_store_le32(p, 0);
            p += sizeof(uint32_t);
        }
    }

    /* Write tail: num_blocks (for backward detection) */
    zxc_store_le32(p, num_blocks);
    p += sizeof(uint32_t);

    return (int64_t)(p - dst);
}

/* ========================================================================= */
/*  Seekable Reader (Opaque Handle)                                          */
/* ========================================================================= */

struct zxc_seekable_s {
    /* Source — exactly one is non-NULL */
    const uint8_t* src;
    size_t src_size;
    FILE* file;

    /* Parsed seek table */
    uint32_t num_blocks;
    uint32_t* comp_sizes;     /* array[num_blocks] */
    uint32_t* decomp_sizes;   /* array[num_blocks] */
    uint64_t* comp_offsets;   /* prefix-sum: byte offset in compressed file per block */
    uint64_t* decomp_offsets; /* prefix-sum: byte offset in decompressed data per block */
    uint64_t total_decomp;    /* sum of all decomp_sizes */
    int has_checksums;        /* from block_flags */

    /* File header info */
    size_t block_size;
    int file_has_checksums;

    /* Reusable decompression context */
    zxc_cctx_t dctx;
    int dctx_initialized;
};

/**
 * @brief Parses the seek table from raw bytes at the end of the archive.
 *
 * Detection (backward from end):
 *   1. Skip file footer (last 12 bytes)
 *   2. Read 4 bytes before footer → num_blocks
 *   3. Read the block header at the computed offset
 *   4. Validate block_type == ZXC_BLOCK_SEK
 */
static zxc_seekable* zxc_seekable_parse(const uint8_t* data, const size_t data_size) {
    /* Minimum: file_header(16) + eof_block(8) + seek_block_header(8)
     *          + seek_tail(4) + file_footer(12) = 48 */
    const size_t MIN_SEEKABLE_SIZE = ZXC_FILE_HEADER_SIZE + ZXC_BLOCK_HEADER_SIZE +
                                     ZXC_BLOCK_HEADER_SIZE + ZXC_SEEK_TAIL_SIZE +
                                     ZXC_FILE_FOOTER_SIZE;
    if (UNLIKELY(data_size < MIN_SEEKABLE_SIZE)) return NULL;

    /* Step 1: validate file header */
    size_t block_size = 0;
    int file_has_chk = 0;
    if (UNLIKELY(zxc_read_file_header(data, data_size, &block_size, &file_has_chk) != ZXC_OK))
        return NULL;

    /* Step 2: read num_blocks from the tail (4 bytes before file footer) */
    const uint8_t* const tail_ptr = data + data_size - ZXC_FILE_FOOTER_SIZE - ZXC_SEEK_TAIL_SIZE;
    if (UNLIKELY(tail_ptr < data)) return NULL;
    const uint32_t num_blocks = zxc_le32(tail_ptr);

    /* A value of 0 means no seek table */
    if (UNLIKELY(num_blocks == 0)) return NULL;

    /* Step 3: determine entry size by reading the block header */
    /* The seek block starts at: tail_ptr - entries - block_header */
    /* We don't know entry_size yet, but the block header has block_flags. */
    /* First, try default entry size (8), compute the header position,
     * read block_flags to determine actual entry size, then re-validate. */

    /* Try to locate the block header — we need to read it to know entry_size.
     * Start with the assumption of no checksums (entry_size = 8). */
    for (int pass = 0; pass < 2; pass++) {
        const size_t entry_sz = (pass == 0) ? ZXC_SEEK_ENTRY_SIZE : ZXC_SEEK_ENTRY_SIZE_CRC;
        const size_t entries_total = (size_t)num_blocks * entry_sz;
        const uint8_t* const seek_block_start = tail_ptr - entries_total - ZXC_BLOCK_HEADER_SIZE;
        if (UNLIKELY(seek_block_start < data)) continue;

        /* Read block header at the computed position */
        zxc_block_header_t bh;
        if (UNLIKELY(
                zxc_read_block_header(seek_block_start,
                                      (size_t)(tail_ptr + ZXC_SEEK_TAIL_SIZE - seek_block_start),
                                      &bh) != ZXC_OK))
            continue;

        /* Validate block type */
        if (bh.block_type != ZXC_BLOCK_SEK) continue;

        /* Validate comp_size consistency */
        const uint32_t expected_payload = (uint32_t)(entries_total + ZXC_SEEK_TAIL_SIZE);
        if (bh.comp_size != expected_payload) continue;

        /* Validate entry_size matches the checksum flag */
        const int has_crc = (bh.block_flags & ZXC_SEEK_FLAG_CHECKSUM) != 0;
        if (has_crc && entry_sz != ZXC_SEEK_ENTRY_SIZE_CRC) continue;
        if (!has_crc && entry_sz != ZXC_SEEK_ENTRY_SIZE) continue;

        /* ✓ Found a valid seek block. Parse entries. */

        zxc_seekable* const s = (zxc_seekable*)calloc(1, sizeof(zxc_seekable));
        if (UNLIKELY(!s)) return NULL;

        s->num_blocks = num_blocks;
        s->has_checksums = has_crc;
        s->block_size = block_size;
        s->file_has_checksums = file_has_chk;
        s->src = data;
        s->src_size = data_size;

        /* Allocate arrays */
        s->comp_sizes = (uint32_t*)calloc(num_blocks, sizeof(uint32_t));
        s->decomp_sizes = (uint32_t*)calloc(num_blocks, sizeof(uint32_t));
        s->comp_offsets = (uint64_t*)calloc((size_t)num_blocks + 1, sizeof(uint64_t));
        s->decomp_offsets = (uint64_t*)calloc((size_t)num_blocks + 1, sizeof(uint64_t));
        if (UNLIKELY(!s->comp_sizes || !s->decomp_sizes || !s->comp_offsets ||
                     !s->decomp_offsets)) {
            zxc_seekable_free(s);
            return NULL;
        }

        /* Parse entries and build prefix sums */
        const uint8_t* ep = seek_block_start + ZXC_BLOCK_HEADER_SIZE;
        uint64_t comp_acc = ZXC_FILE_HEADER_SIZE; /* blocks start after file header */
        uint64_t decomp_acc = 0;
        for (uint32_t i = 0; i < num_blocks; i++) {
            s->comp_sizes[i] = zxc_le32(ep);
            ep += sizeof(uint32_t);
            s->decomp_sizes[i] = zxc_le32(ep);
            ep += sizeof(uint32_t);
            if (has_crc) ep += sizeof(uint32_t); /* skip checksum */

            s->comp_offsets[i] = comp_acc;
            s->decomp_offsets[i] = decomp_acc;
            comp_acc += s->comp_sizes[i];
            decomp_acc += s->decomp_sizes[i];
        }
        s->comp_offsets[num_blocks] = comp_acc;
        s->decomp_offsets[num_blocks] = decomp_acc;
        s->total_decomp = decomp_acc;

        return s;
    }

    return NULL; /* No valid seek block found */
}

zxc_seekable* zxc_seekable_open(const void* src, const size_t src_size) {
    if (UNLIKELY(!src || src_size == 0)) return NULL;
    return zxc_seekable_parse((const uint8_t*)src, src_size);
}

zxc_seekable* zxc_seekable_open_file(FILE* f) {
    if (UNLIKELY(!f)) return NULL;

    const long long saved_pos = ftello(f);
    if (UNLIKELY(saved_pos < 0)) return NULL;

    if (UNLIKELY(fseeko(f, 0, SEEK_END) != 0)) return NULL;
    const long long file_size = ftello(f);
    if (UNLIKELY(file_size <= 0)) {
        fseeko(f, saved_pos, SEEK_SET);
        return NULL;
    }

    /* Read the tail: we need at most the seek table + footer.
     * For safety, read the last 64 KB or the whole file if smaller. */
    const size_t tail_size = ((size_t)file_size > 65536) ? 65536 : (size_t)file_size;
    uint8_t* const tail = (uint8_t*)malloc(tail_size);
    if (UNLIKELY(!tail)) {
        fseeko(f, saved_pos, SEEK_SET);
        return NULL;
    }

    if (UNLIKELY(fseeko(f, file_size - (long long)tail_size, SEEK_SET) != 0 ||
                 fread(tail, 1, tail_size, f) != tail_size)) {
        free(tail);
        fseeko(f, saved_pos, SEEK_SET);
        return NULL;
    }
    fseeko(f, saved_pos, SEEK_SET);

    /* For file mode, we need the full file header too. Read it if not in tail. */
    uint8_t header[ZXC_FILE_HEADER_SIZE];
    if (tail_size < (size_t)file_size) {
        if (UNLIKELY(fseeko(f, 0, SEEK_SET) != 0 ||
                     fread(header, 1, ZXC_FILE_HEADER_SIZE, f) != ZXC_FILE_HEADER_SIZE)) {
            free(tail);
            fseeko(f, saved_pos, SEEK_SET);
            return NULL;
        }
        fseeko(f, saved_pos, SEEK_SET);
    }

    /* Detect seek table from the tail */
    if (UNLIKELY(tail_size < ZXC_FILE_FOOTER_SIZE + ZXC_SEEK_TAIL_SIZE)) {
        free(tail);
        return NULL;
    }

    /* Read num_blocks from tail */
    const uint8_t* const nblk_ptr = tail + tail_size - ZXC_FILE_FOOTER_SIZE - ZXC_SEEK_TAIL_SIZE;
    const uint32_t num_blocks = zxc_le32(nblk_ptr);
    if (num_blocks == 0) {
        free(tail);
        return NULL;
    }

    /* Compute seek block size to check if tail buffer is large enough */
    /* Try both entry sizes; the larger one gives an upper bound */
    const size_t max_seek_size =
        ZXC_BLOCK_HEADER_SIZE + (size_t)num_blocks * ZXC_SEEK_ENTRY_SIZE_CRC + ZXC_SEEK_TAIL_SIZE;

    /* We need seek block + footer in the tail */
    if (UNLIKELY(tail_size < max_seek_size + ZXC_FILE_FOOTER_SIZE)) {
        /* Seek table is larger than our tail buffer — read the entire file */
        free(tail);
        uint8_t* const full = (uint8_t*)malloc((size_t)file_size);
        if (UNLIKELY(!full)) return NULL;
        if (UNLIKELY(fseeko(f, 0, SEEK_SET) != 0 ||
                     fread(full, 1, (size_t)file_size, f) != (size_t)file_size)) {
            free(full);
            fseeko(f, saved_pos, SEEK_SET);
            return NULL;
        }
        fseeko(f, saved_pos, SEEK_SET);
        zxc_seekable* const s = zxc_seekable_parse(full, (size_t)file_size);
        if (s) {
            s->src = NULL;
            s->src_size = (size_t)file_size;
            s->file = f;
        }
        free(full);
        return s;
    }

    /* File small enough or tail large enough — read the full file if <= 64 MB */
    if ((size_t)file_size <= 64 * 1024 * 1024) {
        uint8_t* const full = (uint8_t*)malloc((size_t)file_size);
        if (UNLIKELY(!full)) {
            free(tail);
            return NULL;
        }
        if (UNLIKELY(fseeko(f, 0, SEEK_SET) != 0 ||
                     fread(full, 1, (size_t)file_size, f) != (size_t)file_size)) {
            free(full);
            free(tail);
            fseeko(f, saved_pos, SEEK_SET);
            return NULL;
        }
        fseeko(f, saved_pos, SEEK_SET);
        free(tail);
        zxc_seekable* const s = zxc_seekable_parse(full, (size_t)file_size);
        if (s) {
            s->src = NULL;
            s->src_size = (size_t)file_size;
            s->file = f;
        }
        free(full);
        return s;
    }

    /* Large file: parse seek table directly from the tail */
    size_t bs = 0;
    int fhc = 0;
    if (UNLIKELY(zxc_read_file_header(header, ZXC_FILE_HEADER_SIZE, &bs, &fhc) != ZXC_OK)) {
        free(tail);
        return NULL;
    }

    /* We need to find the block header in the tail. Try both entry sizes. */
    zxc_seekable* s = NULL;
    for (int pass = 0; pass < 2 && !s; pass++) {
        const size_t entry_sz = (pass == 0) ? ZXC_SEEK_ENTRY_SIZE : ZXC_SEEK_ENTRY_SIZE_CRC;
        const size_t entries_total = (size_t)num_blocks * entry_sz;
        const size_t seek_block_total = ZXC_BLOCK_HEADER_SIZE + entries_total + ZXC_SEEK_TAIL_SIZE;

        if (tail_size < seek_block_total + ZXC_FILE_FOOTER_SIZE) continue;

        /* Block header position in the tail */
        const uint8_t* const bh_ptr = tail + tail_size - ZXC_FILE_FOOTER_SIZE - seek_block_total;

        zxc_block_header_t bh;
        if (zxc_read_block_header(bh_ptr, seek_block_total, &bh) != ZXC_OK) continue;
        if (bh.block_type != ZXC_BLOCK_SEK) continue;
        if (bh.comp_size != (uint32_t)(entries_total + ZXC_SEEK_TAIL_SIZE)) continue;

        const int has_crc = (bh.block_flags & ZXC_SEEK_FLAG_CHECKSUM) != 0;
        if (has_crc && entry_sz != ZXC_SEEK_ENTRY_SIZE_CRC) continue;
        if (!has_crc && entry_sz != ZXC_SEEK_ENTRY_SIZE) continue;

        s = (zxc_seekable*)calloc(1, sizeof(zxc_seekable));
        if (UNLIKELY(!s)) break;
        s->file = f;
        s->src = NULL;
        s->src_size = (size_t)file_size;
        s->num_blocks = num_blocks;
        s->has_checksums = has_crc;
        s->block_size = bs;
        s->file_has_checksums = fhc;

        s->comp_sizes = (uint32_t*)calloc(num_blocks, sizeof(uint32_t));
        s->decomp_sizes = (uint32_t*)calloc(num_blocks, sizeof(uint32_t));
        s->comp_offsets = (uint64_t*)calloc((size_t)num_blocks + 1, sizeof(uint64_t));
        s->decomp_offsets = (uint64_t*)calloc((size_t)num_blocks + 1, sizeof(uint64_t));
        if (UNLIKELY(!s->comp_sizes || !s->decomp_sizes || !s->comp_offsets ||
                     !s->decomp_offsets)) {
            zxc_seekable_free(s);
            s = NULL;
            break;
        }

        const uint8_t* ep = bh_ptr + ZXC_BLOCK_HEADER_SIZE;
        uint64_t comp_acc = ZXC_FILE_HEADER_SIZE;
        uint64_t decomp_acc = 0;
        for (uint32_t i = 0; i < num_blocks; i++) {
            s->comp_sizes[i] = zxc_le32(ep);
            ep += sizeof(uint32_t);
            s->decomp_sizes[i] = zxc_le32(ep);
            ep += sizeof(uint32_t);
            if (has_crc) ep += sizeof(uint32_t);

            s->comp_offsets[i] = comp_acc;
            s->decomp_offsets[i] = decomp_acc;
            comp_acc += s->comp_sizes[i];
            decomp_acc += s->decomp_sizes[i];
        }
        s->comp_offsets[num_blocks] = comp_acc;
        s->decomp_offsets[num_blocks] = decomp_acc;
        s->total_decomp = decomp_acc;
    }

    free(tail);
    return s;
}

uint32_t zxc_seekable_get_num_blocks(const zxc_seekable* s) { return s ? s->num_blocks : 0; }

uint64_t zxc_seekable_get_decompressed_size(const zxc_seekable* s) {
    return s ? s->total_decomp : 0;
}

uint32_t zxc_seekable_get_block_comp_size(const zxc_seekable* s, const uint32_t block_idx) {
    if (UNLIKELY(!s || block_idx >= s->num_blocks)) return 0;
    return s->comp_sizes[block_idx];
}

uint32_t zxc_seekable_get_block_decomp_size(const zxc_seekable* s, const uint32_t block_idx) {
    if (UNLIKELY(!s || block_idx >= s->num_blocks)) return 0;
    return s->decomp_sizes[block_idx];
}

/* ========================================================================= */
/*  Random-Access Decompression                                              */
/* ========================================================================= */

/**
 * @brief Binary search: find the block that contains @p offset.
 * decomp_offsets[i] <= offset < decomp_offsets[i+1]
 */
static uint32_t zxc_seek_find_block(const uint64_t* decomp_offsets, const uint32_t num_blocks,
                                    const uint64_t offset) {
    uint32_t lo = 0, hi = num_blocks;
    while (lo < hi) {
        const uint32_t mid = lo + (hi - lo) / 2;
        if (decomp_offsets[mid + 1] <= offset)
            lo = mid + 1;
        else
            hi = mid;
    }
    return lo;
}

/**
 * @brief Reads a compressed block from buffer or file.
 */
static int zxc_seek_read_block(const zxc_seekable* s, const uint32_t block_idx, uint8_t* buf,
                               const size_t buf_cap) {
    const uint64_t off = s->comp_offsets[block_idx];
    const uint32_t csz = s->comp_sizes[block_idx];
    if (UNLIKELY(csz > buf_cap)) return ZXC_ERROR_DST_TOO_SMALL;

    if (s->src) {
        /* Buffer mode */
        if (UNLIKELY(off + csz > s->src_size)) return ZXC_ERROR_SRC_TOO_SMALL;
        ZXC_MEMCPY(buf, s->src + off, csz);
    } else if (s->file) {
        /* File mode */
        if (UNLIKELY(fseeko(s->file, (long long)off, SEEK_SET) != 0 ||
                     fread(buf, 1, csz, s->file) != csz))
            return ZXC_ERROR_IO;
    } else {
        return ZXC_ERROR_NULL_INPUT;
    }
    return (int)csz;
}

int64_t zxc_seekable_decompress_range(zxc_seekable* s, void* dst, const size_t dst_capacity,
                                      const uint64_t offset, const size_t len) {
    if (UNLIKELY(!s || !dst)) return ZXC_ERROR_NULL_INPUT;
    if (UNLIKELY(len == 0)) return 0;
    if (UNLIKELY(dst_capacity < len)) return ZXC_ERROR_DST_TOO_SMALL;
    if (UNLIKELY(offset + len > s->total_decomp)) return ZXC_ERROR_SRC_TOO_SMALL;

    /* Initialize decompression context on first use */
    if (!s->dctx_initialized) {
        if (UNLIKELY(zxc_cctx_init(&s->dctx, s->block_size, 0, 0, 0) != ZXC_OK))
            return ZXC_ERROR_MEMORY;
        s->dctx_initialized = 1;
    }

    /* Ensure work buffer is large enough */
    const size_t work_sz = s->block_size + ZXC_PAD_SIZE;
    if (s->dctx.work_buf_cap < work_sz) {
        free(s->dctx.work_buf);
        s->dctx.work_buf = (uint8_t*)malloc(work_sz);
        if (UNLIKELY(!s->dctx.work_buf)) return ZXC_ERROR_MEMORY;
        s->dctx.work_buf_cap = work_sz;
    }

    /* Find block range */
    const uint32_t blk_start = zxc_seek_find_block(s->decomp_offsets, s->num_blocks, offset);
    const uint32_t blk_end =
        zxc_seek_find_block(s->decomp_offsets, s->num_blocks, offset + len - 1);

    uint8_t* out = (uint8_t*)dst;
    size_t remaining = len;

    /* Allocate read buffer for compressed blocks */
    size_t max_comp = 0;
    for (uint32_t bi = blk_start; bi <= blk_end; bi++) {
        if (s->comp_sizes[bi] > max_comp) max_comp = s->comp_sizes[bi];
    }
    uint8_t* const read_buf = (uint8_t*)malloc(max_comp + ZXC_PAD_SIZE);
    if (UNLIKELY(!read_buf)) return ZXC_ERROR_MEMORY;

    for (uint32_t bi = blk_start; bi <= blk_end; bi++) {
        /* Read compressed block data */
        const int read_res = zxc_seek_read_block(s, bi, read_buf, max_comp + ZXC_PAD_SIZE);
        if (UNLIKELY(read_res < 0)) {
            free(read_buf);
            return read_res;
        }

        /* Decompress the block */
        const int dec_res = zxc_decompress_chunk_wrapper(&s->dctx, read_buf, (size_t)read_res,
                                                         s->dctx.work_buf, work_sz);
        if (UNLIKELY(dec_res < 0)) {
            free(read_buf);
            return dec_res;
        }

        /* Calculate which portion of this block's decompressed data we need */
        const uint64_t blk_decomp_start = s->decomp_offsets[bi];
        const size_t skip = (offset > blk_decomp_start) ? (size_t)(offset - blk_decomp_start) : 0;
        const size_t avail = (size_t)dec_res - skip;
        const size_t copy = (avail < remaining) ? avail : remaining;

        ZXC_MEMCPY(out, s->dctx.work_buf + skip, copy);
        out += copy;
        remaining -= copy;
    }

    free(read_buf);
    return (int64_t)len;
}

void zxc_seekable_free(zxc_seekable* s) {
    if (!s) return;
    if (s->dctx_initialized) zxc_cctx_free(&s->dctx);
    free(s->comp_sizes);
    free(s->decomp_sizes);
    free(s->comp_offsets);
    free(s->decomp_offsets);
    free(s);
}
