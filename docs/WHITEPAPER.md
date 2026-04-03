# ZXC: High-Performance Asymmetric Lossless Compression

**Version**: 0.10.0
**Date**: April 2026
**Author**: Bertrand Lebonnois

---

## 1. Executive Summary

In modern software delivery pipelines—specifically **Mobile Gaming**, **Embedded Systems**, and **FOTA (Firmware Over-The-Air)**—data is typically generated on high-performance x86 workstations but consumed on energy-constrained ARM devices.

Standard industry codecs like LZ4 offer excellent performance but fail to exploit the "Write-Once, Read-Many" (WORM) nature of these pipelines. **ZXC** is a lossless codec designed to bridge this gap. By utilizing an **asymmetric compression model**, ZXC achieves a **>40% increase in decompression speed on ARM** compared to LZ4, while simultaneously reducing storage footprints. On x86 development architecture, ZXC maintains competitive throughput, ensuring no disruption to build pipelines.

## 2. The Efficiency Gap

The industry standard, LZ4, prioritizes symmetric speed (fast compression and fast decompression). While ideal for real-time logs or RAM swapping, this symmetry is useless for asset distribution.

*   **Wasted Cycles**: CPU cycles saved during the single compression event (on a build server) do not benefit the millions of end-users decoding the data.
*   **The Battery Tax**: On mobile devices, slower decompression keeps the CPU active longer, draining battery and generating heat.

## 3. The ZXC Solution

ZXC utilizes a computationally intensive encoder to generate a bitstream specifically structured to **maximize decompression throughput**. By performing heavy analysis upfront, the encoder produces a layout optimized for the instruction pipelining and branch prediction capabilities of modern CPUs, particularly ARMv8, effectively offloading complexity from the decoder to the encoder.

### 3.1 Asymmetric Pipeline
ZXC employs a Producer-Consumer architecture to decouple I/O operations from CPU-intensive tasks. This allows for parallel processing where input reading, compression/decompression, and output writing occur simultaneously, effectively hiding I/O latency.

### 3.2 Modular Architecture
The ZXC file format is inherently modular. **Each block is independent and can be encoded and decoded using the algorithm best suited** for that specific data type. This flexibility allows the format to evolve and incorporate new compression strategies without breaking backward compatibility.

## 4. Core Algorithms

ZXC utilizes a hybrid approach combining LZ77 (Lempel-Ziv) dictionary matching with advanced entropy coding and specialized data transforms.

### 4.1 LZ77 Engine
The heart of ZXC is a heavily optimized LZ77 engine that adapts its behavior based on the requested compression level:
*   **Hash Chain & Collision Resolution**: Uses a fast hash table with chaining to find matches in the history window (configurable sliding window, power-of-2 from 4 KB to 2 MB, default 256 KB).
*   **Lazy Matching**: Implements a "lookahead" strategy to find better matches at the cost of slight encoding speed, significantly improving decompression density.

### 4.2 Specialized SIMD Acceleration & Hardware Hashing
ZXC leverages modern instruction sets to maximize throughput on both ARM and x86 architectures.
* **ARM NEON Optimization**: Extensive usage of vld1q_u8 (vector load) and vceqq_u8 (parallel comparison) allows scanning data at wire speed, while vminvq_u8 provides fast rejection of non-matches.
* **x86 Vectorization**: Maintains high performance on Intel/AMD platforms via dedicated AVX2 and AVX512 paths (falling back to SSE4.1 on older hardware), ensuring parity with ARM throughput.
* **High-Speed Integrity**: Block validation relies on **rapidhash**, a modern non-cryptographic hash algorithm that fully exploits hardware acceleration to verify data integrity without bottlenecking the decompression pipeline.

### 4.3 Entropy Coding & Bitpacking
*   **RLE (Run-Length Encoding)**: Automatically detects runs of identical bytes.
*   **Prefix Varint Encoding**: Variable-length integer encoding (similar to LEB128 but prefix-based) for overflow values.
*   **Bit-Packing**: Compressed sequences are packed into dedicated streams using minimal bit widths.

#### Prefix Varint Format

ZXC uses a **Prefix Varint** encoding for overflow values. Unlike standard VByte (which uses a continuation bit in every byte), Prefix Varint encodes the total length of the integer in the **unary prefix of the first byte**. This allows the decoder to determine the sequence length immediately, enabling branchless or highly predictable decoding without serial dependencies.

**Encoding Scheme:**

| Prefix (Binary) | Total Bytes | Data Bits (1st Byte) | Total Data Bits | Range (Value < X) |
|-----------------|-------------|----------------------|-----------------|-------------------|
| `0xxxxxxx`      | 1           | 7                    | 7               | 128               |
| `10xxxxxx`      | 2           | 6                    | 14 (6+8)        | 16,384            |
| `110xxxxx`      | 3           | 5                    | 21 (5+8+8)      | 2,097,152         |
| `1110xxxx`      | 4           | 4                    | 28 (4+8+8+8)    | 268,435,456       |
| `11110xxx`      | 5           | 3                    | 35 (3+8+8+8+8)  | 34,359,738,368    |

**Example**: Encoding value `300` (binary: `100101100`):
```text
Value 300 > 127 and < 16383 -> Uses 2-byte format (Prefix '10').

Step 1: Low 6 bits
300 & 0x3F = 44 (0x2C, binary 101100)
Byte 1 = Prefix '10' | 101100 = 10101100 (0xAC)

Step 2: Remaining high bits
300 >> 6 = 4 (0x04, binary 00000100)
Byte 2 = 0x04

Result: 0xAC 0x04

Decoding Verification:
Byte 1 (0xAC) & 0x3F = 44
Byte 2 (0x04) << 6   = 256
Total = 256 + 44 = 300
```

## 5. File Format Specification

The ZXC file format is block-based, robust, and designed for parallel processing.

### 5.1 Global Structure (File Header)

The file begins with a **16-byte** header that identifies the format and specifies decompression parameters.

**FILE Header (16 bytes):**

```
  Offset:  0               4       5       6       7                       14      16
           +---------------+-------+-------+-------+-----------------------+-------+
           | Magic Word    | Ver   | Chunk | Flags | Reserved              | CRC   |
           | (4 bytes)     | (1B)  | (1B)  | (1B)  | (7 bytes, must be 0)  | (2B)  |
           +---------------+-------+-------+-------+-----------------------+-------+
```

* **Magic Word (4 bytes)**: `0x9 0xCB 0x02E 0xF5`.
* **Version (1 byte)**: Current version is `5`.
* **Chunk Size Code (1 byte)**: Defines the processing block size using **exponent encoding**:
  - If the value is in `[12, 21]`: block size = `2^value` bytes (4 KB to 2 MB).
    - `12` = 4 KB, `13` = 8 KB, `14` = 16 KB, `15` = 32 KB, `16` = 64 KB, `17` = 128 KB, `18` = 256 KB (default), `19` = 512 KB, `20` = 1 MB, `21` = 2 MB.
  - Legacy value `64` is accepted for backward compatibility (maps to 256 KB).
  - Block sizes must be powers of 2.
* **Flags (1 byte)**: Global configuration flags.
  - **Bit 7 (MSB)**: `HAS_CHECKSUM`. If `1`, checksums are enabled for the stream. Every block will carry a trailing 4-byte checksum, and the footer will contain a global checksum. If `0`, no checksums are present.
  - **Bits 4-6**: Reserved.
  - **Bits 0-3**: Checksum Algorithm ID (e.g., `0` = RapidHash).
* **Reserved (7 bytes)**: Reserved for future use (must be 0).
* **CRC (2 bytes)**: 16-bit Header Checksum. Calculated on the 16-byte header (with CRC bytes set to 0) using `zxc_hash16`.

