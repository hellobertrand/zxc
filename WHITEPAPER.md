# ZXC: High-Performance Asymmetric Compression for Cross-Platform Deployment

**Subtitle**: Optimizing Storage and Latency in WORM (Write-Once, Read-Many) Architectures
**Version**: 0.2.1
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

* **Type**: 0=RAW, 1=GNR (LZ77), 2=NUM (Integers).
* **Flags**: A bitfield.
Bit 7 (0x80): HAS_CHECKSUM. If this bit is set to 1, an **8-byte XXH3-64** checksum field is present immediately after the Raw Size field, shifting the start of the data payload accordingly.
* **Comp Size**: Size of the data that follows (excluding this 12 or 20-byte header).

> **Note**: While the format is designed for threaded execution, a single-threaded API is also available for constrained environments or simple integration cases.

### 5.3 Specific Header: NUM (Numeric)
(Present immediately after the Block Header and any optional Checksum)

  0               8       10              16
+---------------+-------+---------------+
| Num Values    | Frame | Reserved      |
| (8B)          | (2B)  | (6B)          |
+---------------+-------+---------------+
* **Num Values**: Count of integers encoded.
* **Frame**: Processing window size (currently always 128).

### 5.4 Specific Header: GNR (Generic)
(Present immediately after the Block Header and any optional Checksum)

GNR Header (16 bytes):
+---------------+---------------+-------+-------+-------+-------+
| N Sequences   | N Literals    | Lit   | LitLen| Match | Off   |
| (4B)          | (4B)          | Enc   | Enc   | Enc   | Enc   |
+---------------+---------------+-------+-------+-------+-------+

Followed by 4 "Section Descriptors" (8 bytes each):
+-------------------------------+-------------------------------+
| Compressed Size (4B)          | Raw Size (4B)                 |
+-------------------------------+-------------------------------+
The 4 sections described are, in order:

* **Literals**: The raw bytes.
* **Tokens**: The pairs (Literal Length, Match Length).
* **Offsets**: The match offset distances.
* **Extras**: The additional lengths (VByte) for very large matches.


### 5.5 Block Encoding & Processing Algorithms

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

### 5.6 Data Integrity
Every block can optionally be protected by a **64-bit XXH3** checksum. 
*   **Algorithm**: XXH3 (XXHash3) is an extremely fast, non-cryptographic hash algorithm.
*   **Credit**: Developed by Yann Collet, XXH3 runs at RAM speed equivalents, ensuring that enabling checksums introduces **zero measurable latency** to the pipeline.

## 6. System Architecture (Threading)

ZXC leverages a threaded **Producer-Consumer** model to saturate modern multi-core CPUs.

### 6.1 Asynchronous Compression Pipeline
1.  **Block Splitting (Main Thread)**: The input file is read and sliced into fixed-size chunks (default 256KB).
2.  **Ring Buffer Submission**: Chunks are placed into a lock-free ring buffer.
3.  **Parallel Compression (Worker Threads)**:
    *   Workers pull chunks from the queue.
    *   Each worker compresses its chunk independently in its own context (`zxc_cctx_t`).
    *   Output is written to a thread-local buffer.
4.  **Reordering & Write (Writer Thread)**: The writer thread ensures chunks are written to disk in the correct original order, regardless of which worker finished first.

### 6.2 Asynchronous Decompression Pipeline
1.  **Header Parsing (Main Thread)**: The main thread scans block headers to identify boundaries and payload sizes.
2.  **Dispatch**: Compressed payloads are fed into the worker job queue.
3.  **Parallel Decoding (Worker Threads)**:
    *   Workers decode chunks into pre-allocated output buffers.
    *   **Fast Path**: If the output buffer has sufficient margin, the decoder uses "wild copies" (16-byte SIMD stores) to bypass bounds checking for maximal speed.
4.  **Serialization**: Decompressed blocks are committed to the output stream sequentially.

## 7. Performance Analysis (Benchmarks)

**Methodology:**
Benchmarks were conducted using `lzbench` (by inikep).
* **Target 1 (Client):** Apple M2 / macOS 15 (Clang 17)
* **Target 2 (Cloud):** Google Axion / Linux (GCC 12)
* **Target 3 (Build):** AMD EPYC 7763 / Linux (GCC 13)

