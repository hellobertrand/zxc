# ZXC API Examples

This document provides complete, working examples for using the ZXC compression library in C.

## Table of Contents
- [Buffer API (In-Memory)](#buffer-api-in-memory)
- [Stream API (Multi-Threaded)](#stream-api-multi-threaded)
- [Reusable Context API](#reusable-context-api)
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
        /* .block_size = 0  ->  256 KB default */
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

```c
#include "zxc.h"
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

    // 0 threads = auto-detect CPU cores; 0 block_size = 256 KB default
    zxc_compress_opts_t c_opts = {
        .n_threads        = 0,
        .level            = ZXC_LEVEL_DEFAULT,
        .checksum_enabled = 1,
        /* .block_size = 0  ->  256 KB */
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

## Writing Your Own Streaming Driver

The streaming multi-threaded API shown above is the default provided driver. However, ZXC is written in a **"sans-IO" style** that separates compute from I/O and multitasking.

This allows you to write your own driver in any language of your choice, using the native I/O and multitasking capabilities of that language.

To implement a custom driver:
1. Include the extra public header `zxc_sans_io.h`
2. Study the reference implementation in `src/lib/zxc_driver.c`
3. Implement your own I/O and threading logic

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
