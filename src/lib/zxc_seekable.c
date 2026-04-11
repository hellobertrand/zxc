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
 * On-disk layout of a SEK block:
 *
 *   [Block Header (8B)]   block_type=SEK, block_flags=0, comp_size
 *   [N × Entry (8B)]      comp_size(4) + decomp_size(4)
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
/*  Platform Threading & I/O Layer                                           */
/* ========================================================================= */

#if defined(_WIN32)
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
    free(a);
    f(arg);
    return 0;
}

static int zxc_seek_thread_create(zxc_thread_t* t, void* (*fn)(void*), void* arg) {
    zxc_seek_thread_arg_t* wrapper = malloc(sizeof(zxc_seek_thread_arg_t));
    if (UNLIKELY(!wrapper)) return ZXC_ERROR_MEMORY;
    wrapper->func = fn;
    wrapper->arg = arg;
    uintptr_t handle = _beginthreadex(NULL, 0, zxc_seek_thread_entry, wrapper, 0, NULL);
    if (UNLIKELY(handle == 0)) { free(wrapper); return ZXC_ERROR_MEMORY; }
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

/**
 * @brief Thread-safe positional read (Windows).
 *
 * Uses ReadFile + Overlapped to read at a specific offset without moving the
 * file pointer, making it safe for concurrent access from multiple threads.
 */
static int zxc_seek_pread(HANDLE hFile, void* buf, size_t count, uint64_t offset) {
    OVERLAPPED ov;
    ZXC_MEMSET(&ov, 0, sizeof(ov));
    ov.Offset = (DWORD)(offset & 0xFFFFFFFF);
    ov.OffsetHigh = (DWORD)(offset >> 32);
    DWORD bytes_read = 0;
    if (!ReadFile(hFile, buf, (DWORD)count, &bytes_read, &ov)) return ZXC_ERROR_IO;
    return (bytes_read == (DWORD)count) ? (int)count : ZXC_ERROR_IO;
}

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

/**
 * @brief Thread-safe positional read (POSIX).
 *
 * Uses pread() which reads at a given offset without modifying the file
 * descriptor's current position, making it inherently thread-safe.
 */
static int zxc_seek_pread(int fd, void* buf, size_t count, uint64_t offset) {
    const ssize_t r = pread(fd, buf, count, (off_t)offset);
    return (r == (ssize_t)count) ? (int)count : ZXC_ERROR_IO;
}

#endif /* _WIN32 */

/* ========================================================================= */
/*  Seek Table Writer                                                        */
/* ========================================================================= */

size_t zxc_seek_table_size(const uint32_t num_blocks) {
    return ZXC_BLOCK_HEADER_SIZE + (size_t)num_blocks * ZXC_SEEK_ENTRY_SIZE + ZXC_SEEK_TAIL_SIZE;
}

int64_t zxc_write_seek_table(uint8_t* dst, const size_t dst_capacity, const uint32_t* comp_sizes,
                             const uint32_t* decomp_sizes, const uint32_t num_blocks) {
    const size_t total = zxc_seek_table_size(num_blocks);
    if (UNLIKELY(dst_capacity < total)) return ZXC_ERROR_DST_TOO_SMALL;
    if (UNLIKELY(!dst || !comp_sizes || !decomp_sizes)) return ZXC_ERROR_NULL_INPUT;

    const uint32_t payload_size = (uint32_t)(num_blocks * ZXC_SEEK_ENTRY_SIZE + ZXC_SEEK_TAIL_SIZE);

    /* Write standard ZXC block header */
    const zxc_block_header_t bh = {
        .block_type = ZXC_BLOCK_SEK, .block_flags = 0, .reserved = 0, .comp_size = payload_size};
    const int hdr_res = zxc_write_block_header(dst, dst_capacity, &bh);
    if (UNLIKELY(hdr_res < 0)) return hdr_res;
    uint8_t* p = dst + hdr_res;

    /* Write entries: comp_size(4) + decomp_size(4) */
    for (uint32_t i = 0; i < num_blocks; i++) {
        zxc_store_le32(p, comp_sizes[i]);
        p += sizeof(uint32_t);
        zxc_store_le32(p, decomp_sizes[i]);
        p += sizeof(uint32_t);
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

    /* Native file descriptor for thread-safe pread() I/O */
#if defined(_WIN32)
    HANDLE native_handle; /* from GetOSFileHandle or _get_osfhandle */
#else
    int fd; /* from fileno() */
#endif

    /* Parsed seek table */
    uint32_t num_blocks;
    uint32_t* comp_sizes;     /* array[num_blocks] */
    uint32_t* decomp_sizes;   /* array[num_blocks] */
    uint64_t* comp_offsets;   /* prefix-sum: byte offset in compressed file per block */
    uint64_t* decomp_offsets; /* prefix-sum: byte offset in decompressed data per block */
    uint64_t total_decomp;    /* sum of all decomp_sizes */

    /* File header info */
    size_t block_size;
    int file_has_checksums;

    /* Reusable decompression context (single-threaded path only) */
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

    /* Step 3: compute seek block position and validate */
    const size_t entries_total = (size_t)num_blocks * ZXC_SEEK_ENTRY_SIZE;
    const uint8_t* const seek_block_start = tail_ptr - entries_total - ZXC_BLOCK_HEADER_SIZE;
    if (UNLIKELY(seek_block_start < data)) return NULL;

    /* Read block header at the computed position */
    zxc_block_header_t bh;
    const size_t seek_block_total = ZXC_BLOCK_HEADER_SIZE + entries_total + ZXC_SEEK_TAIL_SIZE;
    if (UNLIKELY(zxc_read_block_header(seek_block_start, seek_block_total, &bh) != ZXC_OK))
        return NULL;

    /* Validate block type */
    if (UNLIKELY(bh.block_type != ZXC_BLOCK_SEK)) return NULL;

    /* Validate comp_size consistency */
    const uint32_t expected_payload = (uint32_t)(entries_total + ZXC_SEEK_TAIL_SIZE);
    if (UNLIKELY(bh.comp_size != expected_payload)) return NULL;

    /* Step 4: allocate handle and parse entries */
    zxc_seekable* const s = (zxc_seekable*)calloc(1, sizeof(zxc_seekable));
    if (UNLIKELY(!s)) return NULL;

    s->num_blocks = num_blocks;
    s->block_size = block_size;
    s->file_has_checksums = file_has_chk;
    s->src = data;
    s->src_size = data_size;

    /* Allocate arrays */
    s->comp_sizes = (uint32_t*)calloc(num_blocks, sizeof(uint32_t));
    s->decomp_sizes = (uint32_t*)calloc(num_blocks, sizeof(uint32_t));
    s->comp_offsets = (uint64_t*)calloc((size_t)num_blocks + 1, sizeof(uint64_t));
    s->decomp_offsets = (uint64_t*)calloc((size_t)num_blocks + 1, sizeof(uint64_t));
    if (UNLIKELY(!s->comp_sizes || !s->decomp_sizes || !s->comp_offsets || !s->decomp_offsets)) {
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

    /* For simplicity and correctness: read the file into memory.
     * The seek table parsing needs the file header + the tail section. */
    if ((size_t)file_size <= 64 * 1024 * 1024) {
        /* File <= 64 MB: read it all into memory */
        uint8_t* const full = (uint8_t*)malloc((size_t)file_size);
        if (UNLIKELY(!full)) {
            fseeko(f, saved_pos, SEEK_SET);
            return NULL;
        }
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
#if defined(_WIN32)
            s->native_handle = (HANDLE)_get_osfhandle(_fileno(f));
#else
            s->fd = fileno(f);
#endif
        }
        free(full);
        return s;
    }

    /* Large file: read header + tail separately */
    uint8_t header[ZXC_FILE_HEADER_SIZE];
    if (UNLIKELY(fseeko(f, 0, SEEK_SET) != 0 ||
                 fread(header, 1, ZXC_FILE_HEADER_SIZE, f) != ZXC_FILE_HEADER_SIZE)) {
        fseeko(f, saved_pos, SEEK_SET);
        return NULL;
    }

    size_t bs = 0;
    int fhc = 0;
    if (UNLIKELY(zxc_read_file_header(header, ZXC_FILE_HEADER_SIZE, &bs, &fhc) != ZXC_OK)) {
        fseeko(f, saved_pos, SEEK_SET);
        return NULL;
    }

    /* Read tail: footer(12) + tail(4) to get num_blocks */
    const size_t tail_read = ZXC_FILE_FOOTER_SIZE + ZXC_SEEK_TAIL_SIZE;
    uint8_t tail_buf[16]; /* 12 + 4 = 16 */
    if (UNLIKELY(fseeko(f, file_size - (long long)tail_read, SEEK_SET) != 0 ||
                 fread(tail_buf, 1, tail_read, f) != tail_read)) {
        fseeko(f, saved_pos, SEEK_SET);
        return NULL;
    }

    const uint32_t num_blocks = zxc_le32(tail_buf); /* first 4 bytes = num_blocks tail */
    if (UNLIKELY(num_blocks == 0)) {
        fseeko(f, saved_pos, SEEK_SET);
        return NULL;
    }

    /* Read the full seek block */
    const size_t seek_block_total =
        ZXC_BLOCK_HEADER_SIZE + (size_t)num_blocks * ZXC_SEEK_ENTRY_SIZE + ZXC_SEEK_TAIL_SIZE;
    uint8_t* const seek_buf = (uint8_t*)malloc(seek_block_total);
    if (UNLIKELY(!seek_buf)) {
        fseeko(f, saved_pos, SEEK_SET);
        return NULL;
    }

    const long long seek_offset =
        file_size - (long long)ZXC_FILE_FOOTER_SIZE - (long long)seek_block_total;
    if (UNLIKELY(seek_offset < 0 || fseeko(f, seek_offset, SEEK_SET) != 0 ||
                 fread(seek_buf, 1, seek_block_total, f) != seek_block_total)) {
        free(seek_buf);
        fseeko(f, saved_pos, SEEK_SET);
        return NULL;
    }
    fseeko(f, saved_pos, SEEK_SET);

    /* Validate block header */
    zxc_block_header_t bh;
    if (UNLIKELY(zxc_read_block_header(seek_buf, seek_block_total, &bh) != ZXC_OK) ||
        bh.block_type != ZXC_BLOCK_SEK ||
        bh.comp_size != (uint32_t)((size_t)num_blocks * ZXC_SEEK_ENTRY_SIZE + ZXC_SEEK_TAIL_SIZE)) {
        free(seek_buf);
        return NULL;
    }

    /* Build seekable handle */
    zxc_seekable* const s = (zxc_seekable*)calloc(1, sizeof(zxc_seekable));
    if (UNLIKELY(!s)) {
        free(seek_buf);
        return NULL;
    }

    s->file = f;
    s->src = NULL;
#if defined(_WIN32)
    s->native_handle = (HANDLE)_get_osfhandle(_fileno(f));
#else
    s->fd = fileno(f);
#endif
    s->src_size = (size_t)file_size;
    s->num_blocks = num_blocks;
    s->block_size = bs;
    s->file_has_checksums = fhc;

    s->comp_sizes = (uint32_t*)calloc(num_blocks, sizeof(uint32_t));
    s->decomp_sizes = (uint32_t*)calloc(num_blocks, sizeof(uint32_t));
    s->comp_offsets = (uint64_t*)calloc((size_t)num_blocks + 1, sizeof(uint64_t));
    s->decomp_offsets = (uint64_t*)calloc((size_t)num_blocks + 1, sizeof(uint64_t));
    if (UNLIKELY(!s->comp_sizes || !s->decomp_sizes || !s->comp_offsets || !s->decomp_offsets)) {
        free(seek_buf);
        zxc_seekable_free(s);
        return NULL;
    }

    const uint8_t* ep = seek_buf + ZXC_BLOCK_HEADER_SIZE;
    uint64_t comp_acc = ZXC_FILE_HEADER_SIZE;
    uint64_t decomp_acc = 0;
    for (uint32_t i = 0; i < num_blocks; i++) {
        s->comp_sizes[i] = zxc_le32(ep);
        ep += sizeof(uint32_t);
        s->decomp_sizes[i] = zxc_le32(ep);
        ep += sizeof(uint32_t);

        s->comp_offsets[i] = comp_acc;
        s->decomp_offsets[i] = decomp_acc;
        comp_acc += s->comp_sizes[i];
        decomp_acc += s->decomp_sizes[i];
    }
    s->comp_offsets[num_blocks] = comp_acc;
    s->decomp_offsets[num_blocks] = decomp_acc;
    s->total_decomp = decomp_acc;

    free(seek_buf);
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

/* ========================================================================= */
/*  Multi-Threaded Random-Access Decompression (Fork–Join)                   */
/* ========================================================================= */

/**
 * @brief Per-block job descriptor for multi-threaded decompression.
 *
 * Each worker thread receives a pointer to one of these, performs the read +
 * decompress + memcpy sequence, and writes the result code into @c result.
 * The main thread inspects @c result after join.
 */
typedef struct {
    const zxc_seekable* s;  /* shared handle (read-only) */
    uint32_t block_idx;     /* block to decompress */
    uint8_t* dst;           /* output pointer within caller's buffer */
    size_t skip;            /* bytes to skip at start of decompressed block */
    size_t copy_len;        /* bytes to copy into dst */
    int result;             /* 0 = OK, < 0 = error */
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
        /* Buffer mode — memcpy is inherently thread-safe on const data */
        if (UNLIKELY(off + csz > s->src_size)) return ZXC_ERROR_SRC_TOO_SMALL;
        ZXC_MEMCPY(buf, s->src + off, csz);
    } else if (s->file) {
        /* File mode — use pread for concurrent, lock-free reads */
#if defined(_WIN32)
        const int r = zxc_seek_pread(s->native_handle, buf, csz, off);
#else
        const int r = zxc_seek_pread(s->fd, buf, csz, off);
#endif
        if (UNLIKELY(r < 0)) return r;
    } else {
        return ZXC_ERROR_NULL_INPUT;
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
    if (UNLIKELY(zxc_cctx_init(&dctx, s->block_size, 0, 0, 0) != ZXC_OK)) {
        job->result = ZXC_ERROR_MEMORY;
        return NULL;
    }

    /* Allocate work buffer for decompressed output */
    const size_t work_sz = s->block_size + ZXC_PAD_SIZE;
    dctx.work_buf = (uint8_t*)malloc(work_sz);
    if (UNLIKELY(!dctx.work_buf)) {
        zxc_cctx_free(&dctx);
        job->result = ZXC_ERROR_MEMORY;
        return NULL;
    }
    dctx.work_buf_cap = work_sz;

    /* Read compressed block */
    const uint32_t csz = s->comp_sizes[bi];
    uint8_t* const read_buf = (uint8_t*)malloc(csz + ZXC_PAD_SIZE);
    if (UNLIKELY(!read_buf)) {
        zxc_cctx_free(&dctx);
        job->result = ZXC_ERROR_MEMORY;
        return NULL;
    }

    const int read_res = zxc_seek_read_block_mt(s, bi, read_buf, csz + ZXC_PAD_SIZE);
    if (UNLIKELY(read_res < 0)) {
        free(read_buf);
        zxc_cctx_free(&dctx);
        job->result = read_res;
        return NULL;
    }

    /* Decompress */
    const int dec_res =
        zxc_decompress_chunk_wrapper(&dctx, read_buf, (size_t)read_res, dctx.work_buf, work_sz);
    free(read_buf);

    if (UNLIKELY(dec_res < 0)) {
        zxc_cctx_free(&dctx);
        job->result = dec_res;
        return NULL;
    }

    /* Copy the requested portion directly into the caller's output buffer */
    ZXC_MEMCPY(job->dst, dctx.work_buf + job->skip, job->copy_len);

    zxc_cctx_free(&dctx);
    job->result = 0;
    return NULL;
}

int64_t zxc_seekable_decompress_range_mt(zxc_seekable* s, void* dst, const size_t dst_capacity,
                                         const uint64_t offset, const size_t len,
                                         int n_threads) {
    if (UNLIKELY(!s || !dst)) return ZXC_ERROR_NULL_INPUT;
    if (UNLIKELY(len == 0)) return 0;
    if (UNLIKELY(dst_capacity < len)) return ZXC_ERROR_DST_TOO_SMALL;
    if (UNLIKELY(offset + len > s->total_decomp)) return ZXC_ERROR_SRC_TOO_SMALL;

    /* Find block range */
    const uint32_t blk_start = zxc_seek_find_block(s->decomp_offsets, s->num_blocks, offset);
    const uint32_t blk_end =
        zxc_seek_find_block(s->decomp_offsets, s->num_blocks, offset + len - 1);
    const uint32_t num_jobs = blk_end - blk_start + 1;

    /* Fallback to single-threaded path for trivial cases */
    if (n_threads <= 1 || num_jobs <= 1) {
        return zxc_seekable_decompress_range(s, dst, dst_capacity, offset, len);
    }

    /* Auto-detect thread count */
    if (n_threads == 0) n_threads = zxc_seek_get_num_procs();

    /* Cap threads to number of blocks and max limit */
    if ((uint32_t)n_threads > num_jobs) n_threads = (int)num_jobs;
    if (n_threads > ZXC_MAX_THREADS) n_threads = ZXC_MAX_THREADS;

    /* Allocate job descriptors */
    zxc_seek_mt_job_t* const jobs =
        (zxc_seek_mt_job_t*)calloc(num_jobs, sizeof(zxc_seek_mt_job_t));
    if (UNLIKELY(!jobs)) return ZXC_ERROR_MEMORY;

    /* Plan jobs: compute skip, copy_len, and dst pointer for each block */
    uint8_t* out = (uint8_t*)dst;
    size_t remaining = len;
    for (uint32_t i = 0; i < num_jobs; i++) {
        const uint32_t bi = blk_start + i;
        const uint64_t blk_decomp_start = s->decomp_offsets[bi];
        const size_t skip = (offset > blk_decomp_start) ? (size_t)(offset - blk_decomp_start) : 0;
        const size_t blk_decomp_sz = s->decomp_sizes[bi];
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
    zxc_thread_t* const threads = (zxc_thread_t*)malloc((size_t)n_threads * sizeof(zxc_thread_t));
    if (UNLIKELY(!threads)) {
        free(jobs);
        return ZXC_ERROR_MEMORY;
    }

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
            if (zxc_seek_thread_create(&threads[t], zxc_seek_mt_worker, &jobs[job_idx + t]) != 0) {
                /* Failed to create thread — mark remaining jobs as errors */
                for (uint32_t j = job_idx + (uint32_t)t; j < num_jobs; j++)
                    jobs[j].result = ZXC_ERROR_MEMORY;
                error = 1;
                break;
            }
            launched++;
        }

        /* Join phase */
        for (int t = 0; t < launched; t++) {
            zxc_seek_thread_join(threads[t]);
            if (jobs[job_idx + t].result < 0) error = 1;
        }

        job_idx += (uint32_t)launched;
    }

    free(threads);

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

    free(jobs);
    return result;
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
