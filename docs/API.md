# ZXC API & ABI Reference

**Library version**: 0.13.1
**SOVERSION**: 4  
**License**: BSD-3-Clause

This document is the authoritative reference for the public API surface and ABI
guarantees of **libzxc**.  It is intended for integrators, packagers, and
language-binding authors.

For usage examples see [`EXAMPLES.md`](EXAMPLES.md).  
For the on-disk binary format see [`FORMAT.md`](FORMAT.md).

---

## Table of Contents

- [1. Headers and Include Graph](#1-headers-and-include-graph)
- [2. Symbol Visibility](#2-symbol-visibility)
- [3. ABI Versioning](#3-abi-versioning)
- [4. Runtime Dependencies](#4-runtime-dependencies)
- [5. Constants and Enumerations](#5-constants-and-enumerations)
- [6. Type Definitions](#6-type-definitions)
- [7. Buffer API](#7-buffer-api)
- [8. Block API](#8-block-api)
- [9. Reusable Context API](#9-reusable-context-api)
- [9b. Static Context API](#9b-static-context-api)
- [10. Streaming API](#10-streaming-api)
- [10b. Push Streaming API](#10b-push-streaming-api)
- [11. Seekable API](#11-seekable-api)
- [11b. Dictionary API](#11b-dictionary-api)
- [12. Error Handling](#12-error-handling)
- [13. Thread Safety](#13-thread-safety)
- [14. Exported Symbols Summary](#14-exported-symbols-summary)

---

## 1. Headers and Include Graph

```text
zxc.h                  <- freestanding umbrella (no <stdio.h>; kernel-safe)
├── zxc_buffer.h       <- Buffer API + Reusable Context API
│   └── zxc_export.h   <- visibility macros
├── zxc_constants.h    <- version macros, compression levels, block sizes, dict sizes
├── zxc_dict.h         <- Dictionary training, save/load, identification
│   └── zxc_export.h
├── zxc_error.h        <- error codes + zxc_error_name()
│   └── zxc_export.h
├── zxc_opts.h         <- compression / decompression options structs
│   └── zxc_export.h
└── zxc_pstream.h      <- Push streaming API (caller-driven, single-thread)
    └── zxc_export.h

opt-in, freestanding-safe (no <stdio.h>):
    zxc_seekable.h     <- Seekable random-access API (storage-agnostic via
                          zxc_reader_t; usable from kernel-space)
        └── zxc_export.h

opt-in, userspace only (pull <stdio.h>; not freestanding-safe):
    zxc_stream.h       <- Multi-threaded FILE*-based streaming + the FILE*
                          open helper for the seekable API
                          (zxc_seekable_open_file)
        └── zxc_export.h
        └── zxc_seekable.h
```

`<zxc.h>` pulls only the minimal freestanding-compatible core (Buffer,
Reusable Context, Push Streaming, one-shot Block APIs) and is therefore
suitable for kernel-space, embedded, or any target where `<stdio.h>` is
unavailable.

`zxc_seekable.h` is **also freestanding-safe**: it provides the
random-access reader API on top of a caller-supplied `zxc_reader_t`
callback. It is opt-in only because most consumers do not need
random-access decompression; it does not pull `<stdio.h>`. Kernel /
embedded consumers can include it directly.

`zxc_stream.h` is the only header that requires `<stdio.h>`. It groups
every `FILE*`-flavored entry point: the multi-threaded streaming driver
(`zxc_stream_compress` / `zxc_stream_decompress`) and the seekable
`FILE*` open helper (`zxc_seekable_open_file`, a thin wrapper that builds
a `pread`/`ReadFile`-backed `zxc_reader_t` and delegates to
`zxc_seekable_open_reader`). Kernel / freestanding builds simply do not
include this header.

---

## 2. Symbol Visibility

libzxc uses an **opt-in** export strategy:

| Build type | How symbols are exposed |
|-----------|------------------------|
| **Shared library** | Default visibility is `hidden`.  Only functions annotated with `ZXC_EXPORT` are exported.  Internal FMV variants (`_default`, `_neon32`, `_avx2`, `_avx512`) are hidden. |
| **Static library** | Define `ZXC_STATIC_DEFINE` (set automatically by CMake) to disable import/export annotations. |

### Macros

| Macro | Purpose |
|-------|---------|
| `ZXC_EXPORT` | Marks a symbol as part of the public API (`__declspec(dllexport/dllimport)` on Windows, `visibility("default")` on GCC/Clang). |
| `ZXC_NO_EXPORT` | Forces a symbol to be hidden (`visibility("hidden")`). |
| `ZXC_DEPRECATED` | Emits a compiler warning when a deprecated symbol is used. |
| `ZXC_STATIC_DEFINE` | Define when building or consuming as a static library. |
| `zxc_lib_EXPORTS` | Set automatically by CMake when building the shared library. Do not define manually. |

---

## 3. ABI Versioning

libzxc follows the shared-library versioning convention:

```
libzxc.so.{SOVERSION}.{MAJOR}.{MINOR}.{PATCH}
```

| Field | Description | Current |
|-------|-------------|---------|
| `SOVERSION` | Bumped on **ABI-breaking** changes (struct layout, removed symbols, changed signatures). | **4** |
| `VERSION` | Tracks the library release. | **0.13.1** |

**Compatibility rule**: any binary compiled against SOVERSION N will load against
any libzxc with the same SOVERSION, regardless of the `VERSION` triple.

### Platform naming

| Platform | Files |
|----------|-------|
| Linux | `libzxc.so` -> `libzxc.so.4` -> `libzxc.so.0.13.1` |
| macOS | `libzxc.dylib` -> `libzxc.4.dylib` -> `libzxc.0.13.1.dylib` |
| Windows | `zxc.dll` + `zxc.lib` (import) |

---

## 4. Runtime Dependencies

libzxc has **zero external dependencies**.

| Dependency | Notes |
|-----------|-------|
| C standard library | `<stdlib.h>`, `<string.h>`, `<stdint.h>`, `<stdio.h>` |
| POSIX threads | `pthread` on Unix/macOS; Windows threads on Win32 |

No dependency on OpenSSL, zlib, or any other compression library.

---

## 5. Constants and Enumerations

### 5.1 Version (compile-time)

Defined in `zxc_constants.h`:

```c
#define ZXC_VERSION_MAJOR     0
#define ZXC_VERSION_MINOR     10
#define ZXC_VERSION_PATCH     0
#define ZXC_LIB_VERSION_STR   "0.13.1"
```

### 5.2 Block Size Constraints

```c
#define ZXC_BLOCK_SIZE_MIN_LOG2  12              // exponent for minimum
#define ZXC_BLOCK_SIZE_MAX_LOG2  21              // exponent for maximum
#define ZXC_BLOCK_SIZE_MIN       (1U << 12)      // 4 KB
#define ZXC_BLOCK_SIZE_MAX       (1U << 21)      // 2 MB
#define ZXC_BLOCK_SIZE_DEFAULT   (512 * 1024)    // 512 KB
```

Block size must be a power of two within `[ZXC_BLOCK_SIZE_MIN, ZXC_BLOCK_SIZE_MAX]`.
Pass `0` to any API to use `ZXC_BLOCK_SIZE_DEFAULT`.

### 5.3 Compression Levels

```c
typedef enum {
    ZXC_LEVEL_FASTEST  = 1,  // Best throughput, lowest ratio
    ZXC_LEVEL_FAST     = 2,  // Fast, good for real-time
    ZXC_LEVEL_DEFAULT  = 3,  // Recommended balance
    ZXC_LEVEL_BALANCED = 4,  // Higher ratio, good speed
    ZXC_LEVEL_COMPACT  = 5,  // High density
    ZXC_LEVEL_DENSITY  = 6,  // Higher density: adds PIVCO-Huffman-coded literals
    ZXC_LEVEL_ULTRA    = 7   // Maximum density: PIVCO-Huffman literals + tokens
} zxc_compression_level_t;
```

Levels 1..5 produce data decompressible at essentially the **same speed**;
levels 6 and 7 add a per-block Huffman decode step — literals at level 6,
literals *and* sequence tokens at level 7 — trading some decode throughput for
denser output (level 7 is the densest and carries the largest decode cost).
Pass `0` for level to use `ZXC_LEVEL_DEFAULT`.

### 5.4 Error Codes

```c
typedef enum {
    ZXC_OK                  =   0,
    ZXC_ERROR_MEMORY        =  -1,  // malloc failure
    ZXC_ERROR_DST_TOO_SMALL =  -2,  // output buffer too small
    ZXC_ERROR_SRC_TOO_SMALL =  -3,  // input truncated
    ZXC_ERROR_BAD_MAGIC     =  -4,  // invalid magic word
    ZXC_ERROR_BAD_VERSION   =  -5,  // unsupported format version
    ZXC_ERROR_BAD_HEADER    =  -6,  // corrupted header (CRC mismatch)
    ZXC_ERROR_BAD_CHECKSUM  =  -7,  // checksum verification failed
    ZXC_ERROR_CORRUPT_DATA  =  -8,  // corrupted compressed data
    ZXC_ERROR_BAD_OFFSET    =  -9,  // invalid match offset
    ZXC_ERROR_OVERFLOW      = -10,  // buffer overflow detected
    ZXC_ERROR_IO            = -11,  // file read/write/seek failure
    ZXC_ERROR_NULL_INPUT    = -12,  // required pointer is NULL
    ZXC_ERROR_BAD_BLOCK_TYPE = -13, // unknown block type
    ZXC_ERROR_BAD_BLOCK_SIZE = -14, // invalid block size
    ZXC_ERROR_DICT_REQUIRED  = -15, // file requires a dictionary but none provided
    ZXC_ERROR_DICT_MISMATCH  = -16, // provided dictionary ID does not match header
    ZXC_ERROR_DICT_TOO_LARGE = -17, // dictionary exceeds ZXC_DICT_SIZE_MAX
    ZXC_ERROR_BAD_LEVEL      = -18  // level unsupported by this context's workspace
                                    // (static context dense-tier raise; out-of-range
                                    // levels are otherwise silently clamped)
} zxc_error_t;
```

All public functions that can fail return negative `zxc_error_t` values on error.

---

## 6. Type Definitions

### 6.1 Options Structs

**Compression options** (defined in `zxc_opts.h`, used by all compression functions):

```c
typedef struct {
    int    n_threads;         // Worker thread count (0 = auto-detect).
    int    level;             // Compression level 1–7 (0 = default).
    size_t block_size;        // Block size in bytes (0 = 512 KB default).
    int    checksum_enabled;  // 1 = enable checksums, 0 = disable.
    int    seekable;          // 1 = append seek table for random access.
    const void* dict;         // Pre-trained dictionary content (NULL = none).
    size_t dict_size;         // Dictionary size in bytes (0 = none, max 64 KB).
    const void* dict_huf;     // Shared literal Huffman table, 128 bytes
                              // (NULL = none; ignored without dict).
    zxc_progress_callback_t progress_cb;  // Optional callback (NULL to disable).
    void*  user_data;                     // Passed through to progress_cb.
} zxc_compress_opts_t;
```

**Decompression options**:

```c
typedef struct {
    int    n_threads;         // Worker thread count (0 = auto-detect).
    int    checksum_enabled;  // 1 = verify checksums, 0 = skip.
    const void* dict;         // Pre-trained dictionary content (NULL = none).
    size_t dict_size;         // Dictionary size in bytes (0 = none).
    const void* dict_huf;     // Shared literal Huffman table, 128 bytes,
                              // matching the one used at compression time
                              // (NULL = none; ignored without dict).
    zxc_progress_callback_t progress_cb;  // Optional callback.
    void*  user_data;                     // Passed through to progress_cb.
} zxc_decompress_opts_t;
```

Both structs are safe to zero-initialize for default behavior.  
Pass `NULL` instead of an options pointer to use all defaults.

### 6.2 Progress Callback

```c
typedef void (*zxc_progress_callback_t)(
    uint64_t    bytes_processed,  // Total input bytes processed so far
    uint64_t    bytes_total,      // Total input bytes (0 if unknown, e.g. stdin)
    const void* user_data         // User-provided context
);
```

Called from the writer thread after each block is processed.
Must be fast and non-blocking.

### 6.3 Opaque Context Types

```c
typedef struct zxc_cctx_s zxc_cctx;  // Opaque compression context
typedef struct zxc_dctx_s zxc_dctx;  // Opaque decompression context
```

Internal layout is hidden. Interact only through `zxc_create_*` / `zxc_free_*` /
`zxc_*_cctx` / `zxc_*_dctx` functions.

---

## 7. Buffer API

Declared in `zxc_buffer.h`. Single-threaded, blocking, in-memory operations.

### Library Info Helpers

Runtime-queryable library metadata. Exposed so integrations and language
bindings can discover the supported level range and library version without
relying on compile-time constants alone.

#### `zxc_min_level`

```c
ZXC_EXPORT int zxc_min_level(void);
```

Returns the minimum supported compression level (currently `1`,
equivalent to `ZXC_LEVEL_FASTEST`).

#### `zxc_max_level`

```c
ZXC_EXPORT int zxc_max_level(void);
```

Returns the maximum supported compression level (currently `7`,
equivalent to `ZXC_LEVEL_ULTRA`).

#### `zxc_default_level`

```c
ZXC_EXPORT int zxc_default_level(void);
```

Returns the default compression level (currently `3`,
equivalent to `ZXC_LEVEL_DEFAULT`).

#### `zxc_version_string`

```c
ZXC_EXPORT const char* zxc_version_string(void);
```

Returns the library version as a null-terminated string (e.g. `"0.13.1"`).
The returned pointer is a compile-time constant and must not be freed.

### `zxc_compress_bound`

```c
ZXC_EXPORT uint64_t zxc_compress_bound(size_t input_size);
```

Returns the worst-case compressed size for `input_size` bytes.
Use to allocate the destination buffer before compression.

### `zxc_compress`

```c
ZXC_EXPORT int64_t zxc_compress(
    const void*                src,
    size_t                     src_size,
    void*                      dst,
    size_t                     dst_capacity,
    const zxc_compress_opts_t* opts       // NULL = defaults
);
```

Compresses `src` into `dst`. Only `level`, `block_size`, `checksum_enabled`, and
`seekable` fields of `opts` are used. `n_threads` is ignored (always single-threaded).

**Returns**: compressed size (> 0) on success, or negative `zxc_error_t`.

### `zxc_decompress`

```c
ZXC_EXPORT int64_t zxc_decompress(
    const void*                  src,
    size_t                       src_size,
    void*                        dst,
    size_t                       dst_capacity,
    const zxc_decompress_opts_t* opts     // NULL = defaults
);
```

Decompresses `src` into `dst`. Only `checksum_enabled` is used.
`src` and `dst` must not overlap (same contract as `memcpy`); for overlapping
single-buffer decode, use `zxc_decompress_inplace` below.

**Returns**: decompressed size (> 0) on success, or negative `zxc_error_t`.

### `zxc_decompress_inplace_bound`

```c
ZXC_EXPORT size_t zxc_decompress_inplace_bound(
    const void* src,
    size_t      src_size
);
```

Reads `src`'s header and footer (no decoding) and returns the single-buffer
size `zxc_decompress_inplace` needs: `decompressed_size` plus the safety
margin `block_size + nblocks x (block header + per-block checksum, if any) +
file footer + ZXC_DECOMPRESS_TAIL_PAD` — one block, the accumulated per-block
framing overhead (incompressible blocks make the compressed stream run that
much longer than the output), the footer, and the wild-copy tail. Always size
the buffer with this function rather than re-deriving the formula.

**Returns**: required buffer size, or `0` if `src` is not a valid archive.

### `zxc_decompress_inplace`

```c
ZXC_EXPORT int64_t zxc_decompress_inplace(
    void*                        buffer,
    size_t                       buffer_capacity,
    size_t                       comp_size,
    const zxc_decompress_opts_t* opts     // NULL = defaults
);
```

Decompresses **in place** inside a single caller-owned buffer, replacing the
usual input + output pair with one allocation — decisive for memory-constrained
targets (embedded, FOTA, firmware). The compressed archive must sit
**flush-right** in `buffer` (its last `comp_size` bytes); decoding runs
left-to-right into `buffer[0..]`. Because ZXC never expands a block, the write
cursor provably never overtakes the flush-right read cursor once
`buffer_capacity >= zxc_decompress_inplace_bound(...)`. Dictionary archives are
supported. An undersized buffer is rejected with `ZXC_ERROR_DST_TOO_SMALL`,
never corruption.

```c
size_t need = zxc_decompress_inplace_bound(archive, archive_size);
uint8_t* buf = malloc(need);
memcpy(buf + (need - archive_size), archive, archive_size);   // flush-right
int64_t n = zxc_decompress_inplace(buf, need, archive_size, NULL);
// buf[0 .. n) now holds the decompressed data
```

**Returns**: decompressed size (> 0), `0` for an empty frame, or negative
`zxc_error_t`.

### `zxc_get_decompressed_size`

```c
ZXC_EXPORT uint64_t zxc_get_decompressed_size(
    const void* src,
    size_t      src_size
);
```

Reads the original size from the file footer without decompressing.

**Returns**: original size, or `0` if the buffer is invalid.

---

## 8. Block API

Declared in `zxc_buffer.h`. Single-block compression and decompression
**without file framing** (no file header, EOF block, or footer). Designed
for filesystem integrations (DwarFS, EROFS, SquashFS) where the caller
manages its own block indexing.

Output format: `block_header (8 B)` + compressed payload + optional `checksum (4 B)`.

### `zxc_compress_block_bound`

```c
ZXC_EXPORT uint64_t zxc_compress_block_bound(size_t input_size);
```

Returns the maximum compressed size for a single block.  
Unlike `zxc_compress_bound()`, this does **not** include file header,
EOF block, or footer overhead.

`input_size` must be in `[0, ZXC_BLOCK_SIZE_MAX]` (the Block API limit).

**Returns**: upper bound in bytes, or `0` if `input_size > ZXC_BLOCK_SIZE_MAX`
or would overflow the computation.

### `zxc_decompress_block_bound`

```c
ZXC_EXPORT uint64_t zxc_decompress_block_bound(size_t uncompressed_size);
```

Returns the minimum `dst_capacity` required by `zxc_decompress_block()` for
a block of `uncompressed_size` bytes. The fast decoder uses speculative
wild-copy writes and needs a small tail pad beyond the declared uncompressed
size: passing exactly `uncompressed_size` as `dst_capacity` forces the slow
tail path and may trigger `ZXC_ERROR_OVERFLOW` on some inputs.

Use this helper to size destination buffers for the fast path. For callers
that genuinely cannot oversize their output buffer, use
`zxc_decompress_block_safe()` instead.

`uncompressed_size` must be in `[0, ZXC_BLOCK_SIZE_MAX]` (the Block API limit).

**Returns**: minimum `dst_capacity` in bytes, or `0` if
`uncompressed_size > ZXC_BLOCK_SIZE_MAX` or would overflow the computation.

### `zxc_compress_block`

```c
ZXC_EXPORT int64_t zxc_compress_block(
    zxc_cctx*                  cctx,
    const void*                src,
    size_t                     src_size,
    void*                      dst,
    size_t                     dst_capacity,
    const zxc_compress_opts_t* opts       // NULL = defaults
);
```

Compresses a single block using a reusable context.  
Only `level`, `block_size`, and `checksum_enabled` fields of `opts` are used.

`src_size` must be in `[1, ZXC_BLOCK_SIZE_MAX]` (2 MiB). For larger payloads,
use the frame API (`zxc_compress`) or streaming API (`zxc_cstream_*`), which
chunk transparently into format-conformant blocks.

**Returns**: compressed block size (> 0) on success, or negative `zxc_error_t`.
Returns `ZXC_ERROR_BAD_BLOCK_SIZE` if `src_size > ZXC_BLOCK_SIZE_MAX`.

### `zxc_decompress_block`

```c
ZXC_EXPORT int64_t zxc_decompress_block(
    zxc_dctx*                    dctx,
    const void*                  src,
    size_t                       src_size,
    void*                        dst,
    size_t                       dst_capacity,
    const zxc_decompress_opts_t* opts     // NULL = defaults
);
```

Decompresses a single block produced by `zxc_compress_block()`.
`dst_capacity` should be at least
`zxc_decompress_block_bound(uncompressed_size)` to enable the fast path, and
**must not exceed** `ZXC_BLOCK_SIZE_MAX + ZXC_DECOMPRESS_TAIL_PAD`. For payloads
produced by the frame or streaming APIs, use `zxc_decompress` instead.
Only `checksum_enabled` is used.

**Returns**: decompressed size (> 0) on success, or negative `zxc_error_t`.
Returns `ZXC_ERROR_BAD_BLOCK_SIZE` if `dst_capacity` exceeds the per-block
limit.

### `zxc_decompress_block_safe`

```c
ZXC_EXPORT int64_t zxc_decompress_block_safe(
    zxc_dctx*                    dctx,
    const void*                  src,
    size_t                       src_size,
    void*                        dst,
    size_t                       dst_capacity,
    const zxc_decompress_opts_t* opts     // NULL = defaults
);
```

Strict-sized variant of `zxc_decompress_block()`. Accepts
`dst_capacity == uncompressed_size` exactly: no tail pad required. Intended
for integrations whose output buffer cannot be oversized (page-aligned
decoding into mapped pages, fixed-size slots in a columnar layout, etc.).

Output is **bit-identical** to `zxc_decompress_block()`. RAW blocks
transparently forward to the fast path; only GLO/GHI blocks use the
strict-tail decoder, which is slightly slower than the wild-copy fast path
(see the performance table in `EXAMPLES.md`).

Strict-tail variant: `dst_capacity` is the exact uncompressed size with no
tail-pad margin, so the upper limit is `ZXC_BLOCK_SIZE_MAX` (not
`MAX+TAIL_PAD` as for `zxc_decompress_block`). Returns
`ZXC_ERROR_BAD_BLOCK_SIZE` if `dst_capacity > ZXC_BLOCK_SIZE_MAX`.

Only `checksum_enabled` is used.

**Returns**: decompressed size (> 0) on success, or negative `zxc_error_t`.

### `zxc_estimate_cctx_size`

```c
ZXC_EXPORT uint64_t zxc_estimate_cctx_size(size_t src_size, int level);
```

Returns an accurate estimate of the peak memory used when compressing a single
block of `src_size` bytes at the given `level` via `zxc_compress_block()`.

The estimate covers all per-chunk working buffers (chain table, literals,
sequence/token/offset/extras buffers) plus the fixed hash tables and the
cache-line alignment padding. At `level >= 6` it also includes the transient
DP scratch (~18 × `src_size` bytes) malloc'd by the price-based optimal parser
for the duration of each block. It scales roughly linearly with `src_size` and
is intended for integrators that need to build an accurate memory budget
(filesystems, embedded devices, sandboxed workloads).

**Returns**: estimated peak cctx memory usage in bytes, or `0` if `src_size == 0`.

---

## 9. Reusable Context API

Declared in `zxc_buffer.h`. Eliminates per-call allocation overhead for
hot-path integrations (filesystem plug-ins, batch processing).

### `zxc_create_cctx`

```c
ZXC_EXPORT zxc_cctx* zxc_create_cctx(const zxc_compress_opts_t* opts);
```

Creates a heap-allocated compression context.  
When `opts` is non-NULL, internal buffers are pre-allocated (eager init).  
When `opts` is NULL, allocation is deferred to first use (lazy init).

**Returns**: context pointer, or `NULL` on allocation failure.

### `zxc_free_cctx`

```c
ZXC_EXPORT void zxc_free_cctx(zxc_cctx* cctx);
```

Frees all resources. Safe to pass `NULL`.

### `zxc_compress_cctx`

```c
ZXC_EXPORT int64_t zxc_compress_cctx(
    zxc_cctx*                  cctx,
    const void*                src,
    size_t                     src_size,
    void*                      dst,
    size_t                     dst_capacity,
    const zxc_compress_opts_t* opts
);
```

Same as `zxc_compress()` but reuses internal buffers from `cctx`.
Automatically re-initializes when `block_size` or `level` changes.

### `zxc_create_dctx`

```c
ZXC_EXPORT zxc_dctx* zxc_create_dctx(void);
```

Creates a heap-allocated decompression context.

### `zxc_free_dctx`

```c
ZXC_EXPORT void zxc_free_dctx(zxc_dctx* dctx);
```

Frees all resources. Safe to pass `NULL`.

### `zxc_decompress_dctx`

```c
ZXC_EXPORT int64_t zxc_decompress_dctx(
    zxc_dctx*                    dctx,
    const void*                  src,
    size_t                       src_size,
    void*                        dst,
    size_t                       dst_capacity,
    const zxc_decompress_opts_t* opts
);
```

Same as `zxc_decompress()` but reuses buffers from `dctx`.

---

## 9b. Static Context API

Declared in `zxc_buffer.h`. Mirrors the Reusable Context API but places the
entire context, opaque handle plus every persistent sub-buffer, inside a
**single buffer allocated and owned by the caller**. This pattern is
mandatory for environments where the library cannot call into the host
allocator on the hot path: Linux kernel filesystems (one workspace per mount,
served via `vmalloc` / `kmalloc` up front), embedded targets without a heap
(`.bss` or stack-allocated workspace), sandboxed runtimes with a fixed
memory budget, etc.

Trade-off vs the dynamic API: the workspace is **pinned** to a single `block_size` at init time;
subsequent compress / decompress calls cannot enlarge the footprint, so a
workload that needs to mix block sizes must size the workspace for the
maximum block size up front.

### Properties

- **Single allocation.** All hash tables, chain table, sequence buffers,
  literal scratch, work buffer, and (at `ZXC_LEVEL_DENSITY`) the optimal-
  parser scratch live inside the caller-supplied buffer. No further
  `ZXC_MALLOC` / `ZXC_ALIGNED_MALLOC` is performed for the lifetime of
  the context.
- **`zxc_free_*ctx` is a no-op** on a static handle. The caller owns the
  workspace.
- **`block_size` is locked.** A subsequent call passing a different
  `block_size` returns `ZXC_ERROR_BAD_BLOCK_SIZE` without re-initialising.
  `level` and `checksum_enabled` may still vary per call (they affect only
  the encoder state, not the buffer layout).
- **Workspace alignment.** Must be at least cache-line (64-byte) aligned.
  Use `posix_memalign(..., 64, ...)`, `aligned_alloc(64, ...)`, the slab
  allocator (kmalloc returns ≥ `ARCH_KMALLOC_MINALIGN`), or a
  `_Alignas(64)` static array.

### `zxc_static_cctx_workspace_size`

```c
ZXC_EXPORT size_t zxc_static_cctx_workspace_size(
    const size_t block_size,
    const int    level
);
```

Returns the exact byte count required by a static compression workspace
for the given `block_size` and `level`. Sum of the opaque `zxc_cctx`
wrapper plus every persistent sub-buffer the library would partition.

**Returns**: workspace size in bytes, or `0` if either argument is invalid
(non-power-of-two `block_size`, out-of-range level, ...).

**Note**: level 6 (`ZXC_LEVEL_DENSITY`) adds the optimal-parser scratch
(~8.125 × `block_size`); levels 1–5 share the same workspace size.

### `zxc_init_static_cctx`

```c
ZXC_EXPORT zxc_cctx* zxc_init_static_cctx(
    void*                       workspace,
    const size_t                workspace_size,
    const zxc_compress_opts_t*  opts
);
```

Initialises a compression context inside a caller-supplied workspace.
`workspace_size` must be at least `zxc_static_cctx_workspace_size` for the
same `block_size` / `level`. `opts` is **required**: `block_size` and
`level` are pinned at init time and must be set explicitly.

The returned handle points **inside** `workspace`; the workspace must
remain valid for the lifetime of the handle. `zxc_free_cctx` is a no-op.

**Returns**: handle pointing inside `workspace`, or `NULL` if the
workspace is too small, the options are invalid, or `workspace` / `opts`
is `NULL`.

### `zxc_static_dctx_workspace_size`

```c
ZXC_EXPORT size_t zxc_static_dctx_workspace_size(
    const size_t block_size
);
```

Returns the exact byte count required by a static decompression workspace
for the given `block_size`. Unlike the compression variant, this size is
**independent of the source archive's level**: `lit_buffer` is always
provisioned worst-case because the decoder cannot predict the per-block
literal encoding (RAW / RLE / HUFFMAN) until it sees each block header.

**Returns**: workspace size in bytes, or `0` if `block_size` is invalid.

### `zxc_init_static_dctx`

```c
ZXC_EXPORT zxc_dctx* zxc_init_static_dctx(
    void*         workspace,
    const size_t  workspace_size,
    const size_t  block_size
);
```

Initialises a decompression context inside a caller-supplied workspace.
`block_size` is **pinned** at init time: feeding the returned handle an
archive whose file header declares a different `block_size` returns
`ZXC_ERROR_BAD_BLOCK_SIZE`.

The returned handle points inside `workspace`; the workspace must remain
valid for the lifetime of the handle. `zxc_free_dctx` is a no-op.

**Returns**: handle pointing inside `workspace`, or `NULL` if the
workspace is too small, `block_size` is invalid, or `workspace` is
`NULL`.

### Typical usage

```c
#include <zxc.h>

#define BLOCK_SZ   (64 * 1024)   /* must match the archive's block_size at decode */
#define LEVEL      ZXC_LEVEL_DEFAULT

/* --- Compression side --- */
size_t cws_sz = zxc_static_cctx_workspace_size(BLOCK_SZ, LEVEL);
void  *cws    = NULL;
posix_memalign(&cws, 64, cws_sz);                  /* or kmalloc / vmalloc / .bss */

zxc_compress_opts_t copts = { .level = LEVEL, .block_size = BLOCK_SZ };
zxc_cctx *cctx = zxc_init_static_cctx(cws, cws_sz, &copts);

for (/* each block */) {
    zxc_compress_cctx(cctx, src, n, dst, cap, NULL);  /* no allocation */
}
zxc_free_cctx(cctx);  /* no-op for static */
free(cws);            /* caller owns the workspace */

/* --- Decompression side --- */
size_t dws_sz = zxc_static_dctx_workspace_size(BLOCK_SZ);
void  *dws    = NULL;
posix_memalign(&dws, 64, dws_sz);

zxc_dctx *dctx = zxc_init_static_dctx(dws, dws_sz, BLOCK_SZ);
zxc_decompress_dctx(dctx, in, in_sz, out, out_cap, NULL);
zxc_free_dctx(dctx);  /* no-op */
free(dws);
```

### Sizing notes

| `block_size` | level 1–5 cctx | level 6 cctx | dctx |
|---|---|---|---|
| 4 KB        | ~280 KB         | ~320 KB       | ~9 KB  |
| 64 KB       | ~410 KB         | ~930 KB       | ~129 KB |
| 512 KB      | ~1.5 MB         | ~5.7 MB       | ~1 MB  |
| 2 MB        | ~5.0 MB         | ~21 MB        | ~4 MB  |

(Exact figures depend on architecture and alignment padding; always query
`zxc_static_*_workspace_size` rather than hard-coding.)

### Discovering `block_size` at decode time

`zxc_init_static_dctx` pins `block_size` at workspace creation, so the caller
must know it *before* calling `init`. Four patterns cover every use case:

1. **Pinned by contract.** Producer and consumer agree on `block_size` at
   deployment time (kernel module, embedded firmware, intra-application
   pipeline). The constant lives in a shared header — no runtime
   discovery needed. This is the canonical static-context use case.

2. **Seekable archive — read the SEK table.** The block size is recoverable
   from the table opened in any backing mode (buffer, `FILE*`, or
   `zxc_reader_t`):

   ```c
   zxc_seekable* s = zxc_seekable_open_reader(&r);  /* parses SEK table only */
   const uint32_t bs = zxc_seekable_get_block_decomp_size(s, 0);
   /* For multi-block archives, block index 0 is always a full block.
    * For a single-block archive, bs equals the total decompressed size. */
   ```

3. **Stream / buffer archive — peek the 16-byte file header.** The block
   size is encoded at offset `0x05` (the *Chunk Size Code*, see
   [FORMAT.md §3](FORMAT.md)). Read the first `ZXC_FILE_HEADER_SIZE`
   bytes, validate the magic word, then decode:

   ```c
   uint8_t hdr[ZXC_FILE_HEADER_SIZE];
   /* fread / read / pread the first 16 bytes of the archive */

   const uint32_t magic = (uint32_t)hdr[0]       | ((uint32_t)hdr[1] << 8)
                        | ((uint32_t)hdr[2] << 16) | ((uint32_t)hdr[3] << 24);
   if (magic != 0x9CB02EF5U) { /* not a ZXC archive */ }

   const uint8_t code = hdr[5];
   size_t block_size;
   if (code >= ZXC_BLOCK_SIZE_MIN_LOG2 && code <= ZXC_BLOCK_SIZE_MAX_LOG2)
       block_size = (size_t)1U << code;       /* 4 KB..2 MB */
   else
       /* invalid archive */;
   ```

   After peeking, rewind the input (or keep the 16-byte prefix) and feed
   the whole archive to `zxc_decompress_dctx` / streaming decoder
   normally — the decoder re-parses the header.

4. **Worst-case sizing fallback.** When the archive's block size cannot be
   pre-fetched (e.g., a one-shot streaming decoder that consumes the
   header in the same pass), size the dctx workspace for
   `ZXC_BLOCK_SIZE_MAX` (2 MB). The handle accepts any conforming
   archive at the cost of over-allocation (~4 MB dctx).

   ```c
   size_t dws_sz = zxc_static_dctx_workspace_size(ZXC_BLOCK_SIZE_MAX);
   ```

   If the workspace pool must stay tight and worst-case sizing is too
   expensive, fall back to the dynamic Context API (`zxc_create_dctx`)
   — its `block_size` is set per call.

---

## 10. Streaming API

Declared in `zxc_stream.h` — the umbrella for every `FILE*`-flavored entry
point (opt-in: **not** included by `<zxc.h>` because `<stdio.h>` would break
freestanding/kernel builds). Hosts the multi-threaded streaming pipeline
described below (reader -> workers -> writer) **and** the seekable `FILE*`
open helper `zxc_seekable_open_file` (documented in §11).

### `zxc_stream_compress`

```c
ZXC_EXPORT int64_t zxc_stream_compress(
    FILE*                      f_in,
    FILE*                      f_out,
    const zxc_compress_opts_t* opts
);
```

Compresses `f_in` -> `f_out` using a parallel pipeline.
All fields of `opts` are used, including `n_threads` and `progress_cb`.

**Returns**: total compressed bytes written, or negative `zxc_error_t`.

### `zxc_stream_decompress`

```c
ZXC_EXPORT int64_t zxc_stream_decompress(
    FILE*                        f_in,
    FILE*                        f_out,
    const zxc_decompress_opts_t* opts
);
```

Decompresses `f_in` -> `f_out` using a parallel pipeline.

**Returns**: total decompressed bytes written, or negative `zxc_error_t`.

### `zxc_stream_get_decompressed_size`

```c
ZXC_EXPORT int64_t zxc_stream_get_decompressed_size(FILE* f_in);
```

Reads the original size from the file footer. File position is restored.

**Returns**: original size, or negative `zxc_error_t`.

---

## 10b. Push Streaming API

Declared in `zxc_pstream.h`. Single-threaded, **caller-driven** streaming,
the inverse of the `FILE*`-based pipeline.  Designed for callback-based
integrations (async runtimes, network protocols) that cannot block on a `FILE*` and need to feed/drain in arbitrary chunks.

The on-disk output is **bit-compatible** with the Buffer / Stream APIs:
files produced by the push API decode with `zxc_decompress()` /
`zxc_stream_decompress()`, and vice-versa.

### Buffer Descriptors

```c
typedef struct {
    const void* src;
    size_t      size;
    size_t      pos;   // advanced by the library
} zxc_inbuf_t;

typedef struct {
    void*  dst;
    size_t size;
    size_t pos;        // advanced by the library
} zxc_outbuf_t;
```

The library reads `[src+pos .. src+size)` and writes `[dst+pos .. dst+size)`,
advancing `pos` by what it consumed/produced.  Buffers are caller-owned.
The whole `[dst+pos .. dst+size)` window is writable scratch: with enough
remaining capacity the decoder decodes blocks straight into it with
speculative (wild-copy) stores, so bytes beyond the final `pos` are
unspecified — never keep live data inside the declared capacity.

### Compression

#### `zxc_cstream_create`

```c
ZXC_EXPORT zxc_cstream* zxc_cstream_create(const zxc_compress_opts_t* opts);
```

Creates a push compression context.  Settings (`level`, `block_size`,
`checksum_enabled`) are copied from `opts` and frozen for the lifetime of
the stream.  `n_threads`, `progress_cb`, `seekable` are ignored on this
path.

**Returns**: context, or `NULL` on allocation failure.

#### `zxc_cstream_free`

```c
ZXC_EXPORT void zxc_cstream_free(zxc_cstream* cs);
```

Releases all resources.  Safe with `NULL`.

#### `zxc_cstream_compress`

```c
ZXC_EXPORT int64_t zxc_cstream_compress(
    zxc_cstream*  cs,
    zxc_outbuf_t* out,
    zxc_inbuf_t*  in
);
```

Pushes input into the stream and drains compressed output:

- emits the file header on the first call;
- copies input into the internal block accumulator;
- compresses one block whenever the accumulator fills, writing it into
  `out` (up to `out->size`);
- returns when `in` is fully consumed *and* no more compressed bytes are
  pending, or when `out` has no room left.

Fully reentrant: if `out` fills mid-block, the next call resumes draining
where it left off.  Safe to call with empty input (drain-only).

**Returns**:
- `0` — `in` fully consumed and no pending output;
- `>0` — bytes still pending in the staging buffer (drain `out` and call again);
- `<0` — `zxc_error_t` (sticky: subsequent calls return the same code).

#### `zxc_cstream_end`

```c
ZXC_EXPORT int64_t zxc_cstream_end(zxc_cstream* cs, zxc_outbuf_t* out);
```

Finalises the stream: compresses any partial last block, emits the EOF
block (8 B) and the file footer (12 B).  **Must be called** to produce a
valid ZXC file.

Reentrant the same way `_compress` is: loop until it returns `0`.

After `_end` returns `0`, the stream is in DONE state and any further call
returns `ZXC_ERROR_NULL_INPUT`.

**Returns**:
- `0` — finalisation complete;
- `>0` — bytes still pending (drain `out` and call again);
- `<0` — `zxc_error_t`.

#### `zxc_cstream_in_size` / `zxc_cstream_out_size`

```c
ZXC_EXPORT size_t zxc_cstream_in_size(const zxc_cstream* cs);
ZXC_EXPORT size_t zxc_cstream_out_size(const zxc_cstream* cs);
```

Suggested buffer sizes for best throughput.  The caller may use any size;
these are purely performance hints.

### Decompression

#### `zxc_dstream_create`

```c
ZXC_EXPORT zxc_dstream* zxc_dstream_create(const zxc_decompress_opts_t* opts);
```

Creates a push decompression context.  Only `checksum_enabled` from `opts`
is honoured (controls whether the global file-level checksum is verified
when the file carries one).

**Returns**: context, or `NULL` on allocation failure.

#### `zxc_dstream_free`

```c
ZXC_EXPORT void zxc_dstream_free(zxc_dstream* ds);
```

Releases all resources.  Safe with `NULL`.

#### `zxc_dstream_decompress`

```c
ZXC_EXPORT int64_t zxc_dstream_decompress(
    zxc_dstream*  ds,
    zxc_outbuf_t* out,
    zxc_inbuf_t*  in
);
```

Drives the parser state machine (file header → blocks → EOF → optional
SEK → footer).  Each call makes as much progress as `in` and `out` allow.

Trailing bytes after the validated footer are silently ignored (the
caller can inspect `in->pos` to detect how many were consumed).

**Returns**:
- `>0` — number of decompressed bytes written into `out` this call;
- `0` — either DONE (use `zxc_dstream_finished` to confirm) or no progress
  possible without more input;
- `<0` — `zxc_error_t` (sticky).

#### `zxc_dstream_finished`

```c
ZXC_EXPORT int zxc_dstream_finished(const zxc_dstream* ds);
```

Returns `1` iff the parser has fully validated the file footer.  Callers
that have finished feeding input should check this to detect truncated
streams: `zxc_dstream_decompress` returning `0` with no output is
ambiguous (DONE vs need-more-input) — `_finished` disambiguates.

#### `zxc_dstream_in_size` / `zxc_dstream_out_size`

```c
ZXC_EXPORT size_t zxc_dstream_in_size(const zxc_dstream* ds);
ZXC_EXPORT size_t zxc_dstream_out_size(const zxc_dstream* ds);
```

Suggested buffer sizes.  Returns `0` if `ds` is `NULL`.  Before the file
header has been parsed, `zxc_dstream_in_size()` may return a default
recommended input size hint (for example `ZXC_BLOCK_SIZE_DEFAULT`),
because the actual block size is not known until it is learned from the
header.

### Threading

Each `zxc_cstream` / `zxc_dstream` is single-threaded: one context, one
thread.  Multiple contexts may be used concurrently from different
threads.

### Compression Example

```c
zxc_compress_opts_t opts = { .level = 3, .checksum_enabled = 1 };
zxc_cstream* cs = zxc_cstream_create(&opts);

uint8_t in_buf[64*1024], out_buf[64*1024];
zxc_outbuf_t out = { out_buf, sizeof out_buf, 0 };

ssize_t n;
while ((n = read_some(in_buf, sizeof in_buf)) > 0) {
    zxc_inbuf_t in = { in_buf, (size_t)n, 0 };
    while (in.pos < in.size) {
        int64_t r = zxc_cstream_compress(cs, &out, &in);
        if (r < 0) goto fatal;
        if (out.pos > 0) { write_to_sink(out_buf, out.pos); out.pos = 0; }
    }
}

int64_t pending;
do {
    pending = zxc_cstream_end(cs, &out);
    if (pending < 0) goto fatal;
    if (out.pos > 0) { write_to_sink(out_buf, out.pos); out.pos = 0; }
} while (pending > 0);

zxc_cstream_free(cs);
```

---

## 11. Seekable API

Declared in `zxc_seekable.h` (opt-in: not included by `<zxc.h>` because most
consumers do not need random-access decompression). The header is
**freestanding-safe** — it does not pull `<stdio.h>` — so kernel / embedded
consumers can include it directly and plug their own storage backend behind
the `zxc_reader_t` callback. The `FILE*` convenience open
(`zxc_seekable_open_file`) lives in `zxc_stream.h` instead. Seekable archives
are produced with `seekable = 1` in `zxc_compress_opts_t`.

### Creating a Seekable Archive

Set `seekable = 1` in `zxc_compress_opts_t`. Works with both the Buffer API
and the Streaming API:

```c
zxc_compress_opts_t opts = { .level = 3, .seekable = 1 };
int64_t csize = zxc_compress(src, src_size, dst, dst_cap, &opts);
```

The resulting archive contains a Seek Table block (SEK) between the EOF block
and the file footer.  Standard decompressors handle seekable archives
transparently, the seek table is skipped during sequential decompression.

### `zxc_seekable_open`

```c
ZXC_EXPORT zxc_seekable* zxc_seekable_open(const void* src, const size_t src_size);
```

Opens a seekable archive from a memory buffer.  The buffer must remain
valid for the lifetime of the handle.

**Returns**: handle on success, or `NULL` if the buffer is not a valid seekable archive.

### `zxc_seekable_open_file`

```c
#include <zxc_stream.h>

ZXC_EXPORT zxc_seekable* zxc_seekable_open_file(FILE* f);
```

Opens a seekable archive from a `FILE*`.  The file must be seekable (not
stdin/pipe).  The file position is saved and restored after parsing.
Internally builds a `zxc_reader_t` over `pread()`/`ReadFile()` and delegates
to `zxc_seekable_open_reader`.

Declared in `zxc_stream.h` (which gathers all `FILE*`-flavored entry points)
rather than `zxc_seekable.h` — that header stays freestanding so it can be
included from kernel-space / freestanding environments.

**Returns**: handle on success, or `NULL` on error.

### `zxc_reader_t`

```c
typedef struct {
    int64_t (*read_at)(void* ctx, void* dst, size_t len, uint64_t offset);
    void*    ctx;
    uint64_t size;   // total size of the compressed archive in bytes
} zxc_reader_t;
```

Storage-agnostic reader interface. Lets the caller plug any backend that
supports positional reads — `mmap`, HTTP `Range:` requests, S3, a custom VFS,
`vfs_read()` in Linux kernel space, etc. — behind the seekable API.

`read_at` returns the number of bytes read (`== len` on success), or a
negative `zxc_error_t` on failure. Short reads are treated as errors.

**Thread safety**: `read_at` MUST be safe to call concurrently from multiple
threads when the resulting handle is used with
`zxc_seekable_decompress_range_mt()`. The single-threaded path makes no
concurrent calls.

**Lifetime**: `ctx` and the backing storage must remain valid until
`zxc_seekable_free()`.

### `zxc_seekable_open_reader`

```c
ZXC_EXPORT zxc_seekable* zxc_seekable_open_reader(const zxc_reader_t* r);
```

Opens a seekable archive through a user-supplied reader. The reader is invoked
to fetch the file header, footer, and seek table at open time (3 reads), then
once per block during decompression. No `FILE*` is involved — this is the
entry point to use for kernel space, networked storage, or any non-POSIX
backend.

**Returns**: handle on success, or `NULL` if `r`/`r->read_at` is `NULL`,
`r->size` is `0`, the archive is not seekable, or any `read_at` call fails.

### `zxc_seekable_get_num_blocks`

```c
ZXC_EXPORT uint32_t zxc_seekable_get_num_blocks(const zxc_seekable* s);
```

Returns the total number of data blocks in the archive.

### `zxc_seekable_get_decompressed_size`

```c
ZXC_EXPORT uint64_t zxc_seekable_get_decompressed_size(const zxc_seekable* s);
```

Returns the total decompressed size of the archive.

### `zxc_seekable_get_block_comp_size`

```c
ZXC_EXPORT uint32_t zxc_seekable_get_block_comp_size(
    const zxc_seekable* s,
    uint32_t            block_idx
);
```

Returns the compressed size (on-disk, including header) of a specific block.

### `zxc_seekable_get_block_decomp_size`

```c
ZXC_EXPORT uint32_t zxc_seekable_get_block_decomp_size(
    const zxc_seekable* s,
    uint32_t            block_idx
);
```

Returns the decompressed size of a specific block.

### `zxc_seekable_decompress_range`

```c
ZXC_EXPORT int64_t zxc_seekable_decompress_range(
    zxc_seekable* s,
    void*         dst,
    size_t        dst_capacity,
    uint64_t      offset,
    size_t        len
);
```

Decompresses `len` bytes starting at byte `offset` in the original
uncompressed data.  Only the blocks overlapping the requested range are read
and decompressed.

**Returns**: `len` on success, or negative `zxc_error_t`.

### `zxc_seekable_decompress_range_mt`

```c
ZXC_EXPORT int64_t zxc_seekable_decompress_range_mt(
    zxc_seekable* s,
    void*         dst,
    size_t        dst_capacity,
    uint64_t      offset,
    size_t        len,
    int           n_threads     // 0 = auto-detect
);
```

Multi-threaded variant.  Each worker thread uses `pread()` (POSIX) or
`ReadFile()` (Windows) for lock-free concurrent I/O.  Falls back to
single-threaded mode when `n_threads <= 1` or the range spans a single block.

**Returns**: `len` on success, or negative `zxc_error_t`.

### `zxc_seekable_free`

```c
ZXC_EXPORT void zxc_seekable_free(zxc_seekable* s);
```

Frees a seekable handle and all associated resources.  Safe to call with `NULL`.

### `zxc_write_seek_table`

```c
ZXC_EXPORT int64_t zxc_write_seek_table(
    uint8_t*        dst,
    size_t          dst_capacity,
    const uint32_t* comp_sizes,
    uint32_t        num_blocks
);
```

Low-level: writes a seek table (block header + entries) to `dst`.

**Returns**: bytes written, or negative `zxc_error_t`.

### `zxc_seek_table_size`

```c
ZXC_EXPORT size_t zxc_seek_table_size(uint32_t num_blocks);
```

Returns the encoded byte size of a seek table for `num_blocks` blocks.

---

## 11b. Dictionary API

Declared in `<zxc_dict.h>`. Provides dictionary training, serialization (`.zxd` format), and identification.

> **The simple path**: most callers only need two functions — `zxc_dict_train()`
> to build a `.zxd` from samples, and `zxc_dict_load()` to unpack one for
> (de)compression. The remaining functions are the lower-level primitives those
> two are built from, exposed for advanced use (raw content-only dictionaries,
> retraining only the table, supplying externally-sourced content).

### `zxc_dict_train`

```c
ZXC_EXPORT int64_t zxc_dict_train(
    const void* const* samples,
    const size_t*       sample_sizes,
    size_t              n_samples,
    void*               zxd_buf,        // receives the .zxd bytes
    size_t              zxd_capacity    // zxc_dict_save_bound(ZXC_DICT_SIZE_MAX) is always safe
);
```

**One-call dictionary creation.** Trains the content, trains the shared literal Huffman table from it, and serializes both into ready-to-write `.zxd` bytes. Hides the (otherwise sequential) train-content → train-table → save pipeline. Returns the number of `.zxd` bytes written, or a negative `zxc_error_t` code.

### `zxc_train_dict`

```c
ZXC_EXPORT int64_t zxc_train_dict(
    const void* const* samples,
    const size_t*       sample_sizes,
    size_t              n_samples,
    void*               dict_buf,
    size_t              dict_capacity    // max ZXC_DICT_SIZE_MAX (64KB - 1)
);
```

Trains a dictionary from a corpus of representative samples. Returns the size of the trained dictionary, or a negative `zxc_error_t` code.

### `zxc_train_dict_huf`

```c
ZXC_EXPORT int zxc_train_dict_huf(
    const void* const* samples,
    const size_t*       sample_sizes,
    size_t              n_samples,
    const void*         dict,        // content from zxc_train_dict
    size_t              dict_size,
    uint8_t*            huf_lengths_out  // ZXC_HUF_TABLE_SIZE (128) bytes
);
```

Trains the **shared literal Huffman table** for an already-trained dictionary. It compresses the samples with `dict` and derives canonical code lengths from the real post-LZ literal distribution. The 128-byte table is required by `zxc_dict_save()` and can be attached at (de)compression time via the `dict_huf` option field. Returns `ZXC_OK` or a negative error code.

### `zxc_dict_id`

```c
ZXC_EXPORT uint32_t zxc_dict_id(const void* dict, size_t dict_size, const void* huf_lengths);
```

Returns the deterministic 32-bit dictionary ID. With `huf_lengths` NULL it
hashes the raw content only — the id of an in-memory content-only dictionary
(buffer API). With a 128-byte table it binds the **(content, table)** pair:
`hash(table, seed = hash(content))` — the value recorded in `.zxd` files and
in archive headers when a shared table is attached. Returns 0 for NULL/empty
content.

### `zxc_dict_save`

```c
ZXC_EXPORT int64_t zxc_dict_save(
    const void* content, size_t content_size,
    const void* huf_lengths,    // 128-byte table from zxc_train_dict_huf (required)
    void* buf, size_t buf_capacity
);
```

Serializes dictionary content **and its shared Huffman table** to the `.zxd` file format. The table is mandatory (pass the 128 bytes from `zxc_train_dict_huf()`); the stored `dict_id` covers both. Use `zxc_dict_save_bound(content_size)` to size `buf`.

### `zxc_dict_load`

```c
ZXC_EXPORT int zxc_dict_load(
    const void* buf, size_t buf_size,
    const void** content_out, size_t* content_size_out,
    const void** huf_out,        // 128-byte shared table; may be NULL
    uint32_t* dict_id_out        // may be NULL
);
```

Validates and parses a `.zxd` file from memory in **one call**. On success, `content_out` and `huf_out` (when non-NULL) both point into the input buffer (zero-copy) — pass them straight to `dict` / `dict_huf` in the options. Returns `ZXC_OK` or a negative error code.

### `zxc_dict_huf`

```c
ZXC_EXPORT const void* zxc_dict_huf(const void* buf, size_t buf_size);
```

Standalone accessor for the 128-byte shared table inside a `.zxd` buffer (zero-copy), or NULL if invalid. Equivalent to the `huf_out` of `zxc_dict_load()`; use it when you only need the table.

Returns a zero-copy pointer to the 128-byte shared Huffman table inside a `.zxd` buffer (valid as long as `buf` is), or NULL if `buf` is not a valid `.zxd` file. Pass this to `zxc_compress_opts_t::dict_huf` / `zxc_decompress_opts_t::dict_huf`.

### `zxc_dict_save_bound`

```c
ZXC_EXPORT size_t zxc_dict_save_bound(size_t content_size);
```

Returns the `.zxd` file size for a given content size (`ZXC_DICT_HEADER_SIZE + content_size + ZXC_HUF_TABLE_SIZE`).

### `zxc_seekable_set_dict`

```c
ZXC_EXPORT int zxc_seekable_set_dict(
    zxc_seekable* s, const void* dict, size_t dict_size,
    const void* dict_huf         // 128-byte table, or NULL
);
```

Attaches a dictionary to a seekable handle for random-access decompression. Pass the shared table as `dict_huf` (the archive's `dict_id` binds the pair); pass NULL for a raw content-only dictionary. Both buffers are copied internally. Must be called before any `zxc_seekable_decompress_range()` call.

---

## 12. Error Handling

### `zxc_error_name`

```c
ZXC_EXPORT const char* zxc_error_name(int code);
```

Returns a constant, null-terminated string for any error code
(e.g. `"ZXC_OK"`, `"ZXC_ERROR_MEMORY"`).  
Returns `"ZXC_UNKNOWN_ERROR"` for unrecognized codes.

### Pattern

All functions that can fail use the same convention:

```c
int64_t result = zxc_compress(src, src_size, dst, dst_cap, &opts);
if (result < 0) {
    fprintf(stderr, "Error: %s\n", zxc_error_name((int)result));
}
```

---

## 13. Thread Safety

| API Layer | Safe to call concurrently? | Notes |
|-----------|---------------------------|-------|
| **Buffer API** | Yes (stateless) | Each call is self-contained.  Multiple threads can compress/decompress simultaneously with independent buffers. |
| **Block API** | Per-context | Uses `zxc_cctx` / `zxc_dctx`: same rule as Context API.  Create one context per thread. |
| **Context API** | Per-context | A single `zxc_cctx` / `zxc_dctx` must not be shared between threads.  Create one context per thread. |
| **Static Context API** | Per-handle | Same per-context rule as the dynamic Context API.  Allocate one workspace per thread; the static handle does not introduce any cross-thread synchronisation. |
| **Streaming API** | Per-call | Each `zxc_stream_*` call manages its own thread pool internally.  Do not call from multiple threads on the same `FILE*`. |
| **Seekable API** | Per-handle | A single `zxc_seekable` handle must not be shared between threads for single-threaded decompression.  Use `zxc_seekable_decompress_range_mt()` for parallel access. |
| `zxc_error_name` | Yes | Returns a pointer to a static string. |

---

## 14. Exported Symbols Summary

The shared library exports **47 symbols** (verified with `nm -gU`):

| # | Symbol | API Layer | Header |
|---|--------|-----------|--------|
| 1 | `zxc_min_level` | Info | `zxc_buffer.h` |
| 2 | `zxc_max_level` | Info | `zxc_buffer.h` |
| 3 | `zxc_default_level` | Info | `zxc_buffer.h` |
| 4 | `zxc_version_string` | Info | `zxc_buffer.h` |
| 5 | `zxc_compress_bound` | Buffer | `zxc_buffer.h` |
| 6 | `zxc_compress` | Buffer | `zxc_buffer.h` |
| 7 | `zxc_decompress` | Buffer | `zxc_buffer.h` |
| 8 | `zxc_get_decompressed_size` | Buffer | `zxc_buffer.h` |
| 9 | `zxc_compress_block_bound` | Block | `zxc_buffer.h` |
| 10 | `zxc_decompress_block_bound` | Block | `zxc_buffer.h` |
| 11 | `zxc_compress_block` | Block | `zxc_buffer.h` |
| 12 | `zxc_decompress_block` | Block | `zxc_buffer.h` |
| 13 | `zxc_decompress_block_safe` | Block | `zxc_buffer.h` |
| 14 | `zxc_estimate_cctx_size` | Block | `zxc_buffer.h` |
| 15 | `zxc_create_cctx` | Context | `zxc_buffer.h` |
| 16 | `zxc_free_cctx` | Context | `zxc_buffer.h` |
| 17 | `zxc_compress_cctx` | Context | `zxc_buffer.h` |
| 18 | `zxc_create_dctx` | Context | `zxc_buffer.h` |
| 19 | `zxc_free_dctx` | Context | `zxc_buffer.h` |
| 20 | `zxc_decompress_dctx` | Context | `zxc_buffer.h` |
| 21 | `zxc_static_cctx_workspace_size` | Static Context | `zxc_buffer.h` |
| 22 | `zxc_init_static_cctx` | Static Context | `zxc_buffer.h` |
| 23 | `zxc_static_dctx_workspace_size` | Static Context | `zxc_buffer.h` |
| 24 | `zxc_init_static_dctx` | Static Context | `zxc_buffer.h` |
| 25 | `zxc_stream_compress` | Streaming | `zxc_stream.h` |
| 26 | `zxc_stream_decompress` | Streaming | `zxc_stream.h` |
| 27 | `zxc_stream_get_decompressed_size` | Streaming | `zxc_stream.h` |
| 28 | `zxc_cstream_create` | Push Streaming | `zxc_pstream.h` |
| 29 | `zxc_cstream_free` | Push Streaming | `zxc_pstream.h` |
| 30 | `zxc_cstream_compress` | Push Streaming | `zxc_pstream.h` |
| 31 | `zxc_cstream_end` | Push Streaming | `zxc_pstream.h` |
| 32 | `zxc_cstream_in_size` | Push Streaming | `zxc_pstream.h` |
| 33 | `zxc_cstream_out_size` | Push Streaming | `zxc_pstream.h` |
| 34 | `zxc_dstream_create` | Push Streaming | `zxc_pstream.h` |
| 35 | `zxc_dstream_free` | Push Streaming | `zxc_pstream.h` |
| 36 | `zxc_dstream_decompress` | Push Streaming | `zxc_pstream.h` |
| 37 | `zxc_dstream_finished` | Push Streaming | `zxc_pstream.h` |
| 38 | `zxc_dstream_in_size` | Push Streaming | `zxc_pstream.h` |
| 39 | `zxc_dstream_out_size` | Push Streaming | `zxc_pstream.h` |
| 40 | `zxc_seekable_open` | Seekable | `zxc_seekable.h` |
| 41 | `zxc_seekable_open_file` | Seekable | `zxc_stream.h` |
| 42 | `zxc_seekable_get_num_blocks` | Seekable | `zxc_seekable.h` |
| 43 | `zxc_seekable_get_decompressed_size` | Seekable | `zxc_seekable.h` |
| 44 | `zxc_seekable_get_block_comp_size` | Seekable | `zxc_seekable.h` |
| 45 | `zxc_seekable_get_block_decomp_size` | Seekable | `zxc_seekable.h` |
| 46 | `zxc_seekable_decompress_range` | Seekable | `zxc_seekable.h` |
| 47 | `zxc_seekable_decompress_range_mt` | Seekable | `zxc_seekable.h` |
| 48 | `zxc_seekable_free` | Seekable | `zxc_seekable.h` |
| 49 | `zxc_write_seek_table` | Seekable | `zxc_seekable.h` |
| 50 | `zxc_seek_table_size` | Seekable | `zxc_seekable.h` |
| 51 | `zxc_error_name` | Error | `zxc_error.h` |
| 52 | `zxc_dict_train` | Dictionary | `zxc_dict.h` |
| 53 | `zxc_train_dict` | Dictionary | `zxc_dict.h` |
| 54 | `zxc_train_dict_huf` | Dictionary | `zxc_dict.h` |
| 55 | `zxc_dict_id` | Dictionary | `zxc_dict.h` |
| 56 | `zxc_dict_save` | Dictionary | `zxc_dict.h` |
| 57 | `zxc_dict_load` | Dictionary | `zxc_dict.h` |
| 58 | `zxc_dict_huf` | Dictionary | `zxc_dict.h` |
| 59 | `zxc_dict_get_id` | Dictionary | `zxc_dict.h` |
| 60 | `zxc_dict_save_bound` | Dictionary | `zxc_dict.h` |
| 61 | `zxc_seekable_set_dict` | Seekable | `zxc_seekable.h` |

No internal symbols leak into the public ABI. FMV dispatch variants
(`_default`, `_neon32`, `_avx2`, `_avx512`) are compiled with
`-fvisibility=hidden` and are not exported.
