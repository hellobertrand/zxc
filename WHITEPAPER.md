# ZXC: High-Performance Asymmetric Compression for Cross-Platform Deployment

**Subtitle**: Optimizing Storage and Latency in WORM (Write-Once, Read-Many) Architectures
**Version**: 0.1.0
**Date**: December 2025
**Author**: Bertrand Lebonnois

---

## 1. Executive Summary

In modern software delivery pipelines—specifically **Mobile Gaming**, **Embedded Systems**, and **FOTA (Firmware Over-The-Air)**—data is typically generated on high-performance x86 workstations but consumed on energy-constrained ARM devices.

Standard symmetric codecs like LZ4 offer excellent performance but fail to exploit the "Write-Once, Read-Many" (WORM) nature of these pipelines. **ZXC** is a lossless codec designed to bridge this gap. By utilizing an **asymmetric compression model**, ZXC achieves a **40% increase in decompression speed on ARM** compared to LZ4, while simultaneously reducing storage footprints. On x86 development architecture, ZXC maintains competitive throughput (>3 GB/s), ensuring no disruption to build pipelines.

## 2. The Efficiency Gap

The industry standard, LZ4, prioritizes symmetric speed (fast compression and fast decompression). While ideal for real-time logs or RAM swapping, this symmetry is inefficient for asset distribution.

*   **Wasted Cycles**: CPU cycles saved during the single compression event (on a build server) do not benefit the millions of end-users decoding the data.
*   **The Battery Tax**: On mobile devices, slower decompression keeps the CPU active longer, draining battery and generating heat.

## 3. The ZXC Solution

ZXC utilizes a computationally intensive encoder to generate a bitstream specifically structured to **maximize decompression throughput**. By performing heavy analysis upfront, the encoder produces a layout optimized for the instruction pipelining and branch prediction capabilities of modern CPUs, particularly ARMv8, effectively offloading complexity from the decoder to the encoder.

### 3.1 Asymmetric Pipeline
ZXC employs a Producer-Consumer architecture to decouple I/O operations from CPU-intensive tasks. This allows for parallel processing where input reading, compression/decompression, and output writing occur simultaneously, effectively hiding I/O latency.

### 3.2 Modular Architecture
The ZXC file format is inherently modular. Each block is independent and can be encoded and decoded using the algorithm best suited for that specific data type. This flexibility allows the format to evolve and incorporate new compression strategies without breaking backward compatibility.

## 4. Core Algorithms

ZXC utilizes a hybrid approach combining LZ77 (Lempel-Ziv) dictionary matching with advanced entropy coding and specialized data transforms.

### 4.1 Specialized SIMD Acceleration & Hardware Hashing
ZXC leverages modern instruction sets to maximize throughput on both ARM and x86 architectures.

### 4.1 LZ77 Engine
The heart of ZXC is a heavily optimized LZ77 engine that adapts its behavior based on the requested compression level:
*   **Hash Chain & Collision Resolution**: Uses a fast hash table with chaining to find matches in the history window (64KB sliding window).
*   **Lazy Matching**: Implements a "lookahead" strategy to find better matches at the cost of slight encoding speed, significantly improving decompression density.

### 4.2 Specialized SIMD Acceleration & Hardware Hashing
ZXC leverages modern instruction sets to maximize throughput on both ARM and x86 architectures.
* **ARM NEON Optimization**: Extensive usage of vld1q_u8 (vector load) and vceqq_u8 (parallel comparison) allows scanning data at wire speed, while vminvq_u8 provides fast rejection of non-matches.
* **x86 Vectorization**: Maintains high performance on Intel/AMD platforms via dedicated AVX2 and AVX512 paths (falling back to SSE4.1 on older hardware), ensuring parity with ARM throughput.
* **Hardware-Accelerated Indexing**: The encoder's hash table mechanism utilizes hardware CRC32c instructions (__crc32cw on ARM, _mm_crc32_u64 on x86) when available, reducing CPU cycle cost for match finding.
* **High-Speed Integrity**: Block validation relies on XXH3 (64-bit), a modern non-cryptographic hash algorithm that fully exploits vector instructions to verify data integrity without bottlenecking the decompression pipeline.

### 4.3 Entropy Coding & Bitpacking
*   **RLE (Run-Length Encoding)**: Automatically detects runs of identical bytes.
*   **VByte Encoding**: Variable-length integer encoding used for length values >= 15.
*   **Bit-Packing**: Compressed sequences are packed into dedicated streams using minimal bit widths.

## 5. File Format Specification

The ZXC file format is block-based, robust, and designed for parallel processing.