### 5.2 Block Header Structure
Each data block consists of an **8-byte** generic header that precedes the specific payload. This header allows the decoder to navigate the stream and identify the processing method required for the next chunk of data.

**BLOCK Header (8 bytes):**

```
  Offset:  0       1       2       3                       7       8
          +-------+-------+-------+-----------------------+-------+
          | Type  | Flags | Rsrvd | Comp Size             | CRC   |
          | (1B)  | (1B)  | (1B)  | (4 bytes)             | (1B)  |
          +-------+-------+-------+-----------------------+-------+

  Block Layout:
  [ Header (8B) ] + [ Compressed Payload (Comp Size bytes) ] + [ Optional Checksum (4B) ]

```

**Note**: The Checksum (if enabled in File Header) is **4 bytes** (32-bit), is always located **at the end** of the compressed data, and is calculated **on the compressed payload**.

* **Type**: Block encoding type (0=RAW, 1=GLO, 2=NUM, 3=GHI, 255=EOF).
* **Flags**: Not used for now.
* **Rsrvd**: Reserved for future use (must be 0).
* **Comp Size**: Compressed payload size (excluding header and optional checksum).
* **CRC**: 1-byte Header Checksum (located at the end of the header). Calculated on the 8-byte header (with CRC byte set to 0) using `zxc_hash8`.

> **Note**: The decompressed size is not stored in the block header. It is derived from internal Section Descriptors within the compressed payload (for GLO/GHI blocks), from the NUM header (for NUM blocks), or equals `Comp Size` (for RAW blocks).

> **Note**: While the format is designed for threaded execution, a single-threaded API is also available for constrained environments or simple integration cases.

### 5.3 Specific Header: NUM (Numeric)
(Present immediately after the Block Header)

**NUM Header (16 bytes):**

```
  Offset:  0                               8       10                      16
          +-------------------------------+-------+-------------------------+
          | N Values                      | Frame | Reserved                |
          | (8 bytes)                     | (2B)  | (6 bytes)               |
          +-------------------------------+-------+-------------------------+
```

* **N Values**: Total count of integers encoded in the block.
* **Frame**: Processing window size (currently always 128).
* **Reserved**: Padding for alignment.

### 5.4 Specific Header: GLO (Generic Low)
(Present immediately after the Block Header)

**GLO Header (16 bytes):**

```
  Offset:  0               4               8   9  10  11  12              16
          +---------------+---------------+---+---+---+---+---------------+
          | N Sequences   | N Literals    |Lit|LL |ML |Off| Reserved      |
          | (4 bytes)     | (4 bytes)     |Enc|Enc|Enc|Enc| (4 bytes)     |
          +---------------+---------------+---+---+---+---+---------------+
```

* **N Sequences**: Total count of LZ sequences in the block.
* **N Literals**: Total count of literal bytes.
* **Encoding Types**
  - `Lit Enc`: Literal stream encoding (0=RAW, 1=RLE). **Currently used.**
  - `LL Enc`: Literal lengths encoding. **Reserved for future use** (lengths are packed in tokens).
  - `ML Enc`: Match lengths encoding. **Reserved for future use** (lengths are packed in tokens).
  - `Off Enc`: Offset encoding mode. **Currently used**
    - `0` = 16-bit offsets (2 bytes each, max distance 65535)
    - `1` = 8-bit offsets (1 byte each, max distance 255)
* **Reserved**: Padding for alignment.

**Section Descriptors (4 × 8 bytes = 32 bytes total):**

Each descriptor stores sizes as a packed 64-bit value:

```
  Single Descriptor (8 bytes):
  +-----------------------------------+-----------------------------------+
  | Compressed Size (4 bytes)         | Raw Size (4 bytes)                |
  | (low 32 bits)                     | (high 32 bits)                    |
  +-----------------------------------+-----------------------------------+

  Full Layout (32 bytes):
  Offset:  0               8               16              24              32
          +---------------+---------------+---------------+---------------+
          | Literals Desc | Tokens Desc   | Offsets Desc  | Extras Desc   |
          | (8 bytes)     | (8 bytes)     | (8 bytes)     | (8 bytes)     |
          +---------------+---------------+---------------+---------------+
```

**Section Contents:**

| # | Section     | Description                                           |
|---|-------------|-------------------------------------------------------|
| 0 | **Literals**| Raw bytes to copy, or RLE-compressed if `enc_lit=1`  |
| 1 | **Tokens**  | Packed bytes: `(LiteralLen << 4) \| MatchLen`        |
| 2 | **Offsets** | Match distances: 8-bit if `enc_off=1`, else 16-bit LE |
| 3 | **Extras**  | Prefix Varint overflow values when LitLen or MatchLen ≥ 15   |

**Data Flow Example:**

```
GLO Block Data Layout:
+------------------------------------------------------------------------+
| Literals Stream | Tokens Stream | Offsets Stream | Extras Stream      |
| (desc[0] bytes) | (desc[1] bytes)| (desc[2] bytes)| (desc[3] bytes)   |
+------------------------------------------------------------------------+
       ↓                 ↓                 ↓                 ↓
   Raw bytes      Token parsing      Match lookup      Length overflow
```

**Why Comp Size and Raw Size?**

Each descriptor stores both a compressed and raw size to support secondary encoding of streams:

| Section     | Comp Size            | Raw Size            | Different?           |
|-------------|----------------------|---------------------|----------------------|
| **Literals**| RLE size (if used)   | Original byte count | Yes, if RLE enabled |
| **Tokens**  | Stream size          | Stream size         | No                   |
| **Offsets** | N×1 or N×2 bytes     | N×1 or N×2 bytes    | No (size depends on `enc_off`) |
| **Extras**  | Prefix Varint stream size | Prefix Varint stream size | No                   |

Currently, the **Literals** section uses different sizes when RLE compression is applied (`enc_lit=1`). The **Offsets** section size depends on `enc_off`: N sequences × 1 byte (if `enc_off=1`) or N sequences × 2 bytes (if `enc_off=0`).

> **Design Note**: This format is designed for future extensibility. The dual-size architecture allows adding entropy coding (FSE/ANS) or bitpacking to any stream without breaking backward compatibility.


### 5.5 Specific Header: GHI (Generic High)
(Present immediately after the Block Header)

The **GHI** (Generic High-Velocity) block format is optimized for maximum decompression speed. It uses a **packed 32-bit sequence** format that allows 4-byte aligned reads, reducing memory access latency and enabling efficient SIMD processing.

**GHI Header (16 bytes):**

```
  Offset:  0               4               8   9  10  11  12              16
          +---------------+---------------+---+---+---+---+---------------+
          | N Sequences   | N Literals    |Lit|LL |ML |Off| Reserved      |
          | (4 bytes)     | (4 bytes)     |Enc|Enc|Enc|Enc| (4 bytes)     |
          +---------------+---------------+---+---+---+---+---------------+
```

* **N Sequences**: Total count of LZ sequences in the block.
* **N Literals**: Total count of literal bytes.
* **Encoding Types**
  - `Lit Enc`: Literal stream encoding (0=RAW).
  - `LL Enc`: Reserved for future use.
  - `ML Enc`: Reserved for future use.
  - `Off Enc`: Offset encoding mode:
    - `0` = 16-bit offsets (max distance 65535)
    - `1` = 8-bit offsets (max distance 255, enables smaller sequence packing)
* **Reserved**: Padding for alignment.

**Section Descriptors (3 × 8 bytes = 24 bytes total):**

```
  Full Layout (24 bytes):
  Offset:  0               8               16              24
          +---------------+---------------+---------------+
          | Literals Desc | Sequences Desc| Extras Desc   |
          | (8 bytes)     | (8 bytes)     | (8 bytes)     |
          +---------------+---------------+---------------+
```

