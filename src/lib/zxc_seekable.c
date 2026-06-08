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
 * between the EOF block and the file footer.  It records the compressed size
 * of every block (decompressed sizes are derived from the header's block_size),
 * enabling O(1) lookup + O(block_size) decompression for any byte range.
 *
 * On-disk layout of a SEK block:
 *
 *   [Block Header (8B)]   block_type=SEK, block_flags=0, comp_size=N*4
 *   [N x Entry (4B)]      comp_size(u32 LE) per block
 *
 * Detection from end of file:
 *   1. Read file header (first 16 bytes) => block_size
 *   2. Read file footer (last 12 bytes) => total_decompressed_size
 *   3. Derive num_blocks = ceil(total_decomp / block_size)
 *   4. Compute seek block size, read backward to the block header
 *   5. Validate block_type == ZXC_BLOCK_SEK
 */

#include "../../include/zxc_seekable.h"

#include "../../include/zxc_dict.h"
#include "../../include/zxc_error.h"
#include "zxc_internal.h"

/* ========================================================================= */
/*  Platform Threading Layer                                                 */
/* ========================================================================= */

// LCOV_EXCL_START - Windows platform layer, not reachable on POSIX CI
#if defined(_WIN32)
#include <process.h> /* _beginthreadex */
#include <windows.h>

/* Map POSIX threading primitives to Windows equivalents */
typedef HANDLE zxc_thread_t;

typedef struct {
    void* (*func)(void*);
    void* arg;
} zxc_seek_thread_arg_t;

static unsigned __stdcall zxc_seek_thread_entry(void* p) {
    zxc_seek_thread_arg_t* a = (zxc_seek_thread_arg_t*)p;
    void* (*f)(void*) = a->func;
    void* arg = a->arg;
    ZXC_FREE(a);
    f(arg);
    return 0;
}

static int zxc_seek_thread_create(zxc_thread_t* t, void* (*fn)(void*), void* arg) {
    zxc_seek_thread_arg_t* wrapper = ZXC_MALLOC(sizeof(zxc_seek_thread_arg_t));
    if (UNLIKELY(!wrapper)) return ZXC_ERROR_MEMORY;
    wrapper->func = fn;
    wrapper->arg = arg;
    uintptr_t handle = _beginthreadex(NULL, 0, zxc_seek_thread_entry, wrapper, 0, NULL);
    if (UNLIKELY(handle == 0)) {
        ZXC_FREE(wrapper);
        return ZXC_ERROR_MEMORY;
    }
    *t = (HANDLE)handle;
    return 0;
}

static void zxc_seek_thread_join(zxc_thread_t t) {
    WaitForSingleObject(t, INFINITE);
    CloseHandle(t);
}

static int zxc_seek_get_num_procs(void) {
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    return (int)si.dwNumberOfProcessors;
}
// LCOV_EXCL_STOP

#else /* POSIX */
#include <pthread.h>
#include <unistd.h>

typedef pthread_t zxc_thread_t;

static int zxc_seek_thread_create(zxc_thread_t* t, void* (*fn)(void*), void* arg) {
    return pthread_create(t, NULL, fn, arg) == 0 ? 0 : ZXC_ERROR_MEMORY;
}

static void zxc_seek_thread_join(zxc_thread_t t) { pthread_join(t, NULL); }

static int zxc_seek_get_num_procs(void) {
    const long n = sysconf(_SC_NPROCESSORS_ONLN);
    return (n > 0) ? (int)n : 1;
}

#endif /* _WIN32 */

/* ========================================================================= */
/*  Seek Table Writer                                                        */
/* ========================================================================= */

size_t zxc_seek_table_size(const uint32_t num_blocks) {
    return ZXC_BLOCK_HEADER_SIZE + (size_t)num_blocks * ZXC_SEEK_ENTRY_SIZE;
}

int64_t zxc_write_seek_table(uint8_t* dst, const size_t dst_capacity, const uint32_t* comp_sizes,
                             const uint32_t num_blocks) {
    if (UNLIKELY(num_blocks > UINT32_MAX / ZXC_SEEK_ENTRY_SIZE)) return ZXC_ERROR_OVERFLOW;

    const size_t total = zxc_seek_table_size(num_blocks);
    if (UNLIKELY(dst_capacity < total)) return ZXC_ERROR_DST_TOO_SMALL;
    if (UNLIKELY(!dst || !comp_sizes)) return ZXC_ERROR_NULL_INPUT;

    const uint32_t payload_size = num_blocks * ZXC_SEEK_ENTRY_SIZE;

    /* Write standard ZXC block header */
    const zxc_block_header_t bh = {
        .block_type = ZXC_BLOCK_SEK, .block_flags = 0, .reserved = 0, .comp_size = payload_size};
    const int hdr_res = zxc_write_block_header(dst, dst_capacity, &bh);
    if (UNLIKELY(hdr_res < 0)) return hdr_res;
    uint8_t* p = dst + hdr_res;

    /* Write entries: comp_size(4) only */
    for (uint32_t i = 0; i < num_blocks; i++) {
        zxc_store_le32(p, comp_sizes[i]);
        p += sizeof(uint32_t);
    }

    return (int64_t)(p - dst);
}

/* ========================================================================= */
/*  Seekable Reader (Opaque Handle)                                          */
/* ========================================================================= */

struct zxc_seekable_s {
    /* Source - exactly one of {src, reader.read_at} is set.  The FILE*
     * variant (see zxc_seekable_file.c) routes through the reader callback
     * by wrapping pread() in its own ctx; from this struct's perspective it
     * is indistinguishable from any other caller-supplied reader. */
    const uint8_t* src;
    uint64_t src_size;
    zxc_reader_t reader; /* user-supplied callback reader; read_at == NULL when unused */

    /* Heap-allocated reader context owned by the seekable handle, freed in
     * zxc_seekable_free.  Set by thin wrappers (e.g. zxc_seekable_open_file)
     * via zxc_seekable_attach_owned_ctx.  NULL when the caller manages
     * reader.ctx lifetime themselves. */
    void* owned_reader_ctx;

    /* Parsed seek table */
    uint32_t num_blocks;
    uint32_t* comp_sizes;   /* array[num_blocks] */
    uint64_t* comp_offsets; /* prefix-sum: byte offset in compressed file per block */
    uint64_t total_decomp;  /* total decompressed size (from footer) */

    /* File header info - block_size is always a power of 2 in [4KB, 2MB],
     * fits in 21 bits. */
    uint32_t block_size;
    int file_has_checksums;
    uint32_t expected_dict_id; /* dict_id from the file header; 0 = no dictionary */

    /* Reusable decompression context (single-threaded path only) */
    zxc_cctx_t dctx;
    int dctx_initialized;

    /* Dictionary (owned copy, freed in zxc_seekable_free). */
    uint8_t* dict;
    size_t dict_size;
};

/**
 * @brief Parses the seek table from raw bytes at the end of the archive.
 *
 * Detection (backward from end):
 *   1. Read file header => block_size
 *   2. Read file footer => total_decomp_size
 *   3. Derive num_blocks = ceil(total_decomp_size / block_size)
 *   4. Compute expected seek block position, validate block_type == SEK
 *   5. Read comp_sizes; derive decomp_sizes from block_size
 */
static zxc_seekable* zxc_seekable_parse(const uint8_t* data, const size_t data_size) {
    /* Minimum: file_header(16) + eof_block(8) + seek_block_header(8)
     *          + file_footer(12) = 44 */
    const size_t MIN_SEEKABLE_SIZE =
        ZXC_FILE_HEADER_SIZE + ZXC_BLOCK_HEADER_SIZE + ZXC_BLOCK_HEADER_SIZE + ZXC_FILE_FOOTER_SIZE;
    if (UNLIKELY(data_size < MIN_SEEKABLE_SIZE)) return NULL;

    /* Step 1: validate file header => block_size */
    size_t block_size_sz = 0;
    int file_has_chk = 0;
    uint32_t header_dict_id = 0;
    if (UNLIKELY(zxc_read_file_header(data, data_size, &block_size_sz, &file_has_chk,
                                      &header_dict_id) != ZXC_OK))
        return NULL;  // LCOV_EXCL_LINE
    const uint32_t block_size = (uint32_t)block_size_sz;
    if (UNLIKELY(block_size == 0)) return NULL;  // LCOV_EXCL_LINE

    /* Step 2: read total decompressed size from the file footer */
    const uint8_t* const footer_ptr = data + data_size - ZXC_FILE_FOOTER_SIZE;
    const uint64_t total_decomp = zxc_le64(footer_ptr);

    /* A value of 0 means empty file - no seek table */
    if (UNLIKELY(total_decomp == 0)) return NULL;

    /* Step 3: derive num_blocks = ceil(total_decomp / block_size) */
    const uint64_t num_blocks_64 = (total_decomp + block_size - 1) / block_size;
    if (UNLIKELY(num_blocks_64 > UINT32_MAX)) return NULL;
    const uint32_t num_blocks = (uint32_t)num_blocks_64;

    /* Step 4: compute seek block position and validate. */
    const uint64_t entries_total_64 = num_blocks_64 * ZXC_SEEK_ENTRY_SIZE;
    if (UNLIKELY(entries_total_64 > SIZE_MAX - ZXC_BLOCK_HEADER_SIZE)) return NULL;
    const size_t entries_total = (size_t)entries_total_64;
    const size_t seek_block_total = ZXC_BLOCK_HEADER_SIZE + entries_total;
    if (UNLIKELY(seek_block_total + ZXC_FILE_FOOTER_SIZE > data_size)) return NULL;
    const uint8_t* const seek_block_start =
        data + data_size - ZXC_FILE_FOOTER_SIZE - seek_block_total;
    if (UNLIKELY(seek_block_start < data)) return NULL;

    /* Read and validate SEK block header */
    zxc_block_header_t bh;
    if (UNLIKELY(zxc_read_block_header(seek_block_start, seek_block_total, &bh) != ZXC_OK))
        return NULL;
    if (UNLIKELY(bh.block_type != ZXC_BLOCK_SEK)) return NULL;
    if (UNLIKELY(bh.comp_size != (uint32_t)entries_total)) return NULL;

    /* Step 5: allocate handle and parse entries */
    zxc_seekable* const s = (zxc_seekable*)ZXC_CALLOC(1, sizeof(zxc_seekable));
    // LCOV_EXCL_START
    if (UNLIKELY(!s)) return NULL;
    // LCOV_EXCL_STOP

    s->num_blocks = num_blocks;
    s->block_size = block_size;
    s->file_has_checksums = file_has_chk;
    s->expected_dict_id = header_dict_id;
    s->src = data;
    s->src_size = (uint64_t)data_size;

    /* Allocate arrays */
    s->comp_sizes = (uint32_t*)ZXC_CALLOC(num_blocks, sizeof(uint32_t));
    s->comp_offsets = (uint64_t*)ZXC_CALLOC((size_t)num_blocks + 1, sizeof(uint64_t));
    // LCOV_EXCL_START
    if (UNLIKELY(!s->comp_sizes || !s->comp_offsets)) {
        zxc_seekable_free(s);
        return NULL;
    }
    // LCOV_EXCL_STOP
    s->total_decomp = total_decomp;

    /* Parse comp_sizes and build compressed prefix sums.
     * Validate each comp_size against data_size to prevent prefix-sum overflow
     * and out-of-bounds reads during decompression. */
    const uint8_t* ep = seek_block_start + ZXC_BLOCK_HEADER_SIZE;
    uint64_t comp_acc = ZXC_FILE_HEADER_SIZE; /* blocks start after file header */
    for (uint32_t i = 0; i < num_blocks; i++) {
        s->comp_sizes[i] = zxc_le32(ep);
        ep += sizeof(uint32_t);

        /* Reject entries below minimum (block header) or larger than the file */
        if (UNLIKELY(s->comp_sizes[i] < ZXC_BLOCK_HEADER_SIZE ||
                     s->comp_sizes[i] > (uint64_t)data_size)) {
            zxc_seekable_free(s);
            return NULL;
        }
        s->comp_offsets[i] = comp_acc;
        comp_acc += s->comp_sizes[i];
        /* Reject if cumulative offset exceeds file size (inconsistent table) */
        if (UNLIKELY(comp_acc > (uint64_t)data_size)) {
            zxc_seekable_free(s);
            return NULL;
        }
    }
    s->comp_offsets[num_blocks] = comp_acc;

    /* Verify prefix-sum lands exactly at the EOF block position.
     * Expected layout: [header 16][data blocks][EOF 8][SEK block][footer 12]
     * So comp_acc (end of data blocks) + EOF(8) == seek_block_start. */
    const uint64_t expected_eof_offset =
        (uint64_t)(seek_block_start - data) - ZXC_BLOCK_HEADER_SIZE;
    if (UNLIKELY(comp_acc != expected_eof_offset)) {
        zxc_seekable_free(s);
        return NULL;
    }

    /* Validate that an actual EOF block header exists at the computed offset */
    if (UNLIKELY(comp_acc + ZXC_BLOCK_HEADER_SIZE > data_size)) {
        zxc_seekable_free(s);
        return NULL;
    }
    zxc_block_header_t eof_bh;
    if (UNLIKELY(zxc_read_block_header(data + comp_acc, ZXC_BLOCK_HEADER_SIZE, &eof_bh) != ZXC_OK ||
                 eof_bh.block_type != ZXC_BLOCK_EOF)) {
        zxc_seekable_free(s);
        return NULL;
    }

    return s;
}

zxc_seekable* zxc_seekable_open(const void* src, const size_t src_size) {
    if (UNLIKELY(!src || src_size == 0)) return NULL;
    return zxc_seekable_parse((const uint8_t*)src, src_size);
}

/* zxc_seekable_open_file (FILE* variant) lives in zxc_seekable_file.c.  It
 * builds a zxc_reader_t over pread() and delegates to
 * zxc_seekable_open_reader below, keeping this translation unit free of any
 * <stdio.h> dependency. */

zxc_seekable* zxc_seekable_open_reader(const zxc_reader_t* r) {
    if (UNLIKELY(!r || !r->read_at || r->size == 0)) return NULL;

    /* Minimum: file_header(16) + eof_block(8) + seek_block_header(8)
     *          + file_footer(12) = 44 */
    const uint64_t MIN_SEEKABLE_SIZE =
        ZXC_FILE_HEADER_SIZE + ZXC_BLOCK_HEADER_SIZE + ZXC_BLOCK_HEADER_SIZE + ZXC_FILE_FOOTER_SIZE;
    if (UNLIKELY(r->size < MIN_SEEKABLE_SIZE)) return NULL;

    /* Read file header => block_size */
    uint8_t header[ZXC_FILE_HEADER_SIZE];
    if (UNLIKELY(r->read_at(r->ctx, header, ZXC_FILE_HEADER_SIZE, 0) !=
                 (int64_t)ZXC_FILE_HEADER_SIZE))
        return NULL;

    size_t bs_sz = 0;
    int fhc = 0;
    uint32_t header_dict_id = 0;
    if (UNLIKELY(zxc_read_file_header(header, ZXC_FILE_HEADER_SIZE, &bs_sz, &fhc,
                                      &header_dict_id) != ZXC_OK))
        return NULL;  // LCOV_EXCL_LINE
    const uint32_t bs = (uint32_t)bs_sz;
    if (UNLIKELY(bs == 0)) return NULL;

    /* Read footer => total_decomp_size */
    uint8_t footer_buf[ZXC_FILE_FOOTER_SIZE];
    if (UNLIKELY(r->read_at(r->ctx, footer_buf, ZXC_FILE_FOOTER_SIZE,
                            r->size - ZXC_FILE_FOOTER_SIZE) != (int64_t)ZXC_FILE_FOOTER_SIZE))
        return NULL;

    const uint64_t total_decomp = zxc_le64(footer_buf);
    if (UNLIKELY(total_decomp == 0)) return NULL;

    /* Derive num_blocks = ceil(total_decomp / block_size) */
    const uint64_t num_blocks_64 = (total_decomp + bs - 1) / bs;
    if (UNLIKELY(num_blocks_64 > UINT32_MAX)) return NULL;
    const uint32_t num_blocks = (uint32_t)num_blocks_64;

    /* Guard against size_t multiplication overflow */
    const uint64_t entries_total_64 = (uint64_t)num_blocks * ZXC_SEEK_ENTRY_SIZE;
    if (UNLIKELY(entries_total_64 > SIZE_MAX - ZXC_BLOCK_HEADER_SIZE)) return NULL;

    /* Read the full seek block */
    const size_t seek_block_total = ZXC_BLOCK_HEADER_SIZE + (size_t)entries_total_64;
    if (UNLIKELY(seek_block_total + ZXC_FILE_FOOTER_SIZE > r->size)) return NULL;

    uint8_t* const seek_buf = (uint8_t*)ZXC_MALLOC(seek_block_total);
    if (UNLIKELY(!seek_buf)) return NULL;

    const uint64_t seek_offset = r->size - ZXC_FILE_FOOTER_SIZE - (uint64_t)seek_block_total;
    if (UNLIKELY(r->read_at(r->ctx, seek_buf, seek_block_total, seek_offset) !=
                 (int64_t)seek_block_total)) {
        // LCOV_EXCL_START
        ZXC_FREE(seek_buf);
        return NULL;
        // LCOV_EXCL_STOP
    }

    /* Validate SEK block header */
    zxc_block_header_t bh;
    if (UNLIKELY(zxc_read_block_header(seek_buf, seek_block_total, &bh) != ZXC_OK) ||
        bh.block_type != ZXC_BLOCK_SEK || bh.comp_size != (uint32_t)entries_total_64) {
        // LCOV_EXCL_START
        ZXC_FREE(seek_buf);
        return NULL;
        // LCOV_EXCL_STOP
    }

    /* Build seekable handle */
    zxc_seekable* const s = (zxc_seekable*)ZXC_CALLOC(1, sizeof(zxc_seekable));
    if (UNLIKELY(!s)) {
        ZXC_FREE(seek_buf);
        return NULL;
    }

    s->reader = *r;
    s->src = NULL;
    s->src_size = r->size;
    s->num_blocks = num_blocks;
    s->block_size = bs;
    s->file_has_checksums = fhc;
    s->expected_dict_id = header_dict_id;

    s->comp_sizes = (uint32_t*)ZXC_CALLOC(num_blocks, sizeof(uint32_t));
    s->comp_offsets = (uint64_t*)ZXC_CALLOC((size_t)num_blocks + 1, sizeof(uint64_t));
    if (UNLIKELY(!s->comp_sizes || !s->comp_offsets)) {
        // LCOV_EXCL_START
        ZXC_FREE(seek_buf);
        zxc_seekable_free(s);
        return NULL;
        // LCOV_EXCL_STOP
    }
    s->total_decomp = total_decomp;

    /* Parse comp_sizes and build prefix sums; validate against archive size. */
    const uint8_t* ep = seek_buf + ZXC_BLOCK_HEADER_SIZE;
    uint64_t comp_acc = ZXC_FILE_HEADER_SIZE;
    for (uint32_t i = 0; i < num_blocks; i++) {
        s->comp_sizes[i] = zxc_le32(ep);
        ep += sizeof(uint32_t);

        if (UNLIKELY(s->comp_sizes[i] < ZXC_BLOCK_HEADER_SIZE || s->comp_sizes[i] > r->size)) {
            // LCOV_EXCL_START
            ZXC_FREE(seek_buf);
            zxc_seekable_free(s);
            return NULL;
            // LCOV_EXCL_STOP
        }
        s->comp_offsets[i] = comp_acc;
        comp_acc += s->comp_sizes[i];
        if (UNLIKELY(comp_acc > r->size)) {
            // LCOV_EXCL_START
            ZXC_FREE(seek_buf);
            zxc_seekable_free(s);
            return NULL;
            // LCOV_EXCL_STOP
        }
    }
    s->comp_offsets[num_blocks] = comp_acc;

    ZXC_FREE(seek_buf);
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
    const uint64_t start = (uint64_t)block_idx * (uint64_t)s->block_size;
    const uint64_t remaining = s->total_decomp - start;
    return (remaining >= (uint64_t)s->block_size) ? s->block_size : (uint32_t)remaining;
}

/* ========================================================================= */
/*  Random-Access Decompression                                              */
/* ========================================================================= */

/** @brief O(1) block lookup: block_index = offset / block_size. */
static uint32_t zxc_seek_find_block(const uint32_t block_size, const uint64_t offset) {
    return (uint32_t)(offset / (uint64_t)block_size);
}

/** @brief O(1) decompressed offset for block @p idx. */
static uint64_t zxc_seek_decomp_offset(const uint32_t block_size, const uint32_t idx) {
    return (uint64_t)idx * (uint64_t)block_size;
}

/** @brief O(1) decompressed size for block @p idx. */
static uint32_t zxc_seek_decomp_size(const uint32_t block_size, const uint64_t total_decomp,
                                     const uint32_t idx) {
    const uint64_t start = (uint64_t)idx * (uint64_t)block_size;
    const uint64_t remaining = total_decomp - start;
    return (remaining >= (uint64_t)block_size) ? block_size : (uint32_t)remaining;
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
    } else if (s->reader.read_at) {
        /* Caller-supplied reader (also covers the FILE* variant, which
         * provides a pread-backed callback from zxc_seekable_file.c). */
        const int64_t r = s->reader.read_at(s->reader.ctx, buf, csz, off);
        if (UNLIKELY(r != (int64_t)csz)) return (r < 0) ? (int)r : ZXC_ERROR_IO;
    } else {
        return ZXC_ERROR_NULL_INPUT;  // LCOV_EXCL_LINE
    }
    return (int)csz;
}

