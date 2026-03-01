# ZXC API Examples

This document provides complete, working examples for using the ZXC compression library in C.

## Table of Contents
- [Buffer API (Single-Threaded)](#buffer-api-single-threaded)
- [Stream API (Multi-Threaded)](#stream-api-multi-threaded)
- [Language Bindings](#language-bindings)

## Buffer API (Single-Threaded)

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
    void* compressed = malloc(max_compressed_size);
    void* decompressed = malloc(original_size);

    if (!compressed || !decompressed) {
        fprintf(stderr, "Memory allocation failed\n");
        free(compressed);
        free(decompressed);
        return 1;
    }

    // Step 3: Compress data (Level 3, checksum enabled)
    size_t compressed_size = zxc_compress(
        original,           // Source buffer
        original_size,      // Source size
        compressed,         // Destination buffer
        max_compressed_size,// Destination capacity
        ZXC_LEVEL_DEFAULT,  // Compression level
        1                   // Enable checksum
    );

    if (compressed_size == 0) {
        fprintf(stderr, "Compression failed\n");
        free(compressed);
        free(decompressed);
        return 1;
    }

    printf("Original size: %zu bytes\n", original_size);
    printf("Compressed size: %zu bytes (%.1f%% ratio)\n",
           compressed_size, 100.0 * compressed_size / original_size);

    // Step 4: Decompress data (checksum verification enabled)
    size_t decompressed_size = zxc_decompress(
        compressed,         // Source buffer
        compressed_size,    // Source size
        decompressed,       // Destination buffer
        original_size,      // Destination capacity
        1                   // Verify checksum
    );

    if (decompressed_size == 0) {
        fprintf(stderr, "Decompression failed\n");
        free(compressed);
        free(decompressed);
        return 1;
    }

    // Step 5: Verify integrity
    if (decompressed_size == original_size &&
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

## Stream API (Multi-Threaded)

For large files, use the streaming API to process data in parallel chunks. This example demonstrates parallel file compression and decompression.

```c
#include "zxc.h"
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char* argv[]) {
    if (argc != 4) {
        fprintf(stderr, "Usage: %s <input_file> <compressed_file> <output_file>\n", argv[0]);
        return 1;
    }

    const char* input_path = argv[1];
    const char* compressed_path = argv[2];
    const char* output_path = argv[3];

    // Step 1: Compress the input file using multi-threaded streaming
    printf("Compressing '%s' to '%s'...\n", input_path, compressed_path);

    FILE* f_in = fopen(input_path, "rb");
    if (!f_in) {
        fprintf(stderr, "Error: Cannot open input file '%s'\n", input_path);
        return 1;
    }

    FILE* f_out = fopen(compressed_path, "wb");
    if (!f_out) {
        fprintf(stderr, "Error: Cannot create output file '%s'\n", compressed_path);
        fclose(f_in);
        return 1;
    }

    // Compress with auto-detected threads (0), level 3, checksum enabled
    int64_t compressed_bytes = zxc_stream_compress(f_in, f_out, 0, ZXC_LEVEL_DEFAULT, 1);

    fclose(f_in);
    fclose(f_out);

    if (compressed_bytes < 0) {
        fprintf(stderr, "Compression failed\n");
        return 1;
    }

    printf("Compression complete: %lld bytes written\n", (long long)compressed_bytes);

    // Step 2: Decompress the file back using multi-threaded streaming
    printf("\nDecompressing '%s' to '%s'...\n", compressed_path, output_path);

    FILE* f_compressed = fopen(compressed_path, "rb");
    if (!f_compressed) {
        fprintf(stderr, "Error: Cannot open compressed file '%s'\n", compressed_path);
        return 1;
    }

    FILE* f_decompressed = fopen(output_path, "wb");
    if (!f_decompressed) {
        fprintf(stderr, "Error: Cannot create output file '%s'\n", output_path);
        fclose(f_compressed);
        return 1;
    }

    // Decompress with auto-detected threads (0), checksum verification enabled
    int64_t decompressed_bytes = zxc_stream_decompress(f_compressed, f_decompressed, 0, 1);

    fclose(f_compressed);
    fclose(f_decompressed);

    if (decompressed_bytes < 0) {
        fprintf(stderr, "Decompression failed\n");
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
./stream_example large_file.bin compressed.xc decompressed.bin
```

**Features demonstrated:**
- Multi-threaded parallel processing (auto-detects CPU cores)
- Checksum validation for data integrity
- Error handling for file operations
- Progress tracking via return values

## Writing Your Own Streaming Driver

The streaming multi-threaded API shown above is the default provided driver. However, ZXC is written in a **"sans-IO" style** that separates compute from I/O and multitasking.

This allows you to write your own driver in any language of your choice, using the native I/O and multitasking capabilities of that language.

To implement a custom driver:
1. Include the extra public header `zxc_sans_io.h`
2. Study the reference implementation in `src/lib/zxc_driver.c`
3. Implement your own I/O and threading logic

## Language Bindings

For non-C languages, see the official bindings:

| Language | Package | Install Command | Documentation |
|----------|---------|-----------------|---------------|
| **Rust** | [`crates.io`](https://crates.io/crates/zxc-compress) | `cargo add zxc-compress` | [README](../wrappers/rust/zxc/README.md) |
| **Python**| [`PyPI`](https://pypi.org/project/zxc-compress) | `pip install zxc-compress` | [README](../wrappers/python/README.md) |
| **Node.js**| [`npm`](https://www.npmjs.com/package/zxc-compress) | `npm install zxc-compress` | [README](../wrappers/nodejs/README.md) |

Community-maintained:
- **Go**: https://github.com/meysam81/go-zxc
