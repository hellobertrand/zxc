# ZXC: Asymmetric High-Performance Compression

[![Build & Release](https://github.com/hellobertrand/zxc/actions/workflows/build.yml/badge.svg)](https://github.com/hellobertrand/zxc/actions/workflows/build.yml)
[![Code Quality](https://github.com/hellobertrand/zxc/actions/workflows/quality.yml/badge.svg)](https://github.com/hellobertrand/zxc/actions/workflows/quality.yml)
[![Fuzzing](https://github.com/hellobertrand/zxc/actions/workflows/fuzzing.yml/badge.svg)](https://github.com/hellobertrand/zxc/actions/workflows/fuzzing.yml)
[![Benchmark](https://github.com/hellobertrand/zxc/actions/workflows/benchmark.yml/badge.svg)](https://github.com/hellobertrand/zxc/actions/workflows/benchmark.yml)

[![License](https://img.shields.io/badge/license-BSD--3--Clause-blue)](LICENSE)


**ZXC** is an asymmetric high-performance lossless compression library optimized for **Content Delivery** and **Embedded Systems** (Game Assets, Firmware, App Bundles). 
It is designed to be *"Write Once, Read Many."*. Unlike symmetric codecs (LZ4), ZXC trades compression speed (build-time) for **maximum decompression throughput** (run-time).

> **Key Result:** ZXC outperforms LZ4 decompression by **+40% on Apple Silicon** and **+22% on Cloud ARM (Google Axion)**.  


## ZXC Design Philosophy

Traditional codecs often force a trade-off between **symmetric speed** (LZ4) and **archival density** (Zstd).

**ZXC focuses on Asymmetric Efficiency.**

Designed for the "Write-Once, Read-Many" reality of software distribution, ZXC utilizes a computationally intensive encoder to generate a bitstream specifically structured to **maximize decompression throughput**.
By performing heavy analysis upfront, the encoder produces a layout optimized for the instruction pipelining and branch prediction capabilities of modern CPUs, particularly ARMv8, effectively offloading complexity from the decoder to the encoder.

*   **Build Time:** You generally compress only once (on CI/CD).
*   **Run Time:** You decompress millions of times (on every user's device). **ZXC respects this asymmetry.**

[👉 **Read the Technical Whitepaper**](./WHITEPAPER.md)


## Benchmarks

To ensure consistent performance, benchmarks are automatically executed on every commit via GitHub Actions.
We monitor metrics on both **x86_64** (Linux) and **ARM64** (Apple Silicon M1/M2) runners to track compression speed, decompression speed, and ratios.

*(See the [latest benchmark logs](https://github.com/hellobertrand/zxc/actions/workflows/benchmark.yml))*


### 1. Mobile & Client: Apple Silicon (M2/M3)
*Scenario: Game Assets loading, App startup.*

| Codec | Decoding Speed | Ratio vs LZ4 | Verdict |
| :--- | :--- | :--- | :--- |
| **ZXC -3 (Standard)** | **6,365 MB/s** | **Smaller (-1.6%)** | **1.39x Faster than LZ4** |
| **ZXC -5 (Compact)** | **5,363 MB/s** | **Dense (-14.1%)** | **3.3x Faster than Zstd-1** |
| LZ4 1.10 | 4,571 MB/s | Reference | |

### 2. Cloud Server: Google Axion (ARM Neoverse V2)
*Scenario: High-throughput Microservices, ARM Cloud Instances.*

| Codec | Decoding Speed | Ratio vs LZ4 | Verdict |
| :--- | :--- | :--- | :--- |
| **ZXC -3 (Standard)** | **5,084 MB/s** | **Smaller (-1.6%)** | **1.22x Faster than LZ4** |
| LZ4 1.10 | 4,147 MB/s | Reference | |

### 3. Build Server: x86_64 (AMD EPYC)
*Scenario: CI/CD Pipelines compatibility.*

| Codec | Decoding Speed | Ratio vs LZ4 | Verdict |
| :--- | :--- | :--- | :--- |
| **ZXC -3 (Standard)** | **3,702 MB/s** | **Smaller (-1.6%)** | **Faster than LZ4 (+4%)** |
| LZ4 1.10 | 3,551 MB/s | Reference | Reference Speed |


*(Benchmark Graph ARM64 : Decompression Throughput & Storage Ratio (Normalized to LZ4))*
![Benchmark Graph ARM64](docs/images/benchmark_arm64.png)



### Benchmark ARM64 (Apple Silicon)

Benchmarks were conducted using lzbench (from @inikep), compiled with Clang 17.0.0 on macOS Sequoia 15.7.2 (Build 24G325). The reference hardware is an Apple M2 processor (ARM64). All performance metrics reflect single-threaded execution on the standard Silesia Corpus.

| Compressor name         | Compression| Decompress.| Compr. size | Ratio | Filename |
| ---------------         | -----------| -----------| ----------- | ----- | -------- |
| memcpy                  | 51970 MB/s | 49784 MB/s |   211938580 |100.00 | 12 files|
| **zxc 0.1.0 -2**            |   **422 MB/s** |  **7174 MB/s** |   128031177 | **60.41** | 12 files|
| **zxc 0.1.0 -3**            |   **182 MB/s** |  **6365 MB/s** |    99295121 | **46.85** | 12 files|
| **zxc 0.1.0 -4**            |   **168 MB/s** |  **5954 MB/s** |    93431082 | **44.08** | 12 files|
| **zxc 0.1.0 -5**            |  **68.2 MB/s** |  **5344 MB/s** |    86696245 | **40.91** | 12 files|
| lz4 1.10.0              |   770 MB/s |  4571 MB/s |   100880147 | 47.60 | 12 files|
| lz4 1.10.0 --fast -17   |  1270 MB/s |  5298 MB/s |   131723524 | 62.15 | 12 files|
| lz4hc 1.10.0 -12        |  13.3 MB/s |  4335 MB/s |    77262399 | 36.46 | 12 files|
| zstd 1.5.7 -1           |   607 MB/s |  1609 MB/s |    73229468 | 34.55 | 12 files|
| snappy 1.2.2            |   818 MB/s |  3217 MB/s |   101352257 | 47.82 | 12 files|


### Benchmark ARM64 (Google Axion)

Benchmarks were conducted using lzbench (from @inikep), compiled with GCC 12.2.0 on Linux 64-bits Debian GNU/Linux 12 (bookworm). The reference hardware is a Google Neoverse-V2 processor (ARM64). All performance metrics reflect single-threaded execution on the standard Silesia Corpus.

| Compressor name         | Compression| Decompress.| Compr. size | Ratio | Filename |
| ---------------         | -----------| -----------| ----------- | ----- | -------- |
| memcpy                  | 23009 MB/s | 23218 MB/s |   211938580 |100.00 | 12 files|
| **zxc 0.1.0 -2**            |   **418 MB/s** |  **6262 MB/s** |   128031177 | **60.41** | 12 files|
| **zxc 0.1.0 -3**            |   **200 MB/s** |  **5084 MB/s** |    99295121 | **46.85** | 12 files|
| **zxc 0.1.0 -4**            |   **171 MB/s** |  **4779 MB/s** |    93431082 | **44.08** | 12 files|
| **zxc 0.1.0 -5**            |  **66.6 MB/s** |  **4308 MB/s** |    86696245 | **40.91** | 12 files|
| lz4 1.10.0              |   735 MB/s |  4147 MB/s |   100880147 | 47.60 | 12 files|
| lz4 1.10.0 --fast -17   |  1285 MB/s |  4817 MB/s |   131723524 | 62.15 | 12 files|
| lz4hc 1.10.0 -12        |  12.5 MB/s |  3769 MB/s |    77262399 | 36.46 | 12 files|
| zstd 1.5.7 -1           |   518 MB/s |  1359 MB/s |    73229468 | 34.55 | 12 files|
| snappy 1.2.2            |   741 MB/s |  1828 MB/s |   101352257 | 47.82 | 12 files|

### Benchmark x86_64

Benchmarks were conducted using lzbench (from @inikep), compiled with GCC 13.3.0 on Linux 64-bits Ubuntu 24.04. The reference hardware is an AMD EPYC 7763 processor (x86_64). All performance metrics reflect single-threaded execution on the standard Silesia Corpus.

| Compressor name         | Compression| Decompress.| Compr. size | Ratio | Filename |
| ---------------         | -----------| -----------| ----------- | ----- | -------- |
| memcpy                  | 20717 MB/s | 20162 MB/s |   211938580 |100.00 | 12 files|
| **zxc 0.1.0 -2**            |   **348 MB/s** |  **4403 MB/s** |   128031177 | **60.41** | 12 files|
| **zxc 0.1.0 -3**            |   **157 MB/s** |  **3702 MB/s** |    99295121 | **46.85** | 12 files|
| **zxc 0.1.0 -4**            |   **139 MB/s** |  **3454 MB/s** |    93431082 | **44.08** | 12 files|
| **zxc 0.1.0 -5**            |  **58.4 MB/s** |  **3193 MB/s** |    86696245 | **40.91** | 12 files|
| lz4 1.10.0              |   593 MB/s |  3551 MB/s |   100880147 | 47.60 | 12 files|
| lz4 1.10.0 --fast -17   |  1034 MB/s |  4114 MB/s |   131723524 | 62.15 | 12 files|
| lz4hc 1.10.0 -12        |  11.3 MB/s |  3476 MB/s |    77262399 | 36.46 | 12 files|
| zstd 1.5.7 -1           |   408 MB/s |  1199 MB/s |    73229468 | 34.55 | 12 files|
| snappy 1.2.2            |   610 MB/s |  1590 MB/s |   101464727 | 47.87 | 12 files|


---

## Installation

### Option 1: Download Release (GitHub)

1.  Go to the [Releases page](https://github.com/hellobertrand/zxc/releases).
2.  Download the binary matching your architecture:
    *   `zxc-macos-arm64` for Apple Silicon.
    *   `zxc-linux-aarch64` for ARM-based Linux servers.
    *   `zxc-linux-x86_64` for standard Linux servers.
    *   `zxc-windows-x86_64.exe` for Windows servers.
3.  Make the binary executable:
    ```bash
    chmod +x zxc-*
    mv zxc-* zxc
    ```

### Option 2: Building from Source

**Requirements:** CMake (3.10+), C Compiler (Clang/GCC C11), Make/Ninja.

```bash
git clone https://github.com/hellobertrand/zxc.git
cd zxc
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make
# Binary usage:
./zxc --help
```

---

## Compression Levels

*   **Level 2 or 3 (Fast):** Optimized for real-time assets (Gaming, UI). ~40% faster loading than LZ4 with comparable compression (Level 3).
*   **Level 4 (Balanced):** A strong middle-ground offering efficient compression speed and a ratio superior to LZ4.
*   **Level 5 (Compact):** The best choice for Embedded, Firmware, or Archival. Better compression than LZ4 and significantly faster decoding than Zstd.

---

## Usage

### 1. CLI

The CLI is perfect for benchmarking or manually compressing assets.

```bash
# Basic Compression (Level 3 is default)
zxc -z input_file output_file

# High Compression (Level 5)
zxc -z input_file output_file -l 5

# Decompression
zxc -d compressed_file output_file

# Benchmark Mode (Testing speed on your machine)
zxc -b input_file
```
### 2. API

ZXC provides a fully **thread-safe (stateless)** and **binding-friendly API**, utilizing caller-allocated buffers with explicit bounds. Integration is straightforward: simply include `zxc.h` and link against `lzxc_lib`.

#### Single-Threaded API (Memory Buffers)
Ideal for small assets or simple integrations. Ready for highly concurrent environments (Go routines, Node.js workers, Python threads).

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
    size_t max_compressed_size = zxc_compress_bound(original_size);
    
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

#### Multi-Threaded API (File Streams)
For large files, use the streaming API to process data in parallel chunks.
Here's a complete example demonstrating parallel file compression and decompression using the streaming API:

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

This example demonstrates:
* Multi-threaded parallel processing (auto-detects CPU cores)
* Checksum validation for data integrity
* Error handling for file operations
* Progress tracking via return values


## Safety & Quality
* **Continuous Fuzzing**: Integrated with Google OSS-Fuzz (PR ready) and local libFuzzer suites.
* **Static Analysis**: Checked with CPPChecker & Clang Static Analyzer.
* **Dynamic Analysis**: Validated with Valgrind and ASan/UBSan in CI pipelines.
* **Safe API: Explicit** buffer capacity is required for all operations.


## License & Credits

**ZXC Codec** Copyright © 2025, Bertrand Lebonnois.
Licensed under the **BSD 3-Clause License**. See LICENSE for details.

**Third-Party Components:**
- **xxHash** by Yann Collet (BSD 2-Clause) - Used for high-speed checksums.