int64_t zxc_seekable_decompress_range(zxc_seekable* s, void* dst, const size_t dst_capacity,
                                      const uint64_t offset, const size_t len) {
    if (UNLIKELY(len == 0)) return 0;
    if (UNLIKELY(!s || !dst)) return ZXC_ERROR_NULL_INPUT;
    if (UNLIKELY(dst_capacity < len)) return ZXC_ERROR_DST_TOO_SMALL;
    if (UNLIKELY(offset + len > s->total_decomp)) return ZXC_ERROR_SRC_TOO_SMALL;
    if (UNLIKELY(s->expected_dict_id != 0 && (!s->dict || s->dict_size == 0)))
        return ZXC_ERROR_DICT_REQUIRED;

    /* Initialize decompression context on first use */
    if (!s->dctx_initialized) {
        // LCOV_EXCL_START
        if (UNLIKELY(zxc_cctx_init(&s->dctx, (size_t)s->block_size, 0, 0, 0, s->dict_size) !=
                     ZXC_OK))
            return ZXC_ERROR_MEMORY;
        // LCOV_EXCL_STOP
        s->dctx_initialized = 1;
        if (s->dict_size > 0) ZXC_MEMCPY(s->dctx.dict_buffer, s->dict, s->dict_size);
    }
    s->dctx.dict_size = s->dict_size;

    /* work_buf is pre-sized to block_size + ZXC_DECOMPRESS_TAIL_PAD by the
     * matching zxc_cctx_init above. */
    const size_t work_sz = (size_t)s->block_size + ZXC_DECOMPRESS_TAIL_PAD;

    /* Find block range - O(1) division */
    const uint32_t blk_start = zxc_seek_find_block(s->block_size, offset);
    const uint32_t blk_end = zxc_seek_find_block(s->block_size, offset + len - 1);

    uint8_t* out = (uint8_t*)dst;
    size_t remaining = len;

    /* Allocate read buffer for compressed blocks */
    size_t max_comp = 0;
    for (uint32_t bi = blk_start; bi <= blk_end; bi++) {
        if (s->comp_sizes[bi] > max_comp) max_comp = s->comp_sizes[bi];
    }
    uint8_t* const read_buf = (uint8_t*)ZXC_MALLOC(max_comp + ZXC_PAD_SIZE);
    if (UNLIKELY(!read_buf)) return ZXC_ERROR_MEMORY;  // LCOV_EXCL_LINE

    for (uint32_t bi = blk_start; bi <= blk_end; bi++) {
        /* Read compressed block data */
        const int read_res = zxc_seek_read_block(s, bi, read_buf, max_comp + ZXC_PAD_SIZE);
        if (UNLIKELY(read_res < 0)) {
            // LCOV_EXCL_START
            ZXC_FREE(read_buf);
            return read_res;
            // LCOV_EXCL_STOP
        }

        /* Decompress the block: when a dictionary is active, decode into the
         * cctx-owned dict_buffer (which has dict content prepended) so that
         * match copies referencing dictionary bytes resolve naturally. */
        uint8_t* dec_dst =
            s->dctx.dict_buffer ? s->dctx.dict_buffer + s->dict_size : s->dctx.work_buf;
        const int dec_res =
            zxc_decompress_chunk_wrapper(&s->dctx, read_buf, (size_t)read_res, dec_dst, work_sz);
        if (UNLIKELY(dec_res < 0)) {
            // LCOV_EXCL_START
            ZXC_FREE(read_buf);
            return dec_res;
            // LCOV_EXCL_STOP
        }

        /* Calculate which portion of this block's decompressed data we need */
        const uint64_t blk_decomp_start = zxc_seek_decomp_offset(s->block_size, bi);
        const size_t skip = (offset > blk_decomp_start) ? (size_t)(offset - blk_decomp_start) : 0;
        if (UNLIKELY((size_t)dec_res < skip)) {
            // LCOV_EXCL_START
            ZXC_FREE(read_buf);
            return ZXC_ERROR_CORRUPT_DATA;
            // LCOV_EXCL_STOP
        }
        const size_t avail = (size_t)dec_res - skip;
        const size_t copy = (avail < remaining) ? avail : remaining;

        ZXC_MEMCPY(out, dec_dst + skip, copy);
        out += copy;
        remaining -= copy;
    }

    ZXC_FREE(read_buf);
    return (int64_t)len;
}