### 5.1 Global Structure
*   **Magic Bytes (4 bytes)**: `0x5A 0x58 0x43 0x30` ("ZXC0").
*   **Version (1 byte)**: Current version is `1`.
*   **Reserved (3 bytes)**: Future use.

### 5.2 Block Header Structure
Each data block consists of a **12-byte** generic header that precedes the specific payload. This header allows the decoder to navigate the stream and identify the processing method required for the next chunk of data.

**Header Format:**

| Offset | Field | Size | Description |
| :--- | :--- | :--- | :--- |
| 0 | `Block Type` | 1 byte | 0=RAW, 1=GNR, 2=NUM |
| 1 | `Flags` | 1 byte | Bit 7 (0x80) = Checksum Present |
| 2 | `Reserved` | 2 bytes | Padding |
| 4 | `Comp Size` | 4 bytes | Compressed payload size |
| 8 | `Raw Size` | 4 bytes | Decompressed size |

If `Flags & 0x80` is set, an **8-byte XXH3-64** checksum follows immediately after the header.

> **Note**: While the format is designed for threaded execution, a single-threaded API is also available for constrained environments or simple integration cases.

### 4.3 Block Encoding & Processing Algorithms

The efficiency of ZXC relies on specialized algorithmic pipelines for each block type.

#### Type 1: GNR (General) - The Workhorse
This format is used for standard data. It employs a **multi-stage encoding pipeline**:

**Encoding Process**:
1.  **LZ77 Parsing**: The encoder iterates through the input using a rolling hash (hardware CRC32/CRC32C) to detect matches.
    *   *Hash Chain*: Collisions are resolved via a chain table to find optimal matches in dense data.
    *   *Lazy Matching*: If a match is found, the encoder checks the next position. If a better match starts there, the current byte is emitted as a literal (deferred matching).
2.  **Tokenization**: Matches are split into three components:
    *   *Literal Length*: Number of raw bytes before the match.
    *   *Match Length*: Duration of the repeated pattern.
    *   *Offset*: Distance back to the pattern start.
3.  **Stream Separation**: These components are routed to separate buffers:
    *   *Literals Buffer*: Raw bytes.
    *   *Tokens Buffer*: Packed `(LitLen << 4) | MatchLen`.
    *   *Offsets Buffer*: 16-bit distances.
    *   *Extras Buffer*: Overflow values for lengths >= 15 (VByte encoded).
4.  **RLE Pass**: The literals buffer is scanned for run-length encoding opportunities (runs of identical bytes). If beneficial (>10% gain), it is compressed in place.
5.  **Final Serialization**: All buffers are concatenated into the payload, preceded by section descriptors.

**Decoding Process**:
1.  **Deserizalization**: The decoder reads the section descriptors to obtain pointers to the start of each stream (Literals, Tokens, Offsets).
2.  **Vertical Execution**: The main loop reads from all three streams simultaneously.
3.  **Wild Copy**:
    *   *Literals*: Copied using unaligned 16-byte SIMD loads/stores (`vld1/vst1` on ARM).
    *   *Matches*: Copied using 16-byte stores. Overlapping matches (e.g., repeating pattern "ABC" for 100 bytes) are handled naturally by the CPU's store forwarding or by specific overlapped-copy primitives.
    *   **Safety**: A "Safe Zone" at the end of the buffer forces a switch to a cautious byte-by-byte loop, allowing the main loop to run without bounds checks.

#### Type 2: NUM (Numeric) - The Specialist
Triggered when data is detected as a dense array of 32-bit integers.

**Encoding Process**:
1.  **Vectorized Delta**: Computes `delta[i] = val[i] - val[i-1]` using SIMD integers (AVX2/NEON).
2.  **ZigZag Transform**: Maps signed deltas to unsigned space: `(d << 1) ^ (d >> 31)`.
3.  **Bit Analysis**: Determines the maximum number of bits `B` needed to represent the deltas in a 128-value frame.
4.  **Bit-Packing**: Packs 128 integers into `128 * B` bits.

**Decoding Process**:
1.  **Bit-Unpacking**: Unpacks bitstreams back into integers.
2.  **ZigZag Decode**: Reverses the mapping.
3.  **Integration**: Computes the prefix sum (cumulative addition) to restore original values. *Note: ZXC utilizes a 4x unrolled loop here to pipeline the dependency chain.*

### 4.4 Data Integrity
Every block can optionally be protected by a **64-bit XXH3** checksum. 
*   **Algorithm**: XXH3 (XXHash3) is an extremely fast, non-cryptographic hash algorithm.
*   **Credit**: Developed by Yann Collet, XXH3 runs at RAM speed equivalents, ensuring that enabling checksums introduces **zero measurable latency** to the pipeline.

## 5. System Architecture (Threading)

