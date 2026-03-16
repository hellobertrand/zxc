# ZXC API & ABI Reference

**Library version**: 0.9.1
**SOVERSION**: 2  
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
- [8. Reusable Context API](#8-reusable-context-api)
- [9. Streaming API](#9-streaming-api)
- [10. Sans-IO API](#10-sans-io-api)
- [11. Error Handling](#11-error-handling)
- [12. Thread Safety](#12-thread-safety)
- [13. Exported Symbols Summary](#13-exported-symbols-summary)

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
└── (not included by zxc.h)
    zxc_sans_io.h      <- Low-level primitives (opt-in)
        └── zxc_export.h
```

Include `<zxc.h>` to access everything except the sans-IO layer.  
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
| `SOVERSION` | Bumped on **ABI-breaking** changes (struct layout, removed symbols, changed signatures). | **2** |
| `VERSION` | Tracks the library release. | **0.9.1** |

**Compatibility rule**: any binary compiled against SOVERSION N will load against
any libzxc with the same SOVERSION, regardless of the `VERSION` triple.

### Platform naming

| Platform | Files |
|----------|-------|
| Linux | `libzxc.so` → `libzxc.so.2` → `libzxc.so.0.9.1` |
| macOS | `libzxc.dylib` → `libzxc.2.dylib` → `libzxc.0.9.1.dylib` |
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
#define ZXC_VERSION_MINOR     9
#define ZXC_VERSION_PATCH     1
#define ZXC_LIB_VERSION_STR   "0.9.1"
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

Compresses `src` into `dst`. Only `level`, `block_size`, and `checksum_enabled`
fields of `opts` are used. `n_threads` is ignored (always single-threaded).

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

## 8. Reusable Context API

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

## 9. Streaming API

Declared in `zxc_stream.h`. Multi-threaded, `FILE*`-based pipeline
(reader → workers → writer).

### `zxc_stream_compress`

```c
ZXC_EXPORT int64_t zxc_stream_compress(
    FILE*                      f_in,
    FILE*                      f_out,
    const zxc_compress_opts_t* opts
);
```

Compresses `f_in` → `f_out` using a parallel pipeline.
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

Decompresses `f_in` → `f_out` using a parallel pipeline.

**Returns**: total decompressed bytes written, or negative `zxc_error_t`.

### `zxc_stream_get_decompressed_size`

```c
ZXC_EXPORT int64_t zxc_stream_get_decompressed_size(FILE* f_in);
```

Reads the original size from the file footer. File position is restored.

**Returns**: original size, or negative `zxc_error_t`.

---

## 10. Sans-IO API

Declared in `zxc_sans_io.h` (not included by `zxc.h` — opt-in).
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

## 11. Error Handling

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

## 12. Thread Safety

| API Layer | Safe to call concurrently? | Notes |
|-----------|---------------------------|-------|
| **Buffer API** | Yes (stateless) | Each call is self-contained.  Multiple threads can compress/decompress simultaneously with independent buffers. |
| **Context API** | Per-context | A single `zxc_cctx` / `zxc_dctx` must not be shared between threads.  Create one context per thread. |
| **Streaming API** | Per-call | Each `zxc_stream_*` call manages its own thread pool internally.  Do not call from multiple threads on the same `FILE*`. |
| **Sans-IO API** | Per-context | Same rule as context API — one `zxc_cctx_t` per thread. |
| `zxc_error_name` | Yes | Returns a pointer to a static string. |

---

## 13. Exported Symbols Summary

The shared library exports exactly **21 symbols** (verified with `nm -gU`):

| # | Symbol | API Layer | Header |
|---|--------|-----------|--------|
| 1 | `zxc_compress_bound` | Buffer | `zxc_buffer.h` |
| 2 | `zxc_compress` | Buffer | `zxc_buffer.h` |
| 3 | `zxc_decompress` | Buffer | `zxc_buffer.h` |
| 4 | `zxc_get_decompressed_size` | Buffer | `zxc_buffer.h` |
| 5 | `zxc_create_cctx` | Context | `zxc_buffer.h` |
| 6 | `zxc_free_cctx` | Context | `zxc_buffer.h` |
| 7 | `zxc_compress_cctx` | Context | `zxc_buffer.h` |
| 8 | `zxc_create_dctx` | Context | `zxc_buffer.h` |
| 9 | `zxc_free_dctx` | Context | `zxc_buffer.h` |
| 10 | `zxc_decompress_dctx` | Context | `zxc_buffer.h` |
| 11 | `zxc_stream_compress` | Streaming | `zxc_stream.h` |
| 12 | `zxc_stream_decompress` | Streaming | `zxc_stream.h` |
| 13 | `zxc_stream_get_decompressed_size` | Streaming | `zxc_stream.h` |
| 14 | `zxc_cctx_init` | Sans-IO | `zxc_sans_io.h` |
| 15 | `zxc_cctx_free` | Sans-IO | `zxc_sans_io.h` |
| 16 | `zxc_write_file_header` | Sans-IO | `zxc_sans_io.h` |
| 17 | `zxc_read_file_header` | Sans-IO | `zxc_sans_io.h` |
| 18 | `zxc_write_block_header` | Sans-IO | `zxc_sans_io.h` |
| 19 | `zxc_read_block_header` | Sans-IO | `zxc_sans_io.h` |
| 20 | `zxc_write_file_footer` | Sans-IO | `zxc_sans_io.h` |
| 21 | `zxc_error_name` | Error | `zxc_error.h` |

No internal symbols leak into the public ABI. FMV dispatch variants
(`_default`, `_neon`, `_avx2`, `_avx512`) are compiled with
`-fvisibility=hidden` and are not exported.