/* ========================================================================= */
/*  Multi-Threaded Random-Access Decompression (Fork-Join)                   */
/* ========================================================================= */

/**
 * @brief Per-block job descriptor for multi-threaded decompression.
 *
 * Each worker thread receives a pointer to one of these, performs the read +
 * decompress + memcpy sequence, and writes the result code into @c result.
 * The main thread inspects @c result after join.
 */
typedef struct {
    const zxc_seekable* s; /* shared handle (read-only) */
    uint32_t block_idx;    /* block to decompress */
    uint8_t* dst;          /* output pointer within caller's buffer */
    size_t skip;           /* bytes to skip at start of decompressed block */
    size_t copy_len;       /* bytes to copy into dst */
    int result;            /* 0 = OK, < 0 = error */
} zxc_seek_mt_job_t;

/**
 * @brief Thread-safe block read using pread (for file mode) or memcpy (buffer mode).
 */
static int zxc_seek_read_block_mt(const zxc_seekable* s, const uint32_t block_idx, uint8_t* buf,
                                  const size_t buf_cap) {
    const uint64_t off = s->comp_offsets[block_idx];
    const uint32_t csz = s->comp_sizes[block_idx];
    if (UNLIKELY(csz > buf_cap)) return ZXC_ERROR_DST_TOO_SMALL;

    if (s->src) {
        /* Buffer mode - memcpy is inherently thread-safe on const data */
        if (UNLIKELY(off + csz > s->src_size)) return ZXC_ERROR_SRC_TOO_SMALL;
        ZXC_MEMCPY(buf, s->src + off, csz);
    } else if (s->reader.read_at) {
        /* Reader callback - caller-supplied read_at must be thread-safe.
         * The FILE* variant (zxc_seekable_file.c) installs a pread-backed
         * callback that is naturally thread-safe. */
        const int64_t r = s->reader.read_at(s->reader.ctx, buf, csz, off);
        if (UNLIKELY(r != (int64_t)csz)) return (r < 0) ? (int)r : ZXC_ERROR_IO;
    } else {
        return ZXC_ERROR_NULL_INPUT;  // LCOV_EXCL_LINE
    }
    return (int)csz;
}

