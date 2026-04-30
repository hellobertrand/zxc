# ZXC API & ABI Reference

**Library version**: 0.10.0
**SOVERSION**: 3  
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
- [10. Streaming API](#10-streaming-api)
- [10b. Push Streaming API](#10b-push-streaming-api)
- [11. Seekable API](#11-seekable-api)
- [12. Sans-IO API](#12-sans-io-api)
- [13. Error Handling](#13-error-handling)
- [14. Thread Safety](#14-thread-safety)
- [15. Exported Symbols Summary](#15-exported-symbols-summary)

---

## 1. Headers and Include Graph

```text
zxc.h                  <- umbrella header (includes everything below)
├── zxc_buffer.h       <- Buffer API + Reusable Context API
│   ├── zxc_export.h   <- visibility macros
│   └── zxc_stream.h   <- Streaming API + opts structs
│       └── zxc_export.h
├── zxc_constants.h    <- version macros, compression levels, block sizes
├── zxc_error.h        <- error codes + zxc_error_name()
│   └── zxc_export.h
├── zxc_pstream.h      <- Push streaming API (caller-driven, single-thread)
│   ├── zxc_export.h
│   └── zxc_stream.h
└── (not included by zxc.h)
    zxc_seekable.h     <- Seekable random-access API (opt-in)
        └── zxc_export.h
    zxc_sans_io.h      <- Low-level primitives (opt-in)
        └── zxc_export.h
```

Include `<zxc.h>` to access everything except the sans-IO and seekable layers.  
Include `<zxc_seekable.h>` explicitly for random-access decompression.  
Include `<zxc_sans_io.h>` explicitly when building custom drivers.

---

## 2. Symbol Visibility

libzxc uses an **opt-in** export strategy:

| Build type | How symbols are exposed |
|-----------|------------------------|
| **Shared library** | Default visibility is `hidden`.  Only functions annotated with `ZXC_EXPORT` are exported.  Internal FMV variants (`_default`, `_neon`, `_avx2`, `_avx512`) are hidden. |
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
| `SOVERSION` | Bumped on **ABI-breaking** changes (struct layout, removed symbols, changed signatures). | **3** |
| `VERSION` | Tracks the library release. | **0.10.0** |

**Compatibility rule**: any binary compiled against SOVERSION N will load against
any libzxc with the same SOVERSION, regardless of the `VERSION` triple.

### Platform naming

| Platform | Files |
|----------|-------|
| Linux | `libzxc.so` -> `libzxc.so.3` -> `libzxc.so.0.10.0` |
| macOS | `libzxc.dylib` -> `libzxc.3.dylib` -> `libzxc.0.10.0.dylib` |
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
#define ZXC_LIB_VERSION_STR   "0.10.0"
```

### 5.2 Block Size Constraints

```c
#define ZXC_BLOCK_SIZE_MIN_LOG2  12              // exponent for minimum
#define ZXC_BLOCK_SIZE_MAX_LOG2  21              // exponent for maximum
#define ZXC_BLOCK_SIZE_MIN       (1U << 12)      // 4 KB
#define ZXC_BLOCK_SIZE_MAX       (1U << 21)      // 2 MB
#define ZXC_BLOCK_SIZE_DEFAULT   (256 * 1024)    // 256 KB
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
    ZXC_LEVEL_COMPACT  = 5   // Highest density
} zxc_compression_level_t;
```

All levels produce data decompressible at the **same speed**.  
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
    ZXC_ERROR_BAD_BLOCK_SIZE = -14  // invalid block size
} zxc_error_t;
```

All public functions that can fail return negative `zxc_error_t` values on error.

---

## 6. Type Definitions

### 6.1 Options Structs

**Compression options** (defined in `zxc_stream.h`, used by all compression functions):

```c
typedef struct {
    int    n_threads;         // Worker thread count (0 = auto-detect).
    int    level;             // Compression level 1–5 (0 = default).
    size_t block_size;        // Block size in bytes (0 = 256 KB default).
    int    checksum_enabled;  // 1 = enable checksums, 0 = disable.
    int    seekable;          // 1 = append seek table for random access.
    zxc_progress_callback_t progress_cb;  // Optional callback (NULL to disable).
    void*  user_data;                     // Passed through to progress_cb.
} zxc_compress_opts_t;
```

**Decompression options**:

```c
typedef struct {
    int    n_threads;         // Worker thread count (0 = auto-detect).
    int    checksum_enabled;  // 1 = verify checksums, 0 = skip.
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

### 6.4 Sans-IO Types

**Compression context** (public struct, for advanced use only):

```c
typedef struct {
    uint32_t* hash_table;     // LZ77 hash table
    uint16_t* chain_table;    // Collision chain table
    void*     memory_block;   // Single allocation owner
    uint32_t  epoch;          // Lazy hash invalidation counter
    uint32_t* buf_sequences;  // Packed sequence records
    uint8_t*  buf_tokens;     // Token buffer
    uint16_t* buf_offsets;    // Offset buffer
    uint8_t*  buf_extras;     // Extra-length buffer
    uint8_t*  literals;       // Literal bytes
    uint8_t*  lit_buffer;     // Scratch buffer for RLE
    size_t    lit_buffer_cap;
    uint8_t*  work_buf;       // Padded scratch for buffer-API decompression
    size_t    work_buf_cap;
    int       checksum_enabled;
    int       compression_level;
    size_t    chunk_size;     // Effective block size
    uint32_t  offset_bits;    // log2(chunk_size)
    uint32_t  offset_mask;    // (1 << offset_bits) - 1
    uint32_t  max_epoch;      // 1 << (32 - offset_bits)
} zxc_cctx_t;
```

**Block header** (8 bytes on disk):

```c
typedef struct {
    uint8_t  block_type;   // See FORMAT.md §4
    uint8_t  block_flags;
    uint8_t  reserved;
    uint8_t  header_crc;   // 1-byte header CRC
    uint32_t comp_size;    // Compressed payload size (excl. header)
} zxc_block_header_t;
```

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

Returns the maximum supported compression level (currently `5`,
equivalent to `ZXC_LEVEL_COMPACT`).

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

Returns the library version as a null-terminated string (e.g. `"0.10.0"`).
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

**Returns**: decompressed size (> 0) on success, or negative `zxc_error_t`.

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

**Returns**: upper bound in bytes, or `0` on overflow.

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

**Returns**: minimum `dst_capacity` in bytes, or `0` if `uncompressed_size`
would overflow.

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

**Returns**: compressed block size (> 0) on success, or negative `zxc_error_t`.

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
`zxc_decompress_block_bound(uncompressed_size)` to enable the fast path.
Only `checksum_enabled` is used.

**Returns**: decompressed size (> 0) on success, or negative `zxc_error_t`.

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

Output is **bit-identical** to `zxc_decompress_block()`. NUM and RAW blocks
transparently forward to the fast path; only GLO/GHI blocks use the
strict-tail decoder, which is slightly slower than the wild-copy fast path
(see the performance table in `EXAMPLES.md`).

Only `checksum_enabled` is used.

**Returns**: decompressed size (> 0) on success, or negative `zxc_error_t`.

### `zxc_estimate_cctx_size`

```c
ZXC_EXPORT uint64_t zxc_estimate_cctx_size(size_t src_size);
```

Returns an accurate estimate of the memory a compression context reserves
when compressing a single block of `src_size` bytes via `zxc_compress_block()`.

The estimate covers all per-chunk working buffers (chain table, literals,
sequence/token/offset/extras buffers) plus the fixed hash tables and the
cache-line alignment padding. It scales roughly linearly with `src_size`
and is intended for integrators that need to build an accurate memory
budget (filesystems, embedded devices, sandboxed workloads).

**Returns**: estimated cctx memory usage in bytes, or `0` if `src_size == 0`.

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

## 10. Streaming API

Declared in `zxc_stream.h`. Multi-threaded, `FILE*`-based pipeline
(reader -> workers -> writer).

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

Declared in `zxc_pstream.h`. Single-threaded, **caller-driven** streaming —
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

Declared in `zxc_seekable.h` (not included by `zxc.h`: opt-in, optional).
Random-access decompression of seekable archives produced with `seekable = 1`.

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
ZXC_EXPORT zxc_seekable* zxc_seekable_open(const void* src, size_t src_size);
```

Opens a seekable archive from a memory buffer.  The buffer must remain
valid for the lifetime of the handle.

**Returns**: handle on success, or `NULL` if the buffer is not a valid seekable archive.

### `zxc_seekable_open_file`

```c
ZXC_EXPORT zxc_seekable* zxc_seekable_open_file(FILE* f);
```

Opens a seekable archive from a `FILE*`.  The file must be seekable (not
stdin/pipe).  The file position is saved and restored after parsing.

**Returns**: handle on success, or `NULL` on error.

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

## 12. Sans-IO API

Declared in `zxc_sans_io.h` (not included by `zxc.h` - opt-in).
Low-level primitives for building custom compression drivers.

### `zxc_cctx_init`

```c
ZXC_EXPORT int zxc_cctx_init(
    zxc_cctx_t* ctx,
    size_t      chunk_size,
    int         mode,              // 1 = compression, 0 = decompression
    int         level,
    int         checksum_enabled
);
```

Allocates internal buffers sized for `chunk_size`.

**Returns**: `ZXC_OK` or `ZXC_ERROR_MEMORY`.

### `zxc_cctx_free`

```c
ZXC_EXPORT void zxc_cctx_free(zxc_cctx_t* ctx);
```

Frees internal buffers. Does **not** free `ctx` itself.

### `zxc_write_file_header`

```c
ZXC_EXPORT int zxc_write_file_header(
    uint8_t* dst,
    size_t   dst_capacity,
    size_t   chunk_size,
    int      has_checksum
);
```

Writes the 16-byte file header.  
**Returns**: bytes written, or `ZXC_ERROR_DST_TOO_SMALL`.

### `zxc_read_file_header`

```c
ZXC_EXPORT int zxc_read_file_header(
    const uint8_t* src,
    size_t         src_size,
    size_t*        out_block_size,    // optional
    int*           out_has_checksum   // optional
);
```

Validates the file header (magic, version, CRC16).  
**Returns**: `ZXC_OK`, or `ZXC_ERROR_BAD_MAGIC` / `ZXC_ERROR_BAD_VERSION` /
`ZXC_ERROR_SRC_TOO_SMALL`.

### `zxc_write_block_header`

```c
ZXC_EXPORT int zxc_write_block_header(
    uint8_t*                  dst,
    size_t                    dst_capacity,
    const zxc_block_header_t* bh
);
```

Serializes a block header (8 bytes, little-endian).  
**Returns**: bytes written, or `ZXC_ERROR_DST_TOO_SMALL`.

### `zxc_read_block_header`

```c
ZXC_EXPORT int zxc_read_block_header(
    const uint8_t*      src,
    size_t              src_size,
    zxc_block_header_t* bh
);
```

Parses a block header (endianness conversion included).  
**Returns**: `ZXC_OK` or `ZXC_ERROR_SRC_TOO_SMALL`.

### `zxc_write_file_footer`

```c
ZXC_EXPORT int zxc_write_file_footer(
    uint8_t* dst,
    size_t   dst_capacity,
    uint64_t src_size,
    uint32_t global_hash,
    int      checksum_enabled
);
```

Writes the 12-byte footer (original size + optional global hash).  
**Returns**: bytes written, or `ZXC_ERROR_DST_TOO_SMALL`.

---

## 13. Error Handling

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

## 14. Thread Safety

| API Layer | Safe to call concurrently? | Notes |
|-----------|---------------------------|-------|
| **Buffer API** | Yes (stateless) | Each call is self-contained.  Multiple threads can compress/decompress simultaneously with independent buffers. |
| **Block API** | Per-context | Uses `zxc_cctx` / `zxc_dctx`: same rule as Context API.  Create one context per thread. |
| **Context API** | Per-context | A single `zxc_cctx` / `zxc_dctx` must not be shared between threads.  Create one context per thread. |
| **Streaming API** | Per-call | Each `zxc_stream_*` call manages its own thread pool internally.  Do not call from multiple threads on the same `FILE*`. |
| **Seekable API** | Per-handle | A single `zxc_seekable` handle must not be shared between threads for single-threaded decompression.  Use `zxc_seekable_decompress_range_mt()` for parallel access. |
| **Sans-IO API** | Per-context | Same rule as context API: one `zxc_cctx_t` per thread. |
| `zxc_error_name` | Yes | Returns a pointer to a static string. |

---

## 15. Exported Symbols Summary

The shared library exports exactly **54 symbols** (verified with `nm -gU`):

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
| 21 | `zxc_stream_compress` | Streaming | `zxc_stream.h` |
| 22 | `zxc_stream_decompress` | Streaming | `zxc_stream.h` |
| 23 | `zxc_stream_get_decompressed_size` | Streaming | `zxc_stream.h` |
| 24 | `zxc_cstream_create` | Push Streaming | `zxc_pstream.h` |
| 25 | `zxc_cstream_free` | Push Streaming | `zxc_pstream.h` |
| 26 | `zxc_cstream_compress` | Push Streaming | `zxc_pstream.h` |
| 27 | `zxc_cstream_end` | Push Streaming | `zxc_pstream.h` |
| 28 | `zxc_cstream_in_size` | Push Streaming | `zxc_pstream.h` |
| 29 | `zxc_cstream_out_size` | Push Streaming | `zxc_pstream.h` |
| 30 | `zxc_dstream_create` | Push Streaming | `zxc_pstream.h` |
| 31 | `zxc_dstream_free` | Push Streaming | `zxc_pstream.h` |
| 32 | `zxc_dstream_decompress` | Push Streaming | `zxc_pstream.h` |
| 33 | `zxc_dstream_finished` | Push Streaming | `zxc_pstream.h` |
| 34 | `zxc_dstream_in_size` | Push Streaming | `zxc_pstream.h` |
| 35 | `zxc_dstream_out_size` | Push Streaming | `zxc_pstream.h` |
| 36 | `zxc_seekable_open` | Seekable | `zxc_seekable.h` |
| 37 | `zxc_seekable_open_file` | Seekable | `zxc_seekable.h` |
| 38 | `zxc_seekable_get_num_blocks` | Seekable | `zxc_seekable.h` |
| 39 | `zxc_seekable_get_decompressed_size` | Seekable | `zxc_seekable.h` |
| 40 | `zxc_seekable_get_block_comp_size` | Seekable | `zxc_seekable.h` |
| 41 | `zxc_seekable_get_block_decomp_size` | Seekable | `zxc_seekable.h` |
| 42 | `zxc_seekable_decompress_range` | Seekable | `zxc_seekable.h` |
| 43 | `zxc_seekable_decompress_range_mt` | Seekable | `zxc_seekable.h` |
| 44 | `zxc_seekable_free` | Seekable | `zxc_seekable.h` |
| 45 | `zxc_write_seek_table` | Seekable | `zxc_seekable.h` |
| 46 | `zxc_seek_table_size` | Seekable | `zxc_seekable.h` |
| 47 | `zxc_cctx_init` | Sans-IO | `zxc_sans_io.h` |
| 48 | `zxc_cctx_free` | Sans-IO | `zxc_sans_io.h` |
| 49 | `zxc_write_file_header` | Sans-IO | `zxc_sans_io.h` |
| 50 | `zxc_read_file_header` | Sans-IO | `zxc_sans_io.h` |
| 51 | `zxc_write_block_header` | Sans-IO | `zxc_sans_io.h` |
| 52 | `zxc_read_block_header` | Sans-IO | `zxc_sans_io.h` |
| 53 | `zxc_write_file_footer` | Sans-IO | `zxc_sans_io.h` |
| 54 | `zxc_error_name` | Error | `zxc_error.h` |

No internal symbols leak into the public ABI. FMV dispatch variants
(`_default`, `_neon`, `_avx2`, `_avx512`) are compiled with
`-fvisibility=hidden` and are not exported.