**Section Contents:**

| # | Section       | Description                                           |
|---|---------------|-------------------------------------------------------|
| 0 | **Literals**  | Raw bytes to copy                                    |
| 1 | **Sequences** | Packed 32-bit sequences (see format below)           |
| 2 | **Extras**    | Prefix Varint overflow values when LitLen or MatchLen ≥ 255  |

**Packed Sequence Format (32 bits):**

Unlike GLO which uses separate token and offset streams, GHI packs all sequence data into a single 32-bit word for cache-friendly sequential access:

```
  32-bit Sequence Word (Little Endian):
  +--------+--------+------------------+
  |   LL   |   ML   |     Offset       |
  | 8 bits | 8 bits |     16 bits      |
  +--------+--------+------------------+
   [31:24]  [23:16]      [15:0]

  Byte Layout in Memory:
  Offset: 0        1        2        3
         +--------+--------+--------+--------+
         | Off Lo | Off Hi |   ML   |   LL   |
         +--------+--------+--------+--------+
```

* **LL (Literal Length)**: 8 bits (0-254, value 255 triggers Prefix Varint overflow)
* **ML (Match Length - 5)**: 8 bits (actual length = ML + 5, range 5-259, value 255 triggers Prefix Varint overflow)
* **Offset**: 16 bits (match distance, 1-65535)

**Data Flow Example:**

```
GHI Block Data Layout:
+------------------------------------------------------------+
| Literals Stream | Sequences Stream       | Extras Stream   |
| (desc[0] bytes) | (desc[1] bytes = N×4)  | (desc[2] bytes) |
+------------------------------------------------------------+
       ↓                    ↓                      ↓
   Raw bytes        32-bit seq read         Length overflow
```

**Key Differences: GLO vs GHI**

| Feature            | GLO (Global)                    | GHI (High-Velocity)              |
|--------------------|---------------------------------|----------------------------------|
| **Sections**       | 4 (Lit, Tokens, Offsets, Extras)| 3 (Lit, Sequences, Extras)       |
| **Sequence Format**| 1-byte token + separate offset  | Packed 32-bit word               |
| **LL/ML Bits**     | 4 bits each (overflow at 15)    | 8 bits each (overflow at 255)    |
| **Memory Access**  | Multiple stream pointers        | Single aligned 4-byte reads      |
| **Decoder Speed**  | Fast                            | Fastest (optimized for ARM/x86)  |
| **RLE Support**    | Yes (literals)                  | No                               |
| **Best For**       | General data, good compression  | Maximum decode throughput        |

> **Design Rationale**: The 32-bit packed format eliminates pointer chasing between token and offset streams. By reading a single aligned word per sequence, the decoder achieves better cache utilization and enables aggressive loop unrolling (4x) for maximum throughput on modern CPUs.


### 5.6 Specific Header: EOF (End of File)
(Block Type 255)

The **EOF** block marks the end of the ZXC stream. It ensures that the decompressor knows exactly when to stop processing, allowing for robust stream termination even when file size metadata is unavailable or when concatenating streams.

*   **Structure**: Standard 8-byte Block Header.
*   **Flags**:
    *   **Bit 7 (0x80)**: `has_checksum`. If set, implies the **Global Stream Checksum** in the footer is valid and should be verified.
*   **Comp Size**: Unlike other blocks, these **MUST be set to 0**. The decoder enforces strict validation (`Type == EOF` AND `Comp Size == 0`) to prevent processing of malformed termination blocks.
*   **CRC**: 1-byte Header Checksum (located at the end of the header). Calculated on the 8-byte header (with CRC byte set to 0) using `zxc_hash8`.


### 5.7 File Footer
(Present immediately after the EOF Block)

A mandatory **12-byte footer** closes the stream, providing total source size information and the global checksum.

**Footer Structure (12 bytes):**

```
  Offset:  0                               8               12
          +-------------------------------+---------------+
          | Original Source Size          | Global Hash   |
          | (8 bytes)                     | (4 bytes)     |
          +-------------------------------+---------------+
```

*   **Original Source Size** (8 bytes): Total size of the uncompressed data.
*   **Global Hash** (4 bytes): The **Global Stream Checksum**. Valid only if the EOF block has the `has_checksum` flag set (or the decoder context requires it).
    *   **Algorithm**: `Rotation + XOR`.
    *   For each block with a checksum: `global_hash = (global_hash << 1) | (global_hash >> 31); global_hash ^= block_hash;`

### 5.8 Block Encoding & Processing Algorithms

The efficiency of ZXC relies on specialized algorithmic pipelines for each block type.

#### Type 1: GLO (Global)
This format is used for standard data. It employs a **multi-stage encoding pipeline**:

**Encoding Process**:
1.  **LZ77 Parsing**: The encoder iterates through the input using a rolling hash to detect matches.
    *   *Hash Chain*: Collisions are resolved via a chain table to find optimal matches in dense data.
    *   *Lazy Matching*: If a match is found, the encoder checks the next position. If a better match starts there, the current byte is emitted as a literal (deferred matching).
2.  **Tokenization**: Matches are split into three components:
    *   *Literal Length*: Number of raw bytes before the match.
    *   *Match Length*: Duration of the repeated pattern.
    *   *Offset*: Distance back to the pattern start.
3.  **Stream Separation**: These components are routed to separate buffers:
    *   *Literals Buffer*: Raw bytes.
    *   *Tokens Buffer*: Packed `(LitLen << 4) | MatchLen`.
    *   *Offsets Buffer*: Variable-width distances (8-bit or 16-bit, see below).
    *   *Extras Buffer*: Overflow values for lengths >= 15 (Prefix Varint encoded).
    *   *Offset Mode Selection*: The encoder tracks the maximum offset across all sequences. If all offsets are ≤ 255, the 8-bit mode (`enc_off=1`) is selected, saving 1 byte per sequence compared to 16-bit mode.
4.  **RLE Pass**: The literals buffer is scanned for run-length encoding opportunities (runs of identical bytes). If beneficial (>10% gain), it is compressed in place.
5.  **Final Serialization**: All buffers are concatenated into the payload, preceded by section descriptors.

**Decoding Process**:
1.  **Deserizalization**: The decoder reads the section descriptors to obtain pointers to the start of each stream (Literals, Tokens, Offsets).
2.  **Vertical Execution**: The main loop reads from all three streams simultaneously.
3.  **Wild Copy**:
    *   *Literals*: Copied using unaligned 16-byte SIMD loads/stores (`vld1/vst1` on ARM).
    *   *Matches*: Copied using 16-byte stores. Overlapping matches (e.g., repeating pattern "ABC" for 100 bytes) are handled naturally by the CPU's store forwarding or by specific overlapped-copy primitives.
    *   **Safety**: A "Safe Zone" at the end of the buffer forces a switch to a cautious byte-by-byte loop, allowing the main loop to run without bounds checks.

#### Type 3: GHI (High-Velocity)
This format prioritizes decompression throughput over compression ratio. It uses a **unified sequence stream**:

**Encoding Process**:
1.  **LZ77 Parsing**: Same as GLO, with aggressive lazy matching and step skipping for optimal matches.
2.  **Sequence Packing**: Each match is packed into a 32-bit word:
    *   Bits [31:24]: Literal Length (8 bits)
    *   Bits [23:16]: Match Length - 5 (8 bits)
    *   Bits [15:0]: Offset (16 bits)
3.  **Stream Assembly**: Only three streams are generated:
    *   *Literals Buffer*: Raw bytes (no RLE).
    *   *Sequences Buffer*: Packed 32-bit words (4 bytes each).
    *   *Extras Buffer*: Prefix Varint overflow values for lengths >= 255.