/**
 * @brief Worker thread entry point for multi-threaded seekable decompression.
 *
 * Each worker:
 *   1. Allocates a thread-local decompression context.
 *   2. Reads the compressed block via pread (thread-safe).
 *   3. Decompresses into a local work buffer.
 *   4. Copies the requested sub-range into the caller's output buffer.
 */
static void* zxc_seek_mt_worker(void* arg) {
    zxc_seek_mt_job_t* const job = (zxc_seek_mt_job_t*)arg;
    const zxc_seekable* const s = job->s;
    const uint32_t bi = job->block_idx;

    /* Thread-local decompression context (mode=0 for decompress-only) */
    zxc_cctx_t dctx;
    // LCOV_EXCL_START
    if (UNLIKELY(zxc_cctx_init(&dctx, (size_t)s->block_size, 0, 0, 0, s->dict_size) != ZXC_OK)) {
        job->result = ZXC_ERROR_MEMORY;
        return NULL;
    }
    // LCOV_EXCL_STOP
    const size_t work_sz = (size_t)s->block_size + ZXC_DECOMPRESS_TAIL_PAD;

    uint8_t* const dict_work = dctx.dict_buffer;
    if (dict_work) ZXC_MEMCPY(dict_work, s->dict, s->dict_size);

    /* Read compressed block */
    const uint32_t csz = s->comp_sizes[bi];
    uint8_t* const read_buf = (uint8_t*)ZXC_MALLOC(csz + ZXC_PAD_SIZE);
    // LCOV_EXCL_START
    if (UNLIKELY(!read_buf)) {
        zxc_cctx_free(&dctx);
        job->result = ZXC_ERROR_MEMORY;
        return NULL;
    }
    // LCOV_EXCL_STOP

    const int read_res = zxc_seek_read_block_mt(s, bi, read_buf, csz + ZXC_PAD_SIZE);
    // LCOV_EXCL_START
    if (UNLIKELY(read_res < 0)) {
        ZXC_FREE(read_buf);
        zxc_cctx_free(&dctx);
        job->result = read_res;
        return NULL;
    }
    // LCOV_EXCL_STOP

    /* Decompress: use dict bounce buffer when dictionary is active */
    uint8_t* dec_dst = dict_work ? dict_work + s->dict_size : dctx.work_buf;
    const int dec_res =
        zxc_decompress_chunk_wrapper(&dctx, read_buf, (size_t)read_res, dec_dst, work_sz);
    ZXC_FREE(read_buf);

    // LCOV_EXCL_START
    if (UNLIKELY(dec_res < 0)) {
        zxc_cctx_free(&dctx);
        job->result = dec_res;
        return NULL;
    }
    if (UNLIKELY((size_t)dec_res < job->skip + job->copy_len)) {
        zxc_cctx_free(&dctx);
        job->result = ZXC_ERROR_CORRUPT_DATA;
        return NULL;
    }
    // LCOV_EXCL_STOP

    /* Copy the requested portion directly into the caller's output buffer */
    ZXC_MEMCPY(job->dst, dec_dst + job->skip, job->copy_len);

    zxc_cctx_free(&dctx);
    job->result = 0;
    return NULL;
}