### 7.1 Target ARM64 (Apple Silicon)
| Compressor | Decompression Speed (Ratio vs LZ4) | Compressed Size (Index LZ4=100) (Lower is Better) |
| :--- | :--- | :--- |
| zxc 0.2.0 -2 | 1.76x | 126.91 |
| **zxc 0.2.0 -3** | **1.42x** | **98.43** |
| **zxc 0.2.0 -4** | **1.34x** | **92.62** |
| **zxc 0.2.0 -5** | **1.24x** | **85.94** |
| lz4 1.10.0 --fast -17 | 1.17x | 130.57 |
| lz4 1.10.0 (Ref) | 1.00x | 100.00 |
| lz4hc 1.10.0 -12 | 0.94x | 76.59 |
| snappy 1.2.2 | 0.68x | 100.47 |
| zstd 1.5.7 -1 | 0.34x | 72.59 |

### 7.2 Target: Cloud ARM (Google Axion / Neoverse V2)
| Compressor | Decompression Speed (Ratio vs LZ4) | Compressed Size (Index LZ4=100) (Lower is Better) |
| :--- | :--- | :--- |
| zxc 0.2.0 -2 | 1.58x | 126.91 |
| **zxc 0.2.0 -3** | **1.26x** | **98.43** |
| **zxc 0.2.0 -4** | **1.19x** | **92.62** |
| **zxc 0.2.0 -5** | **1.09x** | **85.94** |
| lz4 1.10.0 --fast -17 | 1.16x | 130.57 |
| lz4 1.10.0 (Ref) | 1.00x | 100.00 |
| lz4hc 1.10.0 -12 | 0.90x | 76.59 |
| snappy 1.2.2 | 0.44x | 100.47 |
| zstd 1.5.7 -1 | 0.33x | 72.59 |

### 7.3 Build Server: x86_64 (AMD EPYC)
| Compressor | Decompression Speed (Ratio vs LZ4) | Compressed Size (Index LZ4=100) (Lower is Better) |
| :--- | :--- | :--- |
| zxc 0.2.0 -2 | 1.39x | 126.91 |
| **zxc 0.2.0 -3** | **1.09x** | **98.43** |
| **zxc 0.2.0 -4** | **1.03x** | **92.62** |
| zxc 0.2.0 -5 | 0.99x | 85.94 |
| lz4 1.10.0 --fast -17 | 1.16x | 130.57 |
| lz4 1.10.0 (Ref) | 1.00x | 100.00 |
| lz4hc 1.10.0 -12 | 0.98x | 76.59 |
| snappy 1.2.2 | 0.45x | 100.58 |
| zstd 1.5.7 -1 | 0.34x | 72.59 |

### 7.4 Benchmarks Results

**Figure 6.3**: Decompression Throughput & Storage Ratio (Normalized to LZ4)

![Benchmark Graph ARM64](docs/images/benchmark_arm64-0.2.1.png)


#### 7.4.1 ARM64 Architecture (Apple Silicon)

Benchmarks were conducted using lzbench (from @inikep), compiled with Clang 17.0.0 on macOS Sequoia 15.7.2 (Build 24G325). The reference hardware is an Apple M2 processor (ARM64). All performance metrics reflect single-threaded execution on the standard Silesia Corpus.

| Compressor name         | Compression| Decompress.| Compr. size | Ratio | Filename |
| ---------------         | -----------| -----------| ----------- | ----- | -------- |
| memcpy                  | 49716 MB/s | 49660 MB/s |   211938580 |100.00 | 12 files|
| **zxc 0.2.0 -2**            |   **427 MB/s** |  **8002 MB/s** |   128031177 | **60.41** | 12 files|
| **zxc 0.2.0 -3**            |   **186 MB/s** |  **6475 MB/s** |    99295121 | **46.85** | 12 files|
| **zxc 0.2.0 -4**            |   **164 MB/s** |  **6107 MB/s** |    93431082 | **44.08** | 12 files|
| **zxc 0.2.0 -5**            |  **68.4 MB/s** |  **5644 MB/s** |    86696245 | **40.91** | 12 files|
| lz4 1.10.0              |   766 MB/s |  4553 MB/s |   100880147 | 47.60 | 12 files|
| lz4 1.10.0 --fast -17   |  1263 MB/s |  5335 MB/s |   131723524 | 62.15 | 12 files|
| lz4hc 1.10.0 -12        |  13.5 MB/s |  4276 MB/s |    77262399 | 36.46 | 12 files|
| zstd 1.5.7 -1           |   605 MB/s |  1528 MB/s |    73229468 | 34.55 | 12 files|
| zstd 1.5.7 --fast --1   |   680 MB/s |  2042 MB/s |    86932028 | 41.02 | 12 files|
| snappy 1.2.2            |   830 MB/s |  3075 MB/s |   101352257 | 47.82 | 12 files|