4.  **Final Serialization**: Streams are concatenated with 3 section descriptors.

**Decoding Process**:
1.  **Single-Read Loop**: The decoder reads one 32-bit word per sequence, extracting LL, ML, and offset in a single operation.
2.  **4x Unrolled Fast Path**: When sufficient buffer margin exists, the decoder processes 4 sequences per iteration:
    *   Pre-reads 4 sequences into registers
    *   Copies literals and matches with 32-byte SIMD operations
    *   Minimal branching for maximum instruction-level parallelism
3.  **Offset Validation Threshold**: For the first 256 (8-bit mode) or 65536 (16-bit mode) bytes, offsets are validated against written bytes. After this threshold, all offsets are guaranteed valid.
4.  **Wild Copy**: Same 32-byte SIMD copies as GLO, with special handling for overlapping matches (offset < 32).

#### Type 2: NUM (Numeric)
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

### 5.9 Data Integrity
Every compressed block can optionally be protected by a **32-bit checksum** to ensure data reliability.

#### Post-Compression Verification
Unlike traditional codecs that verify the integrity of the original uncompressed data, ZXC calculates checksums on the **compressed** payload.

*   **Zero-Overhead Decompression**: Verifying uncompressed data requires computing a hash over the output *after* decompression, contending for cache and CPU resources with the decompression logic itself. By checksumming the compressed stream, verification happens *during* the read phase, before the data even enters the decoder.
*   **Early Failure Detection**: Corruption is detected before attempting to decompress, preventing potential crashes or buffer overruns in the decoder caused by malformed data.
*   **Reduced Memory Bandwidth**: The checksum is computed over a much smaller dataset (the compressed block), saving significant memory bandwidth.

#### Multi-Algorithm Support
ZXC supports multiple integrity verification algorithms (though currently standardized on rapidhash).

*   **Identified Algorithm (0x00: rapidhash)**: The default algorithm. The 64-bit rapidhash result is folded (XORed) into a 32-bit value to minimize storage overhead while maintaining strong collision resistance for block-level integrity.
*   **Performance First**: By using a modern non-cryptographic hash, ZXC ensures that integrity checks do not bottleneck decompression throughput.

#### Credit
The default `rapidhash` algorithm is based on wyhash and was developed by Nicolas De Carli. It is designed to fully exploit hardware performance while maintaining top-tier mathematical distribution qualities.

## 6. System Architecture (Threading)

ZXC leverages a threaded **Producer-Consumer** model to saturate modern multi-core CPUs.