int64_t zxc_seekable_decompress_range_mt(zxc_seekable* s, void* dst, const size_t dst_capacity,
                                         const uint64_t offset, const size_t len, int n_threads) {
    if (UNLIKELY(len == 0)) return 0;
    if (UNLIKELY(!s || !dst)) return ZXC_ERROR_NULL_INPUT;
    if (UNLIKELY(dst_capacity < len)) return ZXC_ERROR_DST_TOO_SMALL;
    if (UNLIKELY(offset + len > s->total_decomp)) return ZXC_ERROR_SRC_TOO_SMALL;
    if (UNLIKELY(s->expected_dict_id != 0 && (!s->dict || s->dict_size == 0)))
        return ZXC_ERROR_DICT_REQUIRED;

    /* Find block range - O(1) division */
    const uint32_t blk_start = zxc_seek_find_block(s->block_size, offset);
    const uint32_t blk_end = zxc_seek_find_block(s->block_size, offset + len - 1);
    const uint32_t num_jobs = blk_end - blk_start + 1;

    /* Auto-detect thread count (0 = use all available cores) */
    if (n_threads == 0) n_threads = zxc_seek_get_num_procs();

    /* Fallback to single-threaded path for trivial cases */
    if (n_threads <= 1 || num_jobs <= 1) {
        return zxc_seekable_decompress_range(s, dst, dst_capacity, offset, len);
    }

    /* Cap threads to number of blocks and max limit */
    if ((uint32_t)n_threads > num_jobs) n_threads = (int)num_jobs;
    if (n_threads > ZXC_MAX_THREADS) n_threads = ZXC_MAX_THREADS;

    /* Allocate job descriptors */
    zxc_seek_mt_job_t* const jobs =
        (zxc_seek_mt_job_t*)ZXC_CALLOC(num_jobs, sizeof(zxc_seek_mt_job_t));
    if (UNLIKELY(!jobs)) return ZXC_ERROR_MEMORY;  // LCOV_EXCL_LINE

    /* Plan jobs: compute skip, copy_len, and dst pointer for each block */
    uint8_t* out = (uint8_t*)dst;
    size_t remaining = len;
    for (uint32_t i = 0; i < num_jobs; i++) {
        const uint32_t bi = blk_start + i;
        const uint64_t blk_decomp_start = zxc_seek_decomp_offset(s->block_size, bi);
        const size_t skip = (offset > blk_decomp_start) ? (size_t)(offset - blk_decomp_start) : 0;
        const size_t blk_decomp_sz = zxc_seek_decomp_size(s->block_size, s->total_decomp, bi);
        if (UNLIKELY(blk_decomp_sz < skip)) {
            // LCOV_EXCL_START
            ZXC_FREE(jobs);
            return ZXC_ERROR_CORRUPT_DATA;
            // LCOV_EXCL_STOP
        }
        const size_t avail = blk_decomp_sz - skip;
        const size_t copy = (avail < remaining) ? avail : remaining;

        jobs[i].s = s;
        jobs[i].block_idx = bi;
        jobs[i].dst = out;
        jobs[i].skip = skip;
        jobs[i].copy_len = copy;
        jobs[i].result = 0;

        out += copy;
        remaining -= copy;
    }

    /* Launch worker threads (fork phase) */
    zxc_thread_t* const threads =
        (zxc_thread_t*)ZXC_MALLOC((size_t)n_threads * sizeof(zxc_thread_t));
    // LCOV_EXCL_START
    if (UNLIKELY(!threads)) {
        ZXC_FREE(jobs);
        return ZXC_ERROR_MEMORY;
    }
    // LCOV_EXCL_STOP

    /*
     * Distribute jobs across threads round-robin style.
     * If num_jobs > n_threads, some threads handle multiple blocks sequentially.
     * We process jobs in waves: spawn n_threads at a time, join, repeat.
     */
    int error = 0;
    uint32_t job_idx = 0;

    while (job_idx < num_jobs && !error) {
        const int wave_size =
            ((int)(num_jobs - job_idx) < n_threads) ? (int)(num_jobs - job_idx) : n_threads;

        int launched = 0;
        for (int t = 0; t < wave_size; t++) {
            // LCOV_EXCL_START
            if (zxc_seek_thread_create(&threads[t], zxc_seek_mt_worker, &jobs[job_idx + t]) != 0) {
                /* Failed to create thread - mark remaining jobs as errors */
                for (uint32_t j = job_idx + (uint32_t)t; j < num_jobs; j++)
                    jobs[j].result = ZXC_ERROR_MEMORY;
                error = 1;
                break;
            }
            // LCOV_EXCL_STOP
            launched++;
        }

        /* Join phase */
        for (int t = 0; t < launched; t++) {
            zxc_seek_thread_join(threads[t]);
            if (jobs[job_idx + t].result < 0) error = 1;
        }

        job_idx += (uint32_t)launched;
    }

    ZXC_FREE(threads);

    /* Check for errors */
    int64_t result = (int64_t)len;
    if (error) {
        for (uint32_t i = 0; i < num_jobs; i++) {
            if (jobs[i].result < 0) {
                result = (int64_t)jobs[i].result;
                break;
            }
        }
    }

    ZXC_FREE(jobs);
    return result;
}

