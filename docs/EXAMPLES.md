# ZXC API Examples

This document provides complete, working examples for using the ZXC compression library in C.

## Table of Contents
- [Buffer API (In-Memory)](#buffer-api-in-memory)
- [Stream API (Multi-Threaded)](#stream-api-multi-threaded)
- [Reusable Context API](#reusable-context-api)
- [Seekable Reader (Custom Storage Backend)](#seekable-reader-custom-storage-backend)
- [Compressing Numeric Data (Pre-Filters)](#compressing-numeric-data-pre-filters)
- [Using a Pre-Trained Dictionary](#using-a-pre-trained-dictionary)
- [Meson Integration](#meson-integration)
- [Language Bindings](#language-bindings)

---

## Buffer API (In-Memory)

Ideal for small assets or simple integrations. Thread-safe and ready for highly concurrent environments (Go routines, Node.js workers, Python threads).

```c
#include "zxc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(void) {
    // Original data to compress
    const char* original = "Hello, ZXC! This is a sample text for compression.";
    size_t original_size = strlen(original) + 1;  // Include null terminator

    // Step 1: Calculate maximum compressed size
    uint64_t max_compressed_size = zxc_compress_bound(original_size);

    // Step 2: Allocate buffers
    void* compressed   = malloc(max_compressed_size);
    void* decompressed = malloc(original_size);

    if (!compressed || !decompressed) {
        fprintf(stderr, "Memory allocation failed\n");
        free(compressed);
        free(decompressed);
        return 1;
    }

    // Step 3: Compress data (level 3, checksum enabled, default block size)
    zxc_compress_opts_t c_opts = {
        .level            = ZXC_LEVEL_DEFAULT,
        .checksum_enabled = 1,
        /* .block_size = 0  ->  512 KB default */
    };
    int64_t compressed_size = zxc_compress(
        original,            // Source buffer
        original_size,       // Source size
        compressed,          // Destination buffer
        max_compressed_size, // Destination capacity
        &c_opts              // Options (NULL = all defaults)
    );

    if (compressed_size <= 0) {
        fprintf(stderr, "Compression failed (error %lld)\n", (long long)compressed_size);
        free(compressed);
        free(decompressed);
        return 1;
    }

    printf("Original size:   %zu bytes\n", original_size);
    printf("Compressed size: %lld bytes (%.1f%% ratio)\n",
           (long long)compressed_size,
           100.0 * (double)compressed_size / (double)original_size);

    // Step 4: Decompress data (checksum verification enabled)
    zxc_decompress_opts_t d_opts = { .checksum_enabled = 1 };
    int64_t decompressed_size = zxc_decompress(
        compressed,          // Source buffer
        (size_t)compressed_size,
        decompressed,        // Destination buffer
        original_size,       // Destination capacity
        &d_opts              // Options (NULL = all defaults)
    );

    if (decompressed_size <= 0) {
        fprintf(stderr, "Decompression failed (error %lld)\n", (long long)decompressed_size);
        free(compressed);
        free(decompressed);
        return 1;
    }

    // Step 5: Verify integrity
    if ((size_t)decompressed_size == original_size &&
        memcmp(original, decompressed, original_size) == 0) {
        printf("Success! Data integrity verified.\n");
        printf("Decompressed: %s\n", (char*)decompressed);
    } else {
        fprintf(stderr, "Data mismatch after decompression\n");
    }

    // Cleanup
    free(compressed);
    free(decompressed);
    return 0;
}
```

**Compilation:**
```bash
gcc -o buffer_example buffer_example.c -I include -L build -lzxc_lib
```

---

## Stream API (Multi-Threaded)

For large files, use the streaming API to process data in parallel chunks.
The `FILE*`-based entry points live in `zxc_stream.h`, which the freestanding
`<zxc.h>` umbrella does *not* pull (so kernel/embedded builds stay clean of
`<stdio.h>`). Userspace consumers include it explicitly.

```c
#include "zxc.h"
#include "zxc_stream.h"   // FILE*-based streaming, opt-in
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char* argv[]) {
    if (argc != 4) {
        fprintf(stderr, "Usage: %s <input_file> <compressed_file> <output_file>\n", argv[0]);
        return 1;
    }

    const char* input_path      = argv[1];
    const char* compressed_path = argv[2];
    const char* output_path     = argv[3];

    /* ------------------------------------------------------------------ */
    /* Step 1: Compress                                                     */
    /* ------------------------------------------------------------------ */
    printf("Compressing '%s' to '%s'...\n", input_path, compressed_path);

    FILE* f_in = fopen(input_path, "rb");
    if (!f_in) {
        fprintf(stderr, "Error: cannot open '%s'\n", input_path);
        return 1;
    }

    FILE* f_out = fopen(compressed_path, "wb");
    if (!f_out) {
        fprintf(stderr, "Error: cannot create '%s'\n", compressed_path);
        fclose(f_in);
        return 1;
    }

    // 0 threads = auto-detect CPU cores; 0 block_size = 512 KB default
    zxc_compress_opts_t c_opts = {
        .n_threads        = 0,
        .level            = ZXC_LEVEL_DEFAULT,
        .checksum_enabled = 1,
        /* .block_size = 0  ->  512 KB */
    };
    int64_t compressed_bytes = zxc_stream_compress(f_in, f_out, &c_opts);

    fclose(f_in);
    fclose(f_out);

    if (compressed_bytes < 0) {
        fprintf(stderr, "Compression failed (error %lld)\n", (long long)compressed_bytes);
        return 1;
    }
    printf("Compression complete: %lld bytes written\n", (long long)compressed_bytes);

    /* ------------------------------------------------------------------ */
    /* Step 2: Decompress                                                   */
    /* ------------------------------------------------------------------ */
    printf("\nDecompressing '%s' to '%s'...\n", compressed_path, output_path);

    FILE* f_compressed = fopen(compressed_path, "rb");
    if (!f_compressed) {
        fprintf(stderr, "Error: cannot open '%s'\n", compressed_path);
        return 1;
    }

    FILE* f_decompressed = fopen(output_path, "wb");
    if (!f_decompressed) {
        fprintf(stderr, "Error: cannot create '%s'\n", output_path);
        fclose(f_compressed);
        return 1;
    }

    zxc_decompress_opts_t d_opts = { .n_threads = 0, .checksum_enabled = 1 };
    int64_t decompressed_bytes = zxc_stream_decompress(f_compressed, f_decompressed, &d_opts);

    fclose(f_compressed);
    fclose(f_decompressed);

    if (decompressed_bytes < 0) {
        fprintf(stderr, "Decompression failed (error %lld)\n", (long long)decompressed_bytes);
        return 1;
    }

    printf("Decompression complete: %lld bytes written\n", (long long)decompressed_bytes);
    printf("\nSuccess! Verify the output file matches the original.\n");

    return 0;
}
```

**Compilation:**
```bash
gcc -o stream_example stream_example.c -I include -L build -lzxc_lib -lpthread -lm
```

**Usage:**
```bash
./stream_example large_file.bin compressed.zxc decompressed.bin
```

**Features demonstrated:**
- Multi-threaded parallel processing (auto-detects CPU cores)
- Checksum validation for data integrity
- Error handling for file operations

---

## Reusable Context API

For tight loops - such as filesystem plug-ins (squashfs, dwarfs, erofs) -
where allocating and freeing internal buffers on every call would add latency,
ZXC provides opaque reusable contexts.

Internal buffers are only reallocated when the **block size changes**, so the
common case (same block size, different data) is completely allocation-free.

Options are **sticky**: settings passed via `opts` to `zxc_create_cctx()` or
`zxc_compress_cctx()` are remembered and reused on subsequent calls where
`opts` is `NULL`.

```c
#include "zxc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BLOCK_SIZE   (64 * 1024)   // 64 KB - typical squashfs block size
#define NUM_BLOCKS   32

int main(void) {
    // Create context with sticky options (level 3, no checksum, 64 KB blocks).
    // These settings are remembered for all subsequent calls.
    zxc_compress_opts_t create_opts = {
        .level            = 3,
        .checksum_enabled = 0,
        .block_size       = BLOCK_SIZE,
    };
    zxc_cctx* cctx = zxc_create_cctx(&create_opts);
    zxc_dctx* dctx = zxc_create_dctx();

    if (!cctx || !dctx) {
        fprintf(stderr, "Context creation failed\n");
        zxc_free_cctx(cctx);
        zxc_free_dctx(dctx);
        return 1;
    }

    const size_t comp_cap = (size_t)zxc_compress_bound(BLOCK_SIZE);
    uint8_t* comp = malloc(comp_cap);
    uint8_t* src  = malloc(BLOCK_SIZE);
    uint8_t* dec  = malloc(BLOCK_SIZE);

    for (int i = 0; i < NUM_BLOCKS; i++) {
        // Fill block with pseudo-random data
        for (size_t j = 0; j < BLOCK_SIZE; j++) src[j] = (uint8_t)(i ^ j);

        // Compress - pass NULL to reuse sticky settings from create_opts.
        // No malloc/free inside, no need to pass opts again.
        int64_t csz = zxc_compress_cctx(cctx, src, BLOCK_SIZE, comp, comp_cap, NULL);
        if (csz <= 0) { fprintf(stderr, "Block %d: compress error %lld\n", i, (long long)csz); break; }

        // Decompress - no malloc/free inside
        int64_t dsz = zxc_decompress_dctx(dctx, comp, (size_t)csz, dec, BLOCK_SIZE, NULL);
        if (dsz != BLOCK_SIZE || memcmp(src, dec, BLOCK_SIZE) != 0) {
            fprintf(stderr, "Block %d: roundtrip mismatch\n", i);
            break;
        }
    }

    // Override sticky settings for a single call (e.g. switch to level 5).
    // This also updates the sticky settings for future NULL calls.
    zxc_compress_opts_t high_opts = { .level = 5 };
    int64_t csz = zxc_compress_cctx(cctx, src, BLOCK_SIZE, comp, comp_cap, &high_opts);
    printf("Level-5 compressed: %lld bytes\n", (long long)csz);

    printf("Processed %d blocks of %d KB - no per-block allocation.\n",
           NUM_BLOCKS, BLOCK_SIZE / 1024);

    zxc_free_cctx(cctx);
    zxc_free_dctx(dctx);
    free(src); free(comp); free(dec);
    return 0;
}
```

**Compilation:**
```bash
gcc -o ctx_example ctx_example.c -I include -L build -lzxc_lib
```

---

## Seekable Reader (Custom Storage Backend)

The seekable API also accepts a user-supplied **reader callback**, letting you
back random-access decompression with any storage that supports positional
reads: `mmap`, an HTTP range-request client, an S3 object, a custom VFS,
`vfs_read()` in kernel space, etc.

The reader exposes a single primitive:

```c
typedef struct {
    int64_t (*read_at)(void* ctx, void* dst, size_t len, uint64_t offset);
    void*    ctx;
    uint64_t size;   // total size of the compressed archive
} zxc_reader_t;
```

`read_at` MUST be safe to call concurrently from multiple threads if you intend
to use `zxc_seekable_decompress_range_mt()`. The single-threaded path makes no
concurrent calls.

The example below wires the reader to an `mmap()`'d archive file, then
decompresses an arbitrary byte range — no `FILE*` is involved.

```c
#include "zxc.h"
#include "zxc_seekable.h"
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

typedef struct {
    const uint8_t* base;   // mmap'd start of the archive
    uint64_t       size;
} mmap_reader_ctx_t;

// read_at: just memcpy from the mapping. Naturally thread-safe.
static int64_t mmap_read_at(void* ctx, void* dst, size_t len, uint64_t offset) {
    mmap_reader_ctx_t* m = (mmap_reader_ctx_t*)ctx;
    if (offset > m->size || len > m->size - offset) return -1;  // bounds check
    memcpy(dst, m->base + offset, len);
    return (int64_t)len;
}

int main(int argc, char** argv) {
    if (argc != 4) {
        fprintf(stderr, "Usage: %s <archive.zxc> <offset> <length>\n", argv[0]);
        return 1;
    }

    // Open and mmap the seekable archive.
    int fd = open(argv[1], O_RDONLY);
    if (fd < 0) { perror("open"); return 1; }

    struct stat st;
    if (fstat(fd, &st) != 0) { perror("fstat"); close(fd); return 1; }

    void* mapping = mmap(NULL, (size_t)st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    if (mapping == MAP_FAILED) { perror("mmap"); close(fd); return 1; }

    // Wire the reader callback to the mmap'd region.
    mmap_reader_ctx_t mctx = { .base = (const uint8_t*)mapping,
                               .size = (uint64_t)st.st_size };
    zxc_reader_t reader = { .read_at = mmap_read_at,
                            .ctx     = &mctx,
                            .size    = (uint64_t)st.st_size };

    zxc_seekable* s = zxc_seekable_open_reader(&reader);
    if (!s) {
        fprintf(stderr, "Not a valid seekable ZXC archive\n");
        munmap(mapping, (size_t)st.st_size);
        close(fd);
        return 1;
    }

    fprintf(stderr, "Archive: %u blocks, %llu bytes decompressed.\n",
            zxc_seekable_get_num_blocks(s),
            (unsigned long long)zxc_seekable_get_decompressed_size(s));

    // Decompress a user-requested byte range.
    const uint64_t offset = strtoull(argv[2], NULL, 0);
    const size_t   length = (size_t)strtoull(argv[3], NULL, 0);
    uint8_t* out = malloc(length);
    if (!out) { zxc_seekable_free(s); munmap(mapping, st.st_size); close(fd); return 1; }

    // The MT path is safe here because mmap_read_at is reentrant.
    int64_t n = zxc_seekable_decompress_range_mt(s, out, length, offset, length, 0);
    if (n < 0) {
        fprintf(stderr, "decompress_range failed: %lld\n", (long long)n);
        free(out); zxc_seekable_free(s); munmap(mapping, st.st_size); close(fd);
        return 1;
    }

    fwrite(out, 1, (size_t)n, stdout);

    free(out);
    zxc_seekable_free(s);
    munmap(mapping, (size_t)st.st_size);
    close(fd);
    return 0;
}
```

**Compilation:**
```bash
gcc -o seekable_reader seekable_reader.c -I include -L build -lzxc -lpthread
```

**Other backends.** The same pattern applies to anything addressable by
offset: an HTTP `Range:` GET (return `-1` on a non-`206` response), an S3
`GetObject` with `--range`, a kernel `vfs_read(file, ..., &pos)`, a custom VFS
plug-in. As long as `read_at` returns exactly the requested length (or a
negative `zxc_error_t` code), the seekable API treats it as a transparent
backend.

---

## Compressing Numeric Data (Pre-Filters)

ZXC is a **pure LZ byte codec**: it compresses by finding repeated byte sequences
and entropy-coding the residue. Arrays of numbers (timestamps, IDs, counters,
sensor readings, floats) carry a lot of redundancy — but it lives in their
*numeric* structure (smooth progression, shared magnitude), which is invisible at
the byte level, so a raw `zxc_compress` often barely compresses them.

The fix is a **reversible pre-filter**: a cheap, lossless transform applied
**before** compression and inverted **after** decompression. It does not compress
anything itself — it re-expresses the numeric structure as byte-level redundancy
that ZXC can then exploit:

```
compress :  data -> [filter] -> zxc_compress  -> archive
decompress: archive -> zxc_decompress -> [inverse filter] -> data
```

ZXC keeps the filter **out of the format on purpose**: the codec
stays pure, and *you* own the transform — including remembering which one you
applied, in your own schema/metadata, so you can invert it. The two most useful
filters are **delta** (correlated sequences) and **byte-shuffle** (wide values
whose high bytes are similar, e.g. floats).

```c
#include "zxc.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* --- Delta: each element becomes its difference from the previous one.
 *     Great for monotonic / smooth sequences (timestamps, IDs, counters). --- */
static void delta_encode_i64(int64_t *a, size_t n) {
    for (size_t i = n; i-- > 1;) a[i] -= a[i - 1];     /* back to front */
}
static void delta_decode_i64(int64_t *a, size_t n) {
    for (size_t i = 1; i < n; i++) a[i] += a[i - 1];   /* prefix sum */
}

/* --- Byte-shuffle: group byte k of every element together, plane by plane.
 *     Great for float/double columns: similar sign/exponent bytes become
 *     contiguous runs. src and dst must not overlap; w = element size. --- */
static void shuffle_encode(const uint8_t *src, uint8_t *dst, size_t n, size_t w) {
    for (size_t b = 0; b < w; b++)
        for (size_t i = 0; i < n; i++) dst[b * n + i] = src[i * w + b];
}
static void shuffle_decode(const uint8_t *src, uint8_t *dst, size_t n, size_t w) {
    for (size_t b = 0; b < w; b++)
        for (size_t i = 0; i < n; i++) dst[i * w + b] = src[b * n + i];
}

int main(void) {
    /* A column of ~1 ms timestamps: huge values, tiny differences. */
    const size_t n = 100000;
    int64_t *ts = malloc(n * sizeof *ts);
    int64_t t = 1700000000000000LL;
    for (size_t i = 0; i < n; i++) { t += 1000000 + (int64_t)(i % 7); ts[i] = t; }

    const size_t raw = n * sizeof *ts;
    size_t cap = (size_t)zxc_compress_bound(raw);
    void *comp = malloc(cap);
    zxc_compress_opts_t opts = { .level = 3 };

    /* 1. Filter, then compress. */
    delta_encode_i64(ts, n);                       /* numeric struct -> small repeated deltas */
    int64_t csize = zxc_compress(ts, raw, comp, cap, &opts);

    /* 2. Decompress, then invert the filter. */
    int64_t *out = malloc(raw);
    zxc_decompress(comp, (size_t)csize, out, raw, NULL);
    delta_decode_i64(out, n);                      /* exact original timestamps restored */

    free(ts); free(comp); free(out);
    return 0;
}
```

**Choosing a filter (you must record this choice yourself):**

| Data | Filter | Inverse |
|---|---|---|
| Monotonic / correlated integers (timestamps, IDs, counters) | `delta` | prefix sum |
| Fixed-cadence series (drifting stride) | `delta` twice (delta-of-delta) | prefix sum twice |
| Float/double columns, wide integers | `shuffle` (byte transpose) | un-shuffle |
| Floats varying slightly between samples | XOR with previous | XOR again |

> **The archive does not record the filter.** ZXC stores only the compressed
> bytes; it has no idea a filter was applied. Your application is responsible for
> remembering — in its own schema or metadata — which filter and `elem_size` it
> used, so it can run the matching inverse after `zxc_decompress`. Apply the
> filter only to genuinely numeric, type-known data; on arbitrary bytes it will
> not help (and may hurt).

---

## Using a Pre-Trained Dictionary

For corpora of **small, similar payloads** (JSON records, log lines, RPC
messages, small files), a pre-trained dictionary substantially improves the
ratio. The dictionary is logically **prepended to the LZ window** at the start of
each block, so even the first bytes of a tiny payload can match against it.

How it ties together:

- A dictionary is up to **64 KB** of content. You can **train** one from samples
  (`zxc_train_dict`) or use any raw bytes.
- It is identified by a **`dict_id`** (a hash of its content). Serialize it to a
  `.zxd` file (`zxc_dict_save`) to distribute it.
- A dictionary-compressed archive records `HAS_DICTIONARY` + the `dict_id` in its
  header, but **not the dictionary itself** — you must provide the same
  dictionary to decompress. A decoder reads the required id with
  `zxc_get_dict_id()`.
- The biggest gains are on **small blocks** (4–128 KB). On large blocks the data
  builds its own match history, so a dictionary helps little.

### CLI quickstart

```sh
# Train from a corpus of files -> writes dictionary_<dict_id>.zxd in the
# current directory (use -o DIR/ or -o FILE to choose the destination).
zxc --train ./corpus/*

# Compress / decompress with the dictionary (required at both ends).
zxc -D dictionary_1a2b3c4d.zxd -z record.json
zxc -D dictionary_1a2b3c4d.zxd -d record.json.zxc -o record.json
```

### C API

```c
#include "zxc.h"
#include "zxc_dict.h"   // training, save/load, identification
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(void) {
    /* 1. Train a dictionary from representative samples (or use any raw bytes). */
    const char *s0 = "{\"event\":\"login\",\"user_id\":1001,\"ok\":true}";
    const char *s1 = "{\"event\":\"logout\",\"user_id\":1002,\"ok\":true}";
    const void  *samples[] = { s0, s1 };
    const size_t sizes[]   = { strlen(s0), strlen(s1) };

    uint8_t dict[4096];
    int64_t dict_size = zxc_train_dict(samples, sizes, 2, dict, sizeof dict);
    if (dict_size < 0) { fprintf(stderr, "train failed\n"); return 1; }

    /* 2. Serialize to a .zxd for distribution (carries the dict_id). */
    size_t   zbnd     = zxc_dict_save_bound((size_t)dict_size);
    uint8_t *zxd      = malloc(zbnd);
    int64_t  zxd_size = zxc_dict_save(dict, (size_t)dict_size, zxd, zbnd);
    uint32_t dict_id  = zxc_dict_get_id(zxd, (size_t)zxd_size);
    /* ... write zxd[0..zxd_size) to "samples.zxd" and ship it to decoders ... */

    /* 3. Compress WITH the dictionary. */
    const char *msg  = "{\"event\":\"login\",\"user_id\":2003,\"ok\":true}";
    size_t      n    = strlen(msg) + 1;
    size_t      cap  = (size_t)zxc_compress_bound(n);
    uint8_t    *comp = malloc(cap);

    zxc_compress_opts_t copts = { .level = 3, .dict = dict, .dict_size = (size_t)dict_size };
    int64_t csize = zxc_compress(msg, n, comp, cap, &copts);
    if (csize < 0) { fprintf(stderr, "compress failed\n"); return 1; }

    /* The archive header now carries HAS_DICTIONARY + dict_id; a decoder reads
     * which dictionary it needs with zxc_get_dict_id(comp, csize). */

    /* 4. Decompress WITH the dictionary (load its content from the .zxd). */
    const void *content; size_t content_size; uint32_t id;
    zxc_dict_load(zxd, (size_t)zxd_size, &content, &content_size, &id);

    uint8_t *out = malloc(n);
    zxc_decompress_opts_t dopts = { .dict = content, .dict_size = content_size };
    int64_t dsize = zxc_decompress(comp, (size_t)csize, out, n, &dopts);
    if (dsize != (int64_t)n || memcmp(msg, out, n) != 0) {
        fprintf(stderr, "round-trip failed\n");
        return 1;
    }

    free(zxd); free(comp); free(out);
    return 0;
}
```

### Error contract

Decoding a dictionary archive enforces that the right dictionary is supplied:

| Situation | Result |
|---|---|
| No dictionary supplied (`opts == NULL` or `dict == NULL`) | `ZXC_ERROR_DICT_REQUIRED` |
| Wrong dictionary (`dict_id` mismatch) | `ZXC_ERROR_DICT_MISMATCH` |
| Dictionary larger than `ZXC_DICT_SIZE_MAX` (64 KB) | `ZXC_ERROR_DICT_TOO_LARGE` |

So a decoder can read `zxc_get_dict_id()` first, fetch the matching `.zxd`, and
pass its loaded content via `zxc_decompress_opts_t::dict`. See `docs/FORMAT.md`
§12 for the on-disk dictionary format and §3.1 for the header fields.

---

## Meson Integration

zxc can be consumed as a Meson subproject. This is the recommended approach for
Meson-based projects that want to vendor or pin a specific zxc version.

**Step 1 — Create `subprojects/zxc.wrap`:**

```ini
[wrap-git]
url = https://github.com/hellobertrand/zxc.git
revision = head
depth = 1

[provide]
libzxc = libzxc_dep
```

**Step 2 — Declare the dependency in your `meson.build`:**

```meson
project('my_project', 'c', default_options : ['c_std=c17'])

zxc_dep = dependency('libzxc', fallback : ['zxc', 'libzxc_dep'])

executable('my_app', 'main.c', dependencies : zxc_dep)
```

When `zxc` is used as a subproject, the CLI and test suite are automatically
skipped. Only the library is built.

**Step 3 — Build and run:**

```bash
meson setup build
meson compile -C build
./build/my_app
```

---

## Language Bindings

For non-C languages, see the official bindings:

| Language | Package | Install Command | Documentation |
|----------|---------|-----------------|---------------|
| **Rust** | [`crates.io`](https://crates.io/crates/zxc-compress) | `cargo add zxc-compress` | [README](../wrappers/rust/zxc/README.md) |
| **Python**| [`PyPI`](https://pypi.org/project/zxc-compress) | `pip install zxc-compress` | [README](../wrappers/python/README.md) |
| **Node.js**| [`npm`](https://www.npmjs.com/package/zxc-compress) | `npm install zxc-compress` | [README](../wrappers/nodejs/README.md) |
| **Go** | `go get` | `go get github.com/hellobertrand/zxc/wrappers/go` | [README](../wrappers/go/README.md) |

Community-maintained:
- **Go**: https://github.com/meysam81/go-zxc
- **Nim**: https://github.com/openpeeps/zxc-nim
- **Free Pascal**: https://github.com/Xelitan/Free-Pascal-port-of-ZXC-compressor-decompressor