### 6.1 Asynchronous Compression Pipeline
1.  **Block Splitting (Main Thread)**: The input file is read and sliced into fixed-size chunks (configurable, default 256 KB, power of 2 from 4 KB to 2 MB).
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
Benchmarks were conducted using `lzbench` (by inikep) with the **default block size of 256 KB**, checksums disabled, single-threaded execution, on the standard Silesia Corpus ([silesia.tar](https://github.com/DataCompression/corpus-collection/tree/main/Silesia-Corpus), 202 MB).
* **Target 1 (Client):** Apple M2 / macOS 26 (Clang 21)
* **Target 2 (Cloud):** Google Axion / Linux (GCC 14)
* **Target 3 (Build):** AMD EPYC 9B45 / Linux (GCC 14)

**Figure A**: Decompression Throughput & Storage Ratio (Normalized to LZ4)

![Benchmark Graph ARM64](./images/benchmark_arm64_0.10.0.webp)


### 7.1 Client ARM64 Summary (Apple Silicon M2)

| Compressor | Decompression Speed (Ratio vs LZ4) | Compressed Size (Index LZ4=100) (Lower is Better) |
| :--- | :--- | :--- |
| **zxc 0.10.0 -1** | **2.50x** | **129.94** |
| **zxc 0.10.0 -2** | **2.05x** | **113.65** |
| **zxc 0.10.0 -3** | **1.46x** | **97.40** |
| **zxc 0.10.0 -4** | **1.38x** | **90.73** |
| **zxc 0.10.0 -5** | **1.28x** | **85.45** |
| lz4 1.10.0 --fast -17 | 1.18x | 130.59 |
| lz4 1.10.0 (Ref) | 1.00x | 100.00 |
| lz4hc 1.10.0 -9 | 0.95x | 77.21 |
| lzav 5.7 -1 | 0.81x | 83.92 |
| snappy 1.2.2 | 0.68x | 100.53 |
| zstd 1.5.7 --fast --1 | 0.53x | 86.16 |
| zstd 1.5.7 -1 | 0.38x | 72.56 |
| zlib 1.3.1 -1 | 0.09x | 76.58 |

**Decompression Efficiency (Cycles per Byte @ 3.5 GHz)**

| Compressor.             | Cycles/Byte | Performance vs memcpy (*) |
| ----------------------- | ----------- | --------------------- |
| memcpy                  | 0.066       | 1.00x (baseline)      |
| **zxc 0.10.0 -1**       | **0.293**   | **4.4x**              |
| **zxc 0.10.0 -2**       | **0.357**   | **5.4x**              |
| **zxc 0.10.0 -3**       | **0.503**   | **7.6x**              |
| **zxc 0.10.0 -4**       | **0.531**   | **8.0x**              |
| **zxc 0.10.0 -5**       | **0.570**   | **8.6x**              |
| lz4 1.10.0              | 0.732       | 11.1x                 |
| lz4 1.10.0 --fast -17   | 0.623       | 9.4x                  |
| lz4hc 1.10.0 -9         | 0.773       | 11.7x                 |
| lzav 5.7 -1             | 0.906       | 13.7x                 |
| zstd 1.5.7 -1           | 1.935       | 29.3x                 |
| zstd 1.5.7 --fast --1   | 1.378       | 20.9x                 |
| snappy 1.2.2            | 1.073       | 16.2x                 |
| zlib 1.3.1 -1           | 8.537       | 129x                  |

*Lower is better. Calculated using Apple M2 Performance Core frequency (3.5 GHz).*


### 7.2 Cloud Server Summary (ARM64 / Google Axion Neoverse-V2)

| Compressor | Decompression Speed (Ratio vs LZ4) | Compressed Size (Index LZ4=100) (Lower is Better) |
| :--- | :--- | :--- |
| **zxc 0.10.0 -1** | **2.05x** | **129.94** |
| **zxc 0.10.0 -2** | **1.72x** | **113.65** |
| **zxc 0.10.0 -3** | **1.24x** | **97.40** |
| **zxc 0.10.0 -4** | **1.17x** | **90.73** |
| **zxc 0.10.0 -5** | **1.09x** | **85.45** |
| lz4 1.10.0 --fast -17 | 1.16x | 130.59 |
| lz4 1.10.0 (Ref) | 1.00x | 100.00 |
| lz4hc 1.10.0 -9 | 0.90x | 77.21 |
| lzav 5.7 -1 | 0.65x | 83.92 |
| snappy 1.2.2 | 0.54x | 100.53 |
| zstd 1.5.7 --fast --1 | 0.54x | 86.16 |
| zstd 1.5.7 -1 | 0.39x | 72.56 |
| zlib 1.3.1 -1 | 0.09x | 76.58 |

**Decompression Efficiency (Cycles per Byte @ 2.6 GHz)**

| Compressor.             | Cycles/Byte | Performance vs memcpy (*) |
| ----------------------- | ----------- | --------------------- |
| memcpy                  | 0.114       | 1.00x (baseline)      |
| **zxc 0.10.0 -1**       | **0.298**   | **2.6x**              |
| **zxc 0.10.0 -2**       | **0.356**   | **3.1x**              |
| **zxc 0.10.0 -3**       | **0.495**   | **4.3x**              |
| **zxc 0.10.0 -4**       | **0.522**   | **4.6x**              |
| **zxc 0.10.0 -5**       | **0.561**   | **4.9x**              |
| lz4 1.10.0              | 0.612       | 5.4x                  |
| lz4 1.10.0 --fast -17   | 0.529       | 4.6x                  |
| lz4hc 1.10.0 -9         | 0.678       | 5.9x                  |
| lzav 5.7 -1             | 0.948       | 8.3x                  |
| zstd 1.5.7 -1           | 1.582       | 13.9x                 |
| zstd 1.5.7 --fast --1   | 1.137       | 10.0x                 |
| snappy 1.2.2            | 1.134       | 9.9x                  |
| zlib 1.3.1 -1           | 6.667       | 58.5x                 |

*Lower is better. Calculated using Neoverse-V2 base frequency (2.6 GHz).*


### 7.3 Build Server Summary (x86_64 / AMD EPYC 9B45)

| Compressor | Decompression Speed (Ratio vs LZ4) | Compressed Size (Index LZ4=100) (Lower is Better) |
| :--- | :--- | :--- |
| **zxc 0.10.0 -1** | **2.03x** | **129.94** |
| **zxc 0.10.0 -2** | **1.81x** | **113.65** |
| **zxc 0.10.0 -3** | **1.16x** | **97.40** |
| **zxc 0.10.0 -4** | **1.10x** | **90.73** |
| **zxc 0.10.0 -5** | **1.04x** | **85.45** |
| lz4 1.10.0 --fast -17 | 1.05x | 130.59 |
| lz4 1.10.0 (Ref) | 1.00x | 100.00 |
| lz4hc 1.10.0 -9 | 0.97x | 77.21 |
| lzav 5.7 -1 | 0.73x | 83.92 |
| snappy 1.2.2 | 0.43x | 100.63 |
| zstd 1.5.7 --fast --1 | 0.49x | 86.16 |
| zstd 1.5.7 -1 | 0.38x | 72.56 |
| zlib 1.3.1 -1 | 0.08x | 76.58 |

**Decompression Efficiency (Cycles per Byte @ 2.1 GHz)**

| Compressor.             | Cycles/Byte | Performance vs memcpy (*) |
| ----------------------- | ----------- | --------------------- |
| memcpy                  | 0.082       | 1.00x (baseline)      |
| **zxc 0.10.0 -1**       | **0.211**   | **2.6x**              |
| **zxc 0.10.0 -2**       | **0.236**   | **2.9x**              |
| **zxc 0.10.0 -3**       | **0.368**   | **4.5x**              |
| **zxc 0.10.0 -4**       | **0.388**   | **4.7x**              |
| **zxc 0.10.0 -5**       | **0.412**   | **5.0x**              |
| lz4 1.10.0              | 0.428       | 5.2x                  |
| lz4 1.10.0 --fast -17   | 0.410       | 5.0x                  |
| lz4hc 1.10.0 -9         | 0.442       | 5.4x                  |
| lzav 5.7 -1             | 0.586       | 7.1x                  |
| zstd 1.5.7 -1           | 1.135       | 13.8x                 |
| zstd 1.5.7 --fast --1   | 0.883       | 10.8x                 |
| snappy 1.2.2            | 1.006       | 12.3x                 |
| zlib 1.3.1 -1           | 5.455       | 66.4x                 |

*Lower is better. Calculated using AMD EPYC 9B45 base frequency (2.1 GHz).*


### 7.4 Benchmarks Results

**Figure B**: Decompression Efficiency : Cycles Per Byte Comparaison

![Benchmark Cycles Per Byte](./images/benchmark_decompression_cycles_0.9.0.webp)


#### 7.4.1 ARM64 Architecture (Apple Silicon M2)

Benchmarks were conducted using lzbench 2.2.1 (from @inikep), compiled with Clang 21.0.0 using *MOREFLAGS="-march=native"* on macOS Tahoe 26.4 (Build 25E246). The reference hardware is an Apple M2 processor (ARM64).

**All performance metrics reflect single-threaded execution on the standard Silesia Corpus and the benchmark made use of [silesia.tar](https://github.com/DataCompression/corpus-collection/tree/main/Silesia-Corpus), which contains tarred files from the Silesia compression corpus.**

| Compressor name         | Compression| Decompress.| Compr. size | Ratio | Filename |
| ---------------         | -----------| -----------| ----------- | ----- | -------- |
| memcpy                  | 52891 MB/s | 52870 MB/s |   211947520 |100.00 | 1 files|
| **zxc 0.10.0 -1**           |   938 MB/s | **11942 MB/s** |   131081262 | **61.85** | 1 files|
| **zxc 0.10.0 -2**           |   600 MB/s |  **9820 MB/s** |   114645515 | **54.09** | 1 files|
| **zxc 0.10.0 -3**           |   240 MB/s |  **6959 MB/s** |    98255455 | **46.36** | 1 files|
| **zxc 0.10.0 -4**           |   169 MB/s |  **6588 MB/s** |    91527983 | **43.18** | 1 files|
| **zxc 0.10.0 -5**           |  96.7 MB/s |  **6144 MB/s** |    86199001 | **40.67** | 1 files|
| lz4 1.10.0              |   812 MB/s |  4782 MB/s |   100880800 | 47.60 | 1 files|
| lz4 1.10.0 --fast -17   |  1352 MB/s |  5623 MB/s |   131732802 | 62.15 | 1 files|
| lz4hc 1.10.0 -9         |  48.1 MB/s |  4529 MB/s |    77884448 | 36.75 | 1 files|
| lzav 5.7 -1             |   663 MB/s |  3864 MB/s |    84644732 | 39.94 | 1 files|
| snappy 1.2.2            |   879 MB/s |  3262 MB/s |   101415443 | 47.85 | 1 files|
| zstd 1.5.7 --fast --1   |   724 MB/s |  2540 MB/s |    86916294 | 41.01 | 1 files|
| zstd 1.5.7 -1           |   645 MB/s |  1809 MB/s |    73193704 | 34.53 | 1 files|
| zlib 1.3.1 -1           |   150 MB/s |   410 MB/s |    77259029 | 36.45 | 1 files|


### 7.4.2 ARM64 Architecture (Google Axion Neoverse-V2)

Benchmarks were conducted using lzbench 2.2.1 (from @inikep), compiled with GCC 14.3.0 using *MOREFLAGS="-march=native"* on Linux 64-bits Debian GNU/Linux 12 (bookworm). The reference hardware is a Google Neoverse-V2 processor (ARM64).

**All performance metrics reflect single-threaded execution on the standard Silesia Corpus and the benchmark made use of [silesia.tar](https://github.com/DataCompression/corpus-collection/tree/main/Silesia-Corpus), which contains tarred files from the Silesia compression corpus.**

| Compressor name         | Compression| Decompress.| Compr. size | Ratio | Filename |
| ---------------         | -----------| -----------| ----------- | ----- | -------- |
| memcpy                  | 23113 MB/s | 22777 MB/s |   211947520 |100.00 | 1 files|
| **zxc 0.10.0 -1**           |   867 MB/s |  **8724 MB/s** |   131081262 | **61.85** | 1 files|
| **zxc 0.10.0 -2**           |   556 MB/s |  **7294 MB/s** |   114645515 | **54.09** | 1 files|
| **zxc 0.10.0 -3**           |   231 MB/s |  **5249 MB/s** |    98255455 | **46.36** | 1 files|
| **zxc 0.10.0 -4**           |   161 MB/s |  **4984 MB/s** |    91527983 | **43.18** | 1 files|
| **zxc 0.10.0 -5**           |  91.8 MB/s |  **4633 MB/s** |    86199001 | **40.67** | 1 files|
| lz4 1.10.0              |   733 MB/s |  4249 MB/s |   100880800 | 47.60 | 1 files|
| lz4 1.10.0 --fast -17   |  1277 MB/s |  4917 MB/s |   131732802 | 62.15 | 1 files|
| lz4hc 1.10.0 -9         |  43.5 MB/s |  3837 MB/s |    77884448 | 36.75 | 1 files|
| lzav 5.7 -1             |   544 MB/s |  2742 MB/s |    84644732 | 39.94 | 1 files|
| snappy 1.2.2            |   757 MB/s |  2293 MB/s |   101415443 | 47.85 | 1 files|
| zstd 1.5.7 --fast --1   |   606 MB/s |  2287 MB/s |    86916294 | 41.01 | 1 files|
| zstd 1.5.7 -1           |   524 MB/s |  1644 MB/s |    73193704 | 34.53 | 1 files|
| zlib 1.3.1 -1           |   114 MB/s |   390 MB/s |    77259029 | 36.45 | 1 files|


#### 7.4.3 x86_64 Architecture (AMD EPYC 9B45)

Benchmarks were conducted using lzbench 2.2.1 (from @inikep), compiled with GCC 14.3.0 using *MOREFLAGS="-march=native"* on Linux 64-bits Ubuntu 24.04. The reference hardware is an AMD EPYC 9B45 processor (x86_64).

**All performance metrics reflect single-threaded execution on the standard Silesia Corpus and the benchmark made use of [silesia.tar](https://github.com/DataCompression/corpus-collection/tree/main/Silesia-Corpus), which contains tarred files from the Silesia compression corpus.**

| Compressor name         | Compression| Decompress.| Compr. size | Ratio | Filename |
| ---------------         | -----------| -----------| ----------- | ----- | -------- |
| memcpy                  | 25522 MB/s | 25555 MB/s |   211947520 |100.00 | 1 files|
| **zxc 0.10.0 -1**           |   827 MB/s |  **9971 MB/s** |   131081262 | **61.85** | 1 files|
| **zxc 0.10.0 -2**           |   508 MB/s |  **8885 MB/s** |   114645515 | **54.09** | 1 files|
| **zxc 0.10.0 -3**           |   210 MB/s |  **5707 MB/s** |    98255455 | **46.36** | 1 files|
| **zxc 0.10.0 -4**           |   149 MB/s |  **5416 MB/s** |    91527983 | **43.18** | 1 files|
| **zxc 0.10.0 -5**           |  87.1 MB/s |  **5093 MB/s** |    86199001 | **40.67** | 1 files|
| lz4 1.10.0              |   770 MB/s |  4902 MB/s |   100880800 | 47.60 | 1 files|
| lz4 1.10.0 --fast -17   |  1260 MB/s |  5127 MB/s |   131732802 | 62.15 | 1 files|
| lz4hc 1.10.0 -9         |  43.6 MB/s |  4751 MB/s |    77884448 | 36.75 | 1 files|
| lzav 5.7 -1             |   604 MB/s |  3582 MB/s |    84644732 | 39.94 | 1 files|
| snappy 1.2.2            |   734 MB/s |  2088 MB/s |   101512076 | 47.89 | 1 files|
| zstd 1.5.7 --fast --1   |   642 MB/s |  2378 MB/s |    86916294 | 41.01 | 1 files|
| zstd 1.5.7 -1           |   588 MB/s |  1851 MB/s |    73193704 | 34.53 | 1 files|
| zlib 1.3.1 -1           |   131 MB/s |   385 MB/s |    77259029 | 36.45 | 1 files|


### 7.5 Block Size Impact: 256 KB vs 512 KB

All benchmarks in sections 7.1–7.4 use the **default block size of 256 KB**. This section evaluates the performance impact of increasing the block size to **512 KB**, measured on three architectures using lzbench 2.2.1 under identical conditions.

**Why block size matters:** Each block starts with a cold hash table, so the LZ77 match-finder has no history and produces more literals until the table warms up. Doubling the block size halves the number of cold-start penalties (~809 blocks to ~405 blocks on the Silesia corpus), improving both compression ratio and decompression throughput.

#### 7.5.1 Apple Silicon M2 (ARM64)

Benchmarked using lzbench 2.2.1, compiled with Clang 21.0.0 using *MOREFLAGS="-march=native"* on macOS Tahoe 26.4 (Build 25E246). Apple M2 processor (ARM64, P-core frequency 3.5 GHz).

**Block Size: 256 KB (default)**

| Compressor name         | Compression| Decompress.| Compr. size | Ratio |
| ---------------         | -----------| -----------| ----------- | ----- |
| memcpy                  | 52891 MB/s | 52870 MB/s |   211947520 |100.00 |
| **zxc 0.10.0 -1**       |   938 MB/s | **11942 MB/s** |   131081262 | **61.85** |
| **zxc 0.10.0 -2**       |   600 MB/s |  **9820 MB/s** |   114645515 | **54.09** |
| **zxc 0.10.0 -3**       |   240 MB/s |  **6959 MB/s** |    98255455 | **46.36** |
| **zxc 0.10.0 -4**       |   169 MB/s |  **6588 MB/s** |    91527983 | **43.18** |
| **zxc 0.10.0 -5**       |  96.7 MB/s |  **6144 MB/s** |    86199001 | **40.67** |

**Block Size: 512 KB**

| Compressor name         | Compression| Decompress.| Compr. size | Ratio |
| ---------------         | -----------| -----------| ----------- | ----- |
| memcpy                  | 52826 MB/s | 52787 MB/s |   211947520 |100.00 |
| **zxc 0.10.0 -1**       |   908 MB/s | **12361 MB/s** |   130969994 | **61.79** |
| **zxc 0.10.0 -2**       |   581 MB/s | **10204 MB/s** |   113859997 | **53.72** |
| **zxc 0.10.0 -3**       |   229 MB/s |  **6964 MB/s** |    97083951 | **45.81** |
| **zxc 0.10.0 -4**       |   163 MB/s |  **6614 MB/s** |    90503394 | **42.70** |
| **zxc 0.10.0 -5**       |  93.3 MB/s |  **6191 MB/s** |    85344194 | **40.27** |

**Decompression Efficiency (Cycles per Byte @ 3.5 GHz)**

| Compressor              | 256 KB (c/B) | 512 KB (c/B) | Δ       |
| ----------------------- | ------------ | ------------ | ------- |
| **zxc 0.10.0 -1**       | **0.293**    | **0.283**    | −3.4%   |
| **zxc 0.10.0 -2**       | **0.356**    | **0.343**    | −3.8%   |
| **zxc 0.10.0 -3**       | **0.503**    | **0.503**    | −0.1%   |
| **zxc 0.10.0 -4**       | **0.531**    | **0.529**    | −0.4%   |
| **zxc 0.10.0 -5**       | **0.570**    | **0.565**    | −0.8%   |

*Lower is better. Consistent improvement across all levels with 512 KB blocks.*

#### 7.5.2 Google Axion (ARM64 Neoverse-V2)

Benchmarked using lzbench 2.2.1, compiled with GCC 14.3.0 using *MOREFLAGS="-march=native"* on Linux Debian 12. Google Neoverse-V2 processor (ARM64, 2.6 GHz).

**Block Size: 256 KB (default)**

| Compressor name         | Compression| Decompress.| Compr. size | Ratio |
| ---------------         | -----------| -----------| ----------- | ----- |
| **zxc 0.10.0 -1**       |   867 MB/s |  **8724 MB/s** |   131081262 | **61.85** |
| **zxc 0.10.0 -2**       |   556 MB/s |  **7294 MB/s** |   114645515 | **54.09** |
| **zxc 0.10.0 -3**       |   231 MB/s |  **5249 MB/s** |    98255455 | **46.36** |
| **zxc 0.10.0 -4**       |   161 MB/s |  **4984 MB/s** |    91527983 | **43.18** |
| **zxc 0.10.0 -5**       |  91.8 MB/s |  **4633 MB/s** |    86199001 | **40.67** |

**Block Size: 512 KB**

| Compressor name         | Compression| Decompress.| Compr. size | Ratio |
| ---------------         | -----------| -----------| ----------- | ----- |
| **zxc 0.10.0 -1**       |   823 MB/s |  **9024 MB/s** |   130969994 | **61.79** |
| **zxc 0.10.0 -2**       |   528 MB/s |  **7349 MB/s** |   113859997 | **53.72** |
| **zxc 0.10.0 -3**       |   220 MB/s |  **5282 MB/s** |    97083951 | **45.81** |
| **zxc 0.10.0 -4**       |   153 MB/s |  **5032 MB/s** |    90503394 | **42.70** |
| **zxc 0.10.0 -5**       |  87.3 MB/s |  **4703 MB/s** |    85344194 | **40.27** |

**Decompression Efficiency (Cycles per Byte @ 2.6 GHz)**

| Compressor              | 256 KB (c/B) | 512 KB (c/B) | Δ       |
| ----------------------- | ------------ | ------------ | ------- |
| **zxc 0.10.0 -1**       | **0.298**    | **0.288**    | −3.3%   |
| **zxc 0.10.0 -2**       | **0.356**    | **0.354**    | −0.7%   |
| **zxc 0.10.0 -3**       | **0.495**    | **0.492**    | −0.6%   |
| **zxc 0.10.0 -4**       | **0.522**    | **0.517**    | −1.0%   |
| **zxc 0.10.0 -5**       | **0.561**    | **0.553**    | −1.5%   |

*Lower is better. Consistent improvement across all levels with 512 KB blocks.*

#### 7.5.3 AMD EPYC 9B45 (x86_64)

Benchmarked using lzbench 2.2.1, compiled with GCC 14.3.0 using *MOREFLAGS="-march=native"* on Linux Ubuntu 24.04. AMD EPYC 9B45 processor (x86_64, 3.5 GHz boost).

**Block Size: 256 KB (default)**

| Compressor name         | Compression| Decompress.| Compr. size | Ratio |
| ---------------         | -----------| -----------| ----------- | ----- |
| **zxc 0.10.0 -1**       |   827 MB/s |  **9907 MB/s** |   131081262 | **61.85** |
| **zxc 0.10.0 -2**       |   508 MB/s |  **8791 MB/s** |   114645515 | **54.09** |
| **zxc 0.10.0 -3**       |   210 MB/s |  **5665 MB/s** |    98255455 | **46.36** |
| **zxc 0.10.0 -4**       |   149 MB/s |  **5353 MB/s** |    91527983 | **43.18** |
| **zxc 0.10.0 -5**       |  87.1 MB/s |  **5100 MB/s** |    86199001 | **40.67** |

**Block Size: 512 KB**

| Compressor name         | Compression| Decompress.| Compr. size | Ratio |
| ---------------         | -----------| -----------| ----------- | ----- |
| **zxc 0.10.0 -1**       |   775 MB/s | **10703 MB/s** |   130969994 | **61.79** |
| **zxc 0.10.0 -2**       |   480 MB/s |  **8927 MB/s** |   113859997 | **53.72** |
| **zxc 0.10.0 -3**       |   198 MB/s |  **5863 MB/s** |    97083951 | **45.81** |
| **zxc 0.10.0 -4**       |   141 MB/s |  **5569 MB/s** |    90503394 | **42.70** |
| **zxc 0.10.0 -5**       |  82.2 MB/s |  **5239 MB/s** |    85344194 | **40.27** |

**Decompression Efficiency (Cycles per Byte @ 3.5 GHz)**

| Compressor              | 256 KB (c/B) | 512 KB (c/B) | Δ       |
| ----------------------- | ------------ | ------------ | ------- |
| **zxc 0.10.0 -1**       | **0.353**    | **0.327**    | −7.4%   |
| **zxc 0.10.0 -2**       | **0.398**    | **0.392**    | −1.5%   |
| **zxc 0.10.0 -3**       | **0.618**    | **0.597**    | −3.4%   |
| **zxc 0.10.0 -4**       | **0.654**    | **0.628**    | −3.9%   |
| **zxc 0.10.0 -5**       | **0.686**    | **0.668**    | −2.7%   |

*Lower is better. Consistent improvement across all levels with 512 KB blocks.*

#### 7.5.4 AMD EPYC 7763 (x86_64)

Benchmarked using lzbench 2.2.1, compiled with GCC 14.2.0 using *MOREFLAGS="-march=native"* on Linux Ubuntu 24.04. AMD EPYC 7763 64-Core processor (x86_64, 2.45 GHz base).

**Block Size: 256 KB (default)**

| Compressor name         | Compression| Decompress.| Compr. size | Ratio |
| ---------------         | -----------| -----------| ----------- | ----- |
| memcpy                  | 21798 MB/s | 21499 MB/s |   211947520 |100.00 |
| **zxc 0.10.0 -1**       |   634 MB/s |  **6790 MB/s** |   131081262 | **61.85** |
| **zxc 0.10.0 -2**       |   405 MB/s |  **5782 MB/s** |   114645515 | **54.09** |
| **zxc 0.10.0 -3**       |   173 MB/s |  **3941 MB/s** |    98255455 | **46.36** |
| **zxc 0.10.0 -4**       |   125 MB/s |  **3785 MB/s** |    91527983 | **43.18** |
| **zxc 0.10.0 -5**       |  73.4 MB/s |  **3626 MB/s** |    86199001 | **40.67** |
| lz4 1.10.0              |   583 MB/s |  2989 MB/s |   100880800 | 47.60 |
| lz4 1.10.0 --fast -17   |  1015 MB/s |  3731 MB/s |   131732802 | 62.15 |
| lz4hc 1.10.0 -9         |  33.5 MB/s |  2819 MB/s |    77884448 | 36.75 |
| lzav 5.7 -1             |   403 MB/s |  2599 MB/s |    84644732 | 39.94 |
| snappy 1.2.2            |   612 MB/s |  1590 MB/s |   101512076 | 47.89 |
| zstd 1.5.7 --fast --1   |   447 MB/s |  1626 MB/s |    86916294 | 41.01 |
| zstd 1.5.7 -1           |   407 MB/s |  1221 MB/s |    73193704 | 34.53 |
| zlib 1.3.1 -1           |  98.1 MB/s |   328 MB/s |    77259029 | 36.45 |

**Block Size: 512 KB**

| Compressor name         | Compression| Decompress.| Compr. size | Ratio |
| ---------------         | -----------| -----------| ----------- | ----- |
| memcpy                  | 23821 MB/s | 23780 MB/s |   211947520 |100.00 |
| **zxc 0.10.0 -1**       |   625 MB/s |  **7151 MB/s** |   130969994 | **61.79** |
| **zxc 0.10.0 -2**       |   402 MB/s |  **6037 MB/s** |   113859997 | **53.72** |
| **zxc 0.10.0 -3**       |   169 MB/s |  **4022 MB/s** |    97083951 | **45.81** |
| **zxc 0.10.0 -4**       |   120 MB/s |  **3868 MB/s** |    90503394 | **42.70** |
| **zxc 0.10.0 -5**       |  71.6 MB/s |  **3708 MB/s** |    85344194 | **40.27** |

**Decompression Efficiency (Cycles per Byte @ 2.45 GHz)**

| Compressor              | 256 KB (c/B) | 512 KB (c/B) | Δ       |
| ----------------------- | ------------ | ------------ | ------- |
| **zxc 0.10.0 -1**       | **0.361**    | **0.343**    | −5.0%   |
| **zxc 0.10.0 -2**       | **0.424**    | **0.406**    | −4.2%   |
| **zxc 0.10.0 -3**       | **0.622**    | **0.609**    | −2.0%   |
| **zxc 0.10.0 -4**       | **0.647**    | **0.633**    | −2.1%   |
| **zxc 0.10.0 -5**       | **0.676**    | **0.661**    | −2.2%   |

*Lower is better. Consistent improvement across all levels with 512 KB blocks.*

#### 7.5.5 Summary: Block Size Trade-offs

**Compression Ratio (Silesia Corpus, 202 MB)**

| Level | 256 KB Ratio | 512 KB Ratio | Δ (pp) | Δ (bytes) |
|:-----:|:------------:|:------------:|:------:|:---------:|
| -1    | 61.85%       | 61.79%       | −0.05  | −111 KB   |
| -2    | 54.09%       | 53.72%       | −0.37  | −767 KB   |
| -3    | 46.36%       | 45.81%       | −0.55  | −1,144 KB |
| -4    | 43.18%       | 42.70%       | −0.48  | −1,001 KB |
| -5    | 40.67%       | 40.27%       | −0.40  | −835 KB   |

**Memory Usage per Compression Context**

| Block Size | Context Memory | Δ vs 256 KB |
|:----------:|:--------------:|:-----------:|
| 256 KB *(default)* | ~1.7 MB  | —           |
| 512 KB             | ~3.3 MB  | +92%        |

> **Guideline:** Use 256 KB (default) for streaming, embedded, or memory-constrained environments. Use 512 KB (`-B 512K`) for bulk compression pipelines and high-throughput servers where memory is not a constraint.


## 8. Compression Ratio Benchmarks

To evaluate compression effectiveness across diverse data distributions, the compressed size is reported as a percentage of the original input for each corpus. All measurements were performed using lzbench 2.2.1 (inikep), compiled with GCC 13.3.0 and *MOREFLAGS="-march=native"* on Linux 64-bit Ubuntu 24.04. The reference platform is an AMD EPYC 7763 (x86_64). ZXC was configured with its default block size of 256KB. Lower values indicate superior compression density.

| Corpus | zxc 0.9.0 -1 | zxc 0.9.0 -2 | zxc 0.9.0 -3 | zxc 0.9.0 -4 | zxc 0.9.0 -5 | lz4 1.10.0 | lz4 1.10.0 --fast -17 | lz4hc 1.10.0 -12 | zstd 1.5.7 -1 | zstd 1.5.7 --fast --1 | Source |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 4SICS-GeekLounge-151020 | 23.66 | 24.74 | 19.76 | 19.73 | 19.39 | 21.82 | 29.02 | 17.54 | 12.62 | 13.64 | [www.netresec.com](https://www.netresec.com/?page=PCAP4SICS) |
| 4SICS-GeekLounge-151022 | 40.73 | 39.84 | 33.21 | 32.85 | 31.94 | 37.35 | 48.38 | 30.15 | 23.51 | 24.34 | [www.netresec.com](https://www.netresec.com/?page=PCAP4SICS) |
| Calgary Large | 80.07 | 62.03 | 48.51 | 44.88 | 42.83 | 51.97 | 74.37 | 38.38 | 35.80 | 46.49 | [data-compression.info](https://www.data-compression.info/Corpora/CalgaryCorpus/) |
| Canterbury | 59.45 | 52.64 | 38.71 | 35.21 | 33.69 | 43.73 | 58.12 | 33.27 | 24.43 | 31.42 | [corpus.canterbury.ac.nz](https://corpus.canterbury.ac.nz/) |
| Canterbury Artificial | 34.64 | 34.65 | 34.64 | 34.64 | 34.64 | 33.74 | 33.75 | 33.70 | 25.04 | 33.36 | [corpus.canterbury.ac.nz](https://corpus.canterbury.ac.nz/) |
| Canterbury Large | 71.75 | 59.85 | 44.16 | 43.52 | 39.54 | 51.97 | 66.89 | 33.78 | 31.13 | 37.05 | [corpus.canterbury.ac.nz](https://corpus.canterbury.ac.nz/) |
| employees_100KB | 14.70 | 13.33 | 11.20 | 11.20 | 9.42 | 12.70 | 16.96 | 8.51 | 7.97 | 9.52 | [sample.json-format.com](https://sample.json-format.com/) |
| employees_10KB | 29.68 | 28.23 | 21.51 | 21.51 | 18.85 | 22.02 | 30.07 | 16.92 | 14.44 | 17.22 | [sample.json-format.com](https://sample.json-format.com/) |
| employees_500MB | 13.37 | 12.17 | 10.28 | 10.22 | 8.70 | 11.44 | 15.34 | 7.09 | 6.42 | 7.48 | [sample.json-format.com](https://sample.json-format.com/) |
| employees_50MB | 13.36 | 12.17 | 10.27 | 10.21 | 8.69 | 11.43 | 15.36 | 7.09 | 6.42 | 7.48 | [sample.json-format.com](https://sample.json-format.com/) |
| enwik8 | 90.27 | 69.87 | 53.84 | 49.22 | 47.43 | 57.26 | 86.21 | 41.91 | 40.66 | 51.62 | [www.mattmahoney.net](https://www.mattmahoney.net/dc/textdata.html) |
| enwik9 | 78.60 | 61.57 | 46.82 | 43.84 | 42.25 | 50.92 | 76.73 | 37.17 | 35.68 | 45.30 | [www.mattmahoney.net](https://www.mattmahoney.net/dc/textdata.html) |
| Manzini (tar) | 52.68 | 45.06 | 34.17 | 32.60 | 30.94 | 37.47 | 54.61 | 26.49 | 23.91 | 30.66 | [www.unsw.adfa.edu.au](https://people.unipmn.it/manzini/lightweight/corpus) |
| Silesia | 61.53 | 54.10 | 46.35 | 43.27 | 40.60 | 47.60 | 62.15 | 36.46 | 34.55 | 41.02 | [sun.aei.polsl.pl](https://sun.aei.polsl.pl/~sdeor/index.php?page=silesia) |
| Taxi (raw) | 23.02 | 22.27 | 17.95 | 17.81 | 15.40 | 21.20 | 21.56 | 11.37 | 12.17 | 16.38 | [www.kaggle.com](https://www.kaggle.com/datasets/shayanshahid997/yellow-taxi-trip-record-of-january-2024/data?select=yellow_tripdata_2024-01.parquet) |


## 9. Strategic Implementation

ZXC is designed to adapt to various deployment scenarios by selecting the appropriate compression level:

*   **Interactive Media & Gaming (Levels 1-2-3)**:
    Optimized for hard real-time constraints. Ideal for texture streaming and asset loading, offering **~40% faster** load times to minimize latency and frame drops.

*   **Embedded Systems & Firmware (Levels 3-4-5)**:
    The sweet spot for maximizing storage density on limited flash memory (e.g., Kernel, Initramfs) while ensuring rapid "instant-on" (XIP-like) boot performance.

*   **Data Archival (Levels 4-5)**:
    A high-efficiency alternative for cold storage, providing better compression ratios than LZ4 and significantly faster retrieval speeds than Zstd.

## 10. Conclusion

ZXC redefines asset distribution by prioritizing the end-user experience. Through its asymmetric design and modular architecture, it shifts computational cost to the build pipeline, unlocking unparalleled decompression speeds on ARM devices. This efficiency translates directly into faster load times, reduced battery consumption, and a smoother user experience, making ZXC a best choice for modern, high-performance deployment constraints.