ZXC leverages a threaded **Producer-Consumer** model to saturate modern multi-core CPUs.

### 5.1 Asynchronous Compression Pipeline
1.  **Block Splitting (Main Thread)**: The input file is read and sliced into fixed-size chunks (default 256KB).
2.  **Ring Buffer Submission**: Chunks are placed into a lock-free ring buffer.
3.  **Parallel Compression (Worker Threads)**:
    *   Workers pull chunks from the queue.
    *   Each worker compresses its chunk independently in its own context (`zxc_cctx_t`).
    *   Output is written to a thread-local buffer.
4.  **Reordering & Write (Writer Thread)**: The writer thread ensures chunks are written to disk in the correct original order, regardless of which worker finished first.

### 5.2 Asynchronous Decompression Pipeline
1.  **Header Parsing (Main Thread)**: The main thread scans block headers to identify boundaries and payload sizes.
2.  **Dispatch**: Compressed payloads are fed into the worker job queue.
3.  **Parallel Decoding (Worker Threads)**:
    *   Workers decode chunks into pre-allocated output buffers.
    *   **Fast Path**: If the output buffer has sufficient margin, the decoder uses "wild copies" (16-byte SIMD stores) to bypass bounds checking for maximal speed.
4.  **Serialization**: Decompressed blocks are committed to the output stream sequentially.

## 6. Performance Analysis (Benchmarks)

**Methodology:**
Benchmarks were conducted using `lzbench` (by inikep).
* **Target 1 (Client):** Apple M2 / macOS 15 (Clang 17)
* **Target 2 (Cloud):** Google Axion / Linux (GCC 12)
* **Target 3 (Build):** AMD EPYC 7763 / Linux (GCC 13)

### 6.1 Target ARM64 (Apple Silicon)
| Compressor | Decompression Speed (Ratio vs LZ4) | Compressed Size (Index LZ4=100) (Lower is Better) |
| :--- | :--- | :--- |
| zxc 0.1.0 -2 | 1.57x | 126.91 |
| **zxc 0.1.0 -3** | **1.39x** | **98.43** |
| **zxc 0.1.0 -4** | **1.30x** | **92.62** |
| **zxc 0.1.0 -5** | **1.17x** | **85.94** |
| lz4 1.10.0 --fast -17 | 1.16x | 130.57 |
| lz4 1.10.0 (Ref) | 1.00x | 100.00 |
| lz4hc 1.10.0 -12 | 0.95x | 76.59 |
| snappy 1.2.2 | 0.70x | 100.47 |
| zstd 1.5.7 -1 | 0.35x | 72.59 |

### 6.2 Target: Cloud ARM (Google Axion / Neoverse V2)
| Compressor | Decompression Speed (Ratio vs LZ4) | Compressed Size (Index LZ4=100) (Lower is Better) |
| :--- | :--- | :--- |
| zxc 0.1.0 -2 | 1.51x | 126.91 |
| **zxc 0.1.0 -3** | **1.23x** | **98.43** |
| **zxc 0.1.0 -4** | **1.15x** | **92.62** |
| **zxc 0.1.0 -5** | **1.04x** | **85.94** |
| lz4 1.10.0 --fast -17 | 1.16x | 130.57 |
| lz4 1.10.0 (Ref) | 1.00x | 100.00 |
| lz4hc 1.10.0 -12 | 0.91x | 76.59 |
| snappy 1.2.2 | 0.44x | 100.47 |
| zstd 1.5.7 -1 | 0.33x | 72.59 |

### 6.3 Build Server: x86_64 (AMD EPYC)
| Compressor | Decompression Speed (Ratio vs LZ4) | Compressed Size (Index LZ4=100) (Lower is Better) |
| :--- | :--- | :--- |
| zxc 0.1.0 -2 | 1.24x | 126.91 |
| **zxc 0.1.0 -3** | **1.04x** | **98.43** |
| zxc 0.1.0 -4 | 0.97x | 92.62 |
| zxc 0.1.0 -5 | 0.90x | 85.94 |
| lz4 1.10.0 --fast -17 | 1.16x | 130.57 |
| lz4 1.10.0 (Ref) | 1.00x | 100.00 |
| lz4hc 1.10.0 -12 | 0.98x | 76.59 |
| snappy 1.2.2 | 0.45x | 100.58 |
| zstd 1.5.7 -1 | 0.34x | 72.59 |

### 6.3 Benchmarks Results

**Figure 6.3**: Decompression Throughput & Storage Ratio (Normalized to LZ4)

![Benchmark Graph ARM64](docs/images/benchmark_arm64.png)


#### 6.3.1 ARM64 Architecture (Github Runner macos-15-xlarge)