void zxc_seekable_free(zxc_seekable* s) {
    if (!s) return;
    if (s->dctx_initialized) zxc_cctx_free(&s->dctx);
    ZXC_FREE(s->dict);
    ZXC_FREE(s->comp_sizes);
    ZXC_FREE(s->comp_offsets);
    ZXC_FREE(s->owned_reader_ctx);
    ZXC_FREE(s);
}

int zxc_seekable_set_dict(zxc_seekable* s, const void* dict, const size_t dict_size) {
    if (UNLIKELY(!s || !dict || dict_size == 0)) return ZXC_ERROR_NULL_INPUT;
    if (UNLIKELY(dict_size > ZXC_DICT_SIZE_MAX)) return ZXC_ERROR_DICT_TOO_LARGE;
    if (UNLIKELY(s->expected_dict_id != 0 && zxc_dict_id(dict, dict_size) != s->expected_dict_id))
        return ZXC_ERROR_DICT_MISMATCH;

    ZXC_FREE(s->dict);
    s->dict = NULL;
    s->dict_size = 0;

    s->dict = (uint8_t*)ZXC_MALLOC(dict_size);
    if (UNLIKELY(!s->dict)) return ZXC_ERROR_MEMORY;
    ZXC_MEMCPY(s->dict, dict, dict_size);
    s->dict_size = dict_size;

    /* The [dict | decode] bounce buffer is carved into the dctx workspace.
     * Drop any context built without it (or for a different dict size) so it is
     * re-carved with the new dict on the next decompress. */
    if (s->dctx_initialized) {
        zxc_cctx_free(&s->dctx);
        s->dctx_initialized = 0;
    }
    return ZXC_OK;
}

void zxc_seekable_attach_owned_ctx(zxc_seekable* s, void* ctx) {
    if (s) s->owned_reader_ctx = ctx;
}