### 7.4.2 ARM64 Architecture (Google Axion)


| Compressor name         | Compression| Decompress.| Compr. size | Ratio | Filename |
| ---------------         | -----------| -----------| ----------- | ----- | -------- |
| memcpy                  | 23009 MB/s | 23218 MB/s |   211938580 |100.00 | 12 files|
| **zxc 0.2.0 -2**            |   **422 MB/s** |  **6581 MB/s** |   128031177 | **60.41** | 12 files|
| **zxc 0.2.0 -3**            |   **194 MB/s** |  **5247 MB/s** |    99295121 | **46.85** | 12 files|
| **zxc 0.2.0 -4**            |   **173 MB/s** |  **4963 MB/s** |    93431082 | **44.08** | 12 files|
| **zxc 0.2.0 -5**            |  **67.6 MB/s** |  **4550 MB/s** |    86696245 | **40.91** | 12 files|
| lz4 1.10.0              |   739 MB/s |  4165 MB/s |   100880147 | 47.60 | 12 files|
| lz4 1.10.0 --fast -17   |  1285 MB/s |  4817 MB/s |   131723524 | 62.15 | 12 files|
| lz4hc 1.10.0 -12        |  12.5 MB/s |  3769 MB/s |    77262399 | 36.46 | 12 files|
| zstd 1.5.7 -1           |   518 MB/s |  1359 MB/s |    73229468 | 34.55 | 12 files|
| snappy 1.2.2            |   741 MB/s |  1828 MB/s |   101352257 | 47.82 | 12 files|


#### 7.4.3 x86_64 Architecture (AMD EPYC)

Benchmarks were conducted using lzbench (from @inikep), compiled with GCC 13.3.0 on Linux 64-bits Ubuntu 24.04. The reference hardware is an AMD EPYC 7763 processor (x86_64). All performance metrics reflect single-threaded execution on the standard Silesia Corpus.

| Compressor name         | Compression| Decompress.| Compr. size | Ratio | Filename |
| ---------------         | -----------| -----------| ----------- | ----- | -------- |
| memcpy                  | 20102 MB/s | 19428 MB/s |   211938580 |100.00 | 12 files|
| **zxc 0.2.0 -2**            |   **332 MB/s** |  **4916 MB/s** |   128031177 | **60.41** | 12 files|
| **zxc 0.2.0 -3**            |   **152 MB/s** |  **3874 MB/s** |    99295121 | **46.85** | 12 files|
| **zxc 0.2.0 -4**            |   **135 MB/s** |  **3671 MB/s** |    93431082 | **44.08** | 12 files|
| **zxc 0.2.0 -5**            |  **56.5 MB/s** |  **3514 MB/s** |    86696245 | **40.91** | 12 files|
| lz4 1.10.0              |   594 MB/s |  3549 MB/s |   100880147 | 47.60 | 12 files|
| lz4 1.10.0 --fast -17   |  1035 MB/s |  4111 MB/s |   131723524 | 62.15 | 12 files|
| lz4hc 1.10.0 -12        |  11.3 MB/s |  3476 MB/s |    77262399 | 36.46 | 12 files|
| zstd 1.5.7 -1           |   408 MB/s |  1196 MB/s |    73229468 | 34.55 | 12 files|
| zstd 1.5.7 --fast --1   |   449 MB/s |  1570 MB/s |    86932028 | 41.02 | 12 files|
| snappy 1.2.2            |   609 MB/s |  1589 MB/s |   101464727 | 47.87 | 12 files|


## 8. Strategic Implementation

ZXC is designed to adapt to various deployment scenarios by selecting the appropriate compression level:

*   **Interactive Media & Gaming (Level 2-3)**:
    Optimized for hard real-time constraints. Ideal for texture streaming and asset loading, offering **~40% faster** load times to minimize latency and frame drops.

*   **Embedded Systems & Firmware (Level 5)**:
    The sweet spot for maximizing storage density on limited flash memory (e.g., Kernel, Initramfs) while ensuring rapid "instant-on" (XIP-like) boot performance.

*   **Data Archival (Level 5)**:
    A high-efficiency alternative for cold storage, providing better compression ratios than LZ4 and significantly faster retrieval speeds than Zstd.

## 9. Conclusion

ZXC redefines asset distribution by prioritizing the end-user experience. Through its asymmetric design and modular architecture, it shifts computational cost to the build pipeline, unlocking unparalleled decompression speeds on ARM devices. This efficiency translates directly into faster load times, reduced battery consumption, and a smoother user experience, making ZXC the definitive choice for modern, high-performance deployment constraints.