Benchmarks were conducted using lzbench (from @inikep), compiled with Clang 17.0.0 on macOS Sequoia 15.7.2 (Build 24G325). The reference hardware is an Apple M2 processor (ARM64). All performance metrics reflect single-threaded execution on the standard Silesia Corpus.

| Compressor name         | Compression| Decompress.| Compr. size | Ratio | Filename |
| ---------------         | -----------| -----------| ----------- | ----- | -------- |
| memcpy                  | 50955 MB/s | 52187 MB/s |   211938580 |100.00 | 12 files|
| **zxc 0.1.0 -2**            |   **429 MB/s** |  **7568 MB/s** |   **128031177** | **60.41** | 12 files|
| **zxc 0.1.0 -3**            |   **197 MB/s** |  **6692 MB/s** |    **99295121** | **46.85** | 12 files|
| **zxc 0.1.0 -4**            |   **165 MB/s** |  **5995 MB/s** |    **93431082** | **44.08** | 12 files|
| **zxc 0.1.0 -5**            |  **70.2 MB/s** |  **5363 MB/s** |    **86696245** | **40.91** | 12 files|
| lz4 1.10.0              |   771 MB/s |  4591 MB/s |   100880147 | 47.60 | 12 files|
| lz4 1.10.0 --fast -17   |  1268 MB/s |  5354 MB/s |   131723524 | 62.15 | 12 files|
| lz4hc 1.10.0 -12        |  13.9 MB/s |  4533 MB/s |    77262399 | 36.46 | 12 files|
| zstd 1.5.7 -1           |   639 MB/s |  1614 MB/s |    73229468 | 34.55 | 12 files|
| snappy 1.2.2            |   833 MB/s |  3096 MB/s |   101352257 | 47.82 | 12 files|

#### 6.3.2 x86_64 Architecture (Github Runner ubuntu-latest)

Benchmarks were conducted using lzbench (from @inikep), compiled with GCC 13.3.0 on Linux 64-bits Ubuntu 24.04. The reference hardware is an AMD EPYC 7763 processor (x86_64). All performance metrics reflect single-threaded execution on the standard Silesia Corpus.

| Compressor name         | Compression| Decompress.| Compr. size | Ratio | Filename |
| ---------------         | -----------| -----------| ----------- | ----- | -------- |
| memcpy                  | 19433 MB/s | 19495 MB/s |   211938580 |100.00 | 12 files|
| **zxc 0.1.0 -2**            |   **336 MB/s** |  **4429 MB/s** |   128031177 | **60.41** | 12 files|
| **zxc 0.1.0 -3**            |   **155 MB/s** |  **3573 MB/s** |    99295121 | **46.85** | 12 files|
| **zxc 0.1.0 -4**            |   **137 MB/s** |  **3347 MB/s** |    93431082 | **44.08** | 12 files|
| **zxc 0.1.0 -5**            |  **56.1 MB/s** |  **3133 MB/s** |    86696245 | **40.91** | 12 files|
| lz4 1.10.0              |   597 MB/s |  3658 MB/s |   100880147 | 47.60 | 12 files|
| lz4 1.10.0 --fast -17   |  1057 MB/s |  4158 MB/s |   131723524 | 62.15 | 12 files|
| lz4hc 1.10.0 -12        |  11.3 MB/s |  3489 MB/s |    77262399 | 36.46 | 12 files|
| zstd 1.5.7 -1           |   421 MB/s |  1209 MB/s |    73229468 | 34.55 | 12 files|
| snappy 1.2.2            |   610 MB/s |  1633 MB/s |   101464727 | 47.87 | 12 files|


## 7. Strategic Implementation

ZXC is designed to adapt to various deployment scenarios by selecting the appropriate compression level:

*   **Interactive Media & Gaming (Level 2-3)**:
    Optimized for hard real-time constraints. Ideal for texture streaming and asset loading, offering **~40% faster** load times to minimize latency and frame drops.

*   **Embedded Systems & Firmware (Level 5)**:
    The sweet spot for maximizing storage density on limited flash memory (e.g., Kernel, Initramfs) while ensuring rapid "instant-on" (XIP-like) boot performance.

*   **Data Archival (Level 5)**:
    A high-efficiency alternative for cold storage, providing better compression ratios than LZ4 and significantly faster retrieval speeds than Zstd.

## 8. Conclusion

ZXC redefines asset distribution by prioritizing the end-user experience. Through its asymmetric design and modular architecture, it shifts computational cost to the build pipeline, unlocking unparalleled decompression speeds on ARM devices. This efficiency translates directly into faster load times, reduced battery consumption, and a smoother user experience, making ZXC the definitive choice for modern, high-performance deployment constraints.
