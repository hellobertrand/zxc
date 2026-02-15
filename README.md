# ZXC: High-Performance Asymmetric Lossless Compression

[![Build & Release](https://github.com/hellobertrand/zxc/actions/workflows/build.yml/badge.svg)](https://github.com/hellobertrand/zxc/actions/workflows/build.yml)
[![Code Quality](https://github.com/hellobertrand/zxc/actions/workflows/quality.yml/badge.svg)](https://github.com/hellobertrand/zxc/actions/workflows/quality.yml)
[![Fuzzing](https://github.com/hellobertrand/zxc/actions/workflows/fuzzing.yml/badge.svg)](https://github.com/hellobertrand/zxc/actions/workflows/fuzzing.yml)
[![Benchmark](https://github.com/hellobertrand/zxc/actions/workflows/benchmark.yml/badge.svg)](https://github.com/hellobertrand/zxc/actions/workflows/benchmark.yml)
[![Code Coverage](https://codecov.io/github/hellobertrand/zxc/branch/main/graph/badge.svg?token=LHA03HOA1X)](https://codecov.io/github/hellobertrand/zxc)

[![vcpkg](https://img.shields.io/vcpkg/v/zxc)](https://vcpkg.io/en/package/zxc)
[![Crates.io](https://img.shields.io/crates/v/zxc-compress)](https://crates.io/crates/zxc-compress)
[![PyPi](https://img.shields.io/pypi/v/zxc-compress)](https://pypi.org/project/zxc-compress)

[![License](https://img.shields.io/badge/license-BSD--3--Clause-blue)](LICENSE)

**ZXC** is a high-performance, lossless, asymmetric compression library optimized for **Content Delivery** and **Embedded Systems** (Game Assets, Firmware, App Bundles).
It is designed to be *"Write Once, Read Many"*. Unlike codecs like LZ4, ZXC trades compression speed (build-time) for **maximum decompression throughput** (run-time).

## TL;DR

- **What:** A C library for lossless compression, optimized for **maximum decompression speed**.
- **Key Result:** Up to **>40% faster** decompression than LZ4 on Apple Silicon, **>25% faster** on Google Axion (ARM64), **>5% faster** on x86_64, **all with better compression ratios**.
- **Use Cases:** Game assets, firmware, app bundles, anything *compressed once, decompressed millions of times*.
- **Install:** `vcpkg install zxc` · `pip install zxc-compress` · `cargo add zxc-compress`
- **Quality:** Fuzzed, sanitized, formally tested, thread-safe API. BSD-3-Clause.

> **Verified:** ZXC has been officially merged into the **[lzbench master branch](https://github.com/inikep/lzbench)**. You can now verify these results independently using the industry-standard benchmark suite.


## ZXC Design Philosophy

Traditional codecs often force a trade-off between **symmetric speed** (LZ4) and **archival density** (Zstd).

**ZXC focuses on Asymmetric Efficiency.**

Designed for the "Write-Once, Read-Many" reality of software distribution, ZXC utilizes a computationally intensive encoder to generate a bitstream specifically structured to **maximize decompression throughput**.
By performing heavy analysis upfront, the encoder produces a layout optimized for the instruction pipelining and branch prediction capabilities of modern CPUs, particularly ARMv8, effectively offloading complexity from the decoder to the encoder.

*   **Build Time:** You generally compress only once (on CI/CD).
*   **Run Time:** You decompress millions of times (on every user's device). **ZXC respects this asymmetry.**

[👉 **Read the Technical Whitepaper**](docs/WHITEPAPER.md)


## Benchmarks

To ensure consistent performance, benchmarks are automatically executed on every commit via GitHub Actions.
We monitor metrics on both **x86_64** (Linux) and **ARM64** (Apple Silicon M2) runners to track compression speed, decompression speed, and ratios.

*(See the [latest benchmark logs](https://github.com/hellobertrand/zxc/actions/workflows/benchmark.yml))*


### 1. Mobile & Client: Apple Silicon (M2)
*Scenario: Game Assets loading, App startup.*

| Target | ZXC vs Competitor | Decompression Speed | Ratio | Verdict |
| :--- | :--- | :--- | :--- | :--- |
| **1. Max Speed** | **ZXC -1** vs *LZ4 --fast* | **11,045 MB/s** vs 5,651 MB/s **1.95x Faster** | **61.2** vs 62.2 **Smaller** (-1.5%) | **ZXC** leads in raw throughput. |
| **2. Standard** | **ZXC -3** vs *LZ4 Default* | **6,948 MB/s** vs 4,802 MB/s **1.45x Faster** | **46.5** vs 47.6 **Smaller** (-2.4%) | **ZXC** outperforms LZ4 in read speed and ratio. |
| **3. High Density** | **ZXC -5** vs *Zstd --fast 1* | **6,072 MB/s** vs 2,159 MB/s **2.81x Faster** | **40.7** vs 41.0 **Equivalent** (-0.9%) | **ZXC** outperforms Zstd in decoding speed. |

### 2. Cloud Server: Google Axion (ARM Neoverse V2)
*Scenario: High-throughput Microservices, ARM Cloud Instances.*

| Target | ZXC vs Competitor | Decompression Speed | Ratio | Verdict |
| :--- | :--- | :--- | :--- | :--- |
| **1. Max Speed** | **ZXC -1** vs *LZ4 --fast* | **8,437 MB/s** vs 4,847 MB/s **1.74x Faster** | **61.2** vs 62.2 **Smaller** (-1.5%) | **ZXC** leads in raw throughput. |
| **2. Standard** | **ZXC -3** vs *LZ4 Default* | **5,215 MB/s** vs 4,151 MB/s **1.26x Faster** | **46.5** vs 47.6 **Smaller** (-2.4%) | **ZXC** outperforms LZ4 in read speed and ratio. |
| **3. High Density** | **ZXC -5** vs *Zstd --fast 1* | **4,509 MB/s** vs 1,748 MB/s **2.58x Faster** | **40.7** vs 41.0 **Equivalent** (-0.9%) | **ZXC** outperforms Zstd in decoding speed. |

### 3. Build Server: x86_64 (AMD EPYC 7763)
*Scenario: CI/CD Pipelines compatibility.*

| Target | ZXC vs Competitor | Decompression Speed | Ratio | Verdict |
| :--- | :--- | :--- | :--- | :--- |
| **1. Max Speed** | **ZXC -1** vs *LZ4 --fast* | **6,233 MB/s** vs 4,109 MB/s **1.52x Faster** | **61.2** vs 62.2 **Smaller** (-1.5%) | **ZXC** achieves higher throughput. |
| **2. Standard** | **ZXC -3** vs *LZ4 Default* | **3,814 MB/s** vs 3,555 MB/s **1.07x Faster** | **46.5** vs 47.6 **Smaller** (-2.4%) | ZXC offers improved speed and ratio. |
| **3. High Density** | **ZXC -5** vs *Zstd --fast 1* | **3,448 MB/s** vs 1,572 MB/s **2.19x Faster** | **40.7** vs 41.0 **Equivalent** (-0.9%) | **ZXC** provides faster decoding. |


*(Benchmark Graph ARM64 : Decompression Throughput & Storage Ratio (Normalized to LZ4))*
![Benchmark Graph ARM64](docs/images/benchmark_arm64_0.7.1.png)


### Benchmark ARM64 (Apple Silicon)

Benchmarks were conducted using lzbench 2.2.1 (from @inikep), compiled with Clang 17.0.0 using *MOREFLAGS="-march=native"* on macOS Sequoia 15.7.2 (Build 24G325). The reference hardware is an Apple M2 processor (ARM64). All performance metrics reflect single-threaded execution on the standard Silesia Corpus.

| Compressor name         | Compression| Decompress.| Compr. size | Ratio | Filename |
| ---------------         | -----------| -----------| ----------- | ----- | -------- |
| memcpy                  | 52833 MB/s | 52754 MB/s |   211938580 |100.00 | 12 files|
| **zxc 0.7.1 -1**            |   913 MB/s | **11045 MB/s** |   129770958 | **61.23** | 12 files|
| **zxc 0.7.1 -2**            |   608 MB/s |  **8974 MB/s** |   115921778 | **54.70** | 12 files|
| **zxc 0.7.1 -3**            |   180 MB/s |  **6948 MB/s** |    98472307 | **46.46** | 12 files|
| **zxc 0.7.1 -4**            |   123 MB/s |  **6599 MB/s** |    92027546 | **43.42** | 12 files|
| **zxc 0.7.1 -5**            |  65.2 MB/s |  **6072 MB/s** |    86177811 | **40.66** | 12 files|
| lz4 1.10.0              |   814 MB/s |  4802 MB/s |   100880147 | 47.60 | 12 files|
| lz4 1.10.0 --fast -17   |  1342 MB/s |  5651 MB/s |   131723524 | 62.15 | 12 files|
| lz4hc 1.10.0 -12        |  13.9 MB/s |  4543 MB/s |    77262399 | 36.46 | 12 files|
| zstd 1.5.7 -1           |   644 MB/s |  1620 MB/s |    73229468 | 34.55 | 12 files|
| zstd 1.5.7 --fast --1   |   724 MB/s |  2159 MB/s |    86932028 | 41.02 | 12 files|
| brotli 1.2.0 -0         |   539 MB/s |   418 MB/s |    78306095 | 36.95 | 12 files|
| snappy 1.2.2            |   880 MB/s |  3262 MB/s |   101352257 | 47.82 | 12 files|

### Benchmark ARM64 (Google Axion)

Benchmarks were conducted using lzbench 2.2.1 (from @inikep), compiled with GCC 12.2.0 using *MOREFLAGS="-march=native"* on Linux 64-bits Debian GNU/Linux 12 (bookworm). The reference hardware is a Google Neoverse-V2 processor (ARM64). All performance metrics reflect single-threaded execution on the standard Silesia Corpus.

| Compressor name         | Compression| Decompress.| Compr. size | Ratio | Filename |
| ---------------         | -----------| -----------| ----------- | ----- | -------- |
| memcpy                  | 23949 MB/s | 23969 MB/s |   211938580 |100.00 | 12 files|
| **zxc 0.7.1 -1**            |   808 MB/s |  **8437 MB/s** |   129770958 | **61.23** | 12 files|
| **zxc 0.7.1 -2**            |   539 MB/s |  **7003 MB/s** |   115921778 | **54.70** | 12 files|
| **zxc 0.7.1 -3**            |   163 MB/s |  **5215 MB/s** |    98472307 | **46.46** | 12 files|
| **zxc 0.7.1 -4**            |   111 MB/s |  **4968 MB/s** |    92027546 | **43.42** | 12 files|
| **zxc 0.7.1 -5**            |  58.2 MB/s |  **4509 MB/s** |    86177811 | **40.66** | 12 files|
| lz4 1.10.0              |   740 MB/s |  4151 MB/s |   100880147 | 47.60 | 12 files|
| lz4 1.10.0 --fast -17   |  1282 MB/s |  4847 MB/s |   131723524 | 62.15 | 12 files|
| lz4hc 1.10.0 -12        |  12.4 MB/s |  3776 MB/s |    77262399 | 36.46 | 12 files|
| zstd 1.5.7 -1           |   519 MB/s |  1345 MB/s |    73229468 | 34.55 | 12 files|
| zstd 1.5.7 --fast --1   |   605 MB/s |  1748 MB/s |    86932028 | 41.02 | 12 files|
| brotli 1.2.0 -0         |   422 MB/s |   385 MB/s |    78306095 | 36.95 | 12 files|
| snappy 1.2.2            |   747 MB/s |  1832 MB/s |   101352257 | 47.82 | 12 files|


### Benchmark x86_64

Benchmarks were conducted using lzbench 2.2.1 (from @inikep), compiled with GCC 13.3.0 using *MOREFLAGS="-march=native"* on Linux 64-bits Ubuntu 24.04. The reference hardware is an AMD EPYC 7763 processor (x86_64). All performance metrics reflect single-threaded execution on the standard Silesia Corpus.

| Compressor name         | Compression| Decompress.| Compr. size | Ratio | Filename |
| ---------------         | -----------| -----------| ----------- | ----- | -------- |
| memcpy                  | 20222 MB/s | 20186 MB/s |   211938580 |100.00 | 12 files|
| **zxc 0.7.1 -1**            |   601 MB/s |  **6233 MB/s** |   129770958 | **61.23** | 12 files|
| **zxc 0.7.1 -2**            |   397 MB/s |  **5081 MB/s** |   115921778 | **54.70** | 12 files|
| **zxc 0.7.1 -3**            |   128 MB/s |  **3814 MB/s** |    98472307 | **46.46** | 12 files|
| **zxc 0.7.1 -4**            |  89.7 MB/s |  **3665 MB/s** |    92027546 | **43.42** | 12 files|
| **zxc 0.7.1 -5**            |  48.2 MB/s |  **3448 MB/s** |    86177811 | **40.66** | 12 files|
| lz4 1.10.0              |   594 MB/s |  3555 MB/s |   100880147 | 47.60 | 12 files|
| lz4 1.10.0 --fast -17   |  1034 MB/s |  4109 MB/s |   131723524 | 62.15 | 12 files|
| lz4hc 1.10.0 -12        |  11.3 MB/s |  3479 MB/s |    77262399 | 36.46 | 12 files|
| zstd 1.5.7 -1           |   414 MB/s |  1197 MB/s |    73229468 | 34.55 | 12 files|
| zstd 1.5.7 --fast --1   |   453 MB/s |  1572 MB/s |    86932028 | 41.02 | 12 files|
| brotli 1.2.0 -0         |   358 MB/s |   287 MB/s |    78306095 | 36.95 | 12 files|
| snappy 1.2.2            |   612 MB/s |  1587 MB/s |   101464727 | 47.87 | 12 files|


---

## Installation

### Option 1: Download Release (GitHub)

1.  Go to the [Releases page](https://github.com/hellobertrand/zxc/releases).
2.  Download the archive matching your architecture:
    
    **macOS:**
    *   `zxc-macos-arm64.tar.gz` (NEON optimizations included).
    
    **Linux:**
    *   `zxc-linux-aarch64.tar.gz` (NEON optimizations included).
    *   `zxc-linux-x86_64.tar.gz` (Runtime dispatch for AVX2/AVX512).
    
    **Windows:**
    *   `zxc-windows-x64.zip` (Runtime dispatch for AVX2/AVX512).

3.  Extract and install:
    ```bash
    tar -xzf zxc-linux-x86_64.tar.gz -C /usr/local
    ```

    Each archive contains:
    ```
    bin/zxc                          # CLI binary
    include/                         # C headers (zxc.h, zxc_buffer.h, ...)
    lib/libzxc.a                     # Static library
    lib/pkgconfig/zxc.pc             # pkg-config support
    lib/cmake/zxc/zxcConfig.cmake    # CMake find_package(zxc) support
    ```

4.  Use in your project:

    **CMake:**
    ```cmake
    find_package(zxc REQUIRED)
    target_link_libraries(myapp PRIVATE zxc::zxc_lib)
    ```

    **pkg-config:**
    ```bash
    cc myapp.c $(pkg-config --cflags --libs zxc) -o myapp
    ```

### Option 2: vcpkg

**Classic mode:**
```bash
vcpkg install zxc
```

**Manifest mode** (add to `vcpkg.json`):
```json
{
  "dependencies": ["zxc"]
}
```

Then in your CMake project:
```cmake
find_package(zxc CONFIG REQUIRED)
target_link_libraries(myapp PRIVATE zxc::zxc_lib)
```

### Option 3: Conan

```bash
conan install --requires="zxc/[*]" --build=missing
```

Or add to your `conanfile.txt`:
```ini
[requires]
zxc/[*]
```

Then in your CMake project:
```cmake
find_package(zxc CONFIG REQUIRED)
target_link_libraries(myapp PRIVATE zxc::zxc_lib)
```

### Option 4: Building from Source

**Requirements:** CMake (3.14+), C17 Compiler (Clang/GCC/MSVC).

```bash
git clone https://github.com/hellobertrand/zxc.git
cd zxc
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel

# Run tests
ctest --test-dir build -C Release --output-on-failure

# CLI usage
./build/zxc --help

# Install library, headers, and CMake/pkg-config files
sudo cmake --install build
```

#### CMake Options

| Option | Default | Description |
|--------|---------|-------------|
| `BUILD_SHARED_LIBS` | OFF | Build shared libraries instead of static (`libzxc.so`, `libzxc.dylib`, `zxc.dll`) |
| `ZXC_NATIVE_ARCH` | ON | Enable `-march=native` for maximum performance |
| `ZXC_ENABLE_LTO` | ON | Enable Link-Time Optimization (LTO) |
| `ZXC_PGO_MODE` | OFF | Profile-Guided Optimization mode (`OFF`, `GENERATE`, `USE`) |
| `ZXC_BUILD_CLI` | ON | Build command-line interface |
| `ZXC_BUILD_TESTS` | ON | Build unit tests |
| `ZXC_ENABLE_COVERAGE` | OFF | Enable code coverage generation (disables LTO/PGO) |

```bash
# Build shared library
cmake -B build -DBUILD_SHARED_LIBS=ON

# Portable build (without -march=native)
cmake -B build -DZXC_NATIVE_ARCH=OFF

# Library only (no CLI, no tests)
cmake -B build -DZXC_BUILD_CLI=OFF -DZXC_BUILD_TESTS=OFF

# Code coverage build
cmake -B build -DZXC_ENABLE_COVERAGE=ON
```

---

## Compression Levels

*   **Level 1, 2 (Fast):** Optimized for real-time assets (Gaming, UI).
*   **Level 3, 4 (Balanced):** A strong middle-ground offering efficient compression speed and a ratio superior to LZ4.
*   **Level 5 (Compact):** The best choice for Embedded, Firmware, or Archival. Better compression than LZ4 and significantly faster decoding than Zstd.

---

## Usage

### 1. CLI

The CLI is perfect for benchmarking or manually compressing assets.

```bash
# Basic Compression (Level 3 is default)
zxc -z input_file output_file

# High Compression (Level 5)
zxc -z -5 input_file output_file

# -z for compression can be omitted
zxc input_file output_file

# as well as output file; it will be automatically assigned to input_file.xc
zxc input_file

# Decompression
zxc -d compressed_file output_file

# Benchmark Mode (Testing speed on your machine)
zxc -b input_file
```
### 2. API

ZXC provides a **thread-safe, stateless API** with two usage patterns:

#### Buffer API (In-Memory)
```c
#include "zxc.h"

// Compression
uint64_t bound = zxc_compress_bound(src_size);
size_t compressed_size = zxc_compress(src, src_size, dst, bound, level, checksum);

// Decompression
size_t decompressed_size = zxc_decompress(src, src_size, dst, dst_capacity, checksum);
```

#### Stream API (Files, Multi-Threaded)
```c
#include "zxc.h"

// Compression
int64_t result = zxc_stream_compress(f_in, f_out, threads, level, checksum);

// Decompression
int64_t result = zxc_stream_decompress(f_in, f_out, threads, checksum);
```

**Features:**
- Caller-allocated buffers with explicit bounds
- Thread-safe (stateless)
- Multi-threaded streaming (auto-detects CPU cores)
- Optional checksum validation

**[See complete examples and advanced usage →](docs/EXAMPLES.md)**

## Language Bindings

Official wrappers maintained in this repository:

| Language | Package Manager | Install Command | Documentation | Author |
|----------|-----------------|-----------------|---------------|--------|
| **Rust** | [`crates.io`](https://crates.io/crates/zxc-compress) | `cargo add zxc-compress` | [README](wrappers/rust/zxc/README.md) | [@hellobertrand](https://github.com/hellobertrand) |
| **Python**| [`PyPI`](https://pypi.org/project/zxc-compress) | `pip install zxc-compress` | [README](wrappers/python/README.md) | [@nuberchardzer1](https://github.com/nuberchardzer1) |

Community-maintained bindings:

| Language | Package Manager | Install Command | Repository | Author |
| -------- | --------------- | --------------- | ---------- | ------ |
| **Go** | pkg.go.dev | `go get github.com/meysam81/go-zxc` | <https://github.com/meysam81/go-zxc> | [@meysam81](https://github.com/meysam81) |

## Safety & Quality
* **Unit Tests**: Comprehensive test suite with CTest integration.
* **Continuous Fuzzing**: Integrated with ClusterFuzzLite suites.
* **Static Analysis**: Checked with Cppcheck & Clang Static Analyzer.
* **CodeQL Analysis**: GitHub Advanced Security scanning for vulnerabilities.
* **Code Coverage**: Automated tracking with Codecov integration.
* **Dynamic Analysis**: Validated with Valgrind and ASan/UBSan in CI pipelines.
* **Safe API**: Explicit buffer capacity is required for all operations.


## License & Credits

**ZXC** Copyright © 2025-2026, Bertrand Lebonnois and contributors.
Licensed under the **BSD 3-Clause License**. See LICENSE for details.

**Third-Party Components:**
- **rapidhash** by Nicolas De Carli (MIT) - Used for high-speed, platform-independent checksums.
