# ZXC Compressed File Format (Technical Specification)

**Date**: March 2026
**Format Version**: 5

This document describes the on-disk binary format of a ZXC compressed file.
It formalizes the current reference implementation of format version **5**.

## 1. Conventions

- **Byte order**: all multi-byte integers are **little-endian**.
- **Unit**: offsets are in bytes, zero-based from the start of each structure.
- **Checksum mode**: enabled globally by a flag in the file header.
- **Block model**: a file is a sequence of blocks terminated by an EOF block, then a footer.

---

## 2. Full File Layout

```text
+----------------------+ 16 bytes
| File Header          |
+----------------------+
| Block #0             |
|  - 8B Block Header   |
|  - Block Payload     |
|  - Optional 4B CRC32 |
+----------------------+
| Block #1             |
|  ...                 |
+----------------------+
| EOF Block            | 8 bytes (type=255, comp_size=0)
+----------------------+
| SEK Block (Optional) | table of contents for random access
+----------------------+
| File Footer          | 12 bytes
+----------------------+
```

---

## 3. File Header (16 bytes)

```text
Offset  Size  Field
0x00    4     Magic Word
0x04    1     Format Version
0x05    1     Chunk Size Code
0x06    1     Flags
0x07    7     Reserved (must be 0)
0x0E    2     Header CRC16
```

### 3.1 Field definitions

- **Magic Word** (`u32`): `0x9CB02EF5`.
- **Format Version** (`u8`): currently `5`.
- **Chunk Size Code** (`u8`):
  - If the value is in the range `[12, 21]`, it is an **exponent**: `block_size = 2^code`.
    - `12` = 4 KB, `13` = 8 KB, ..., `18` = 256 KB (default), ..., `21` = 2 MB.
  - The legacy value `64` (from older encoders) is accepted and maps to 256 KB (default).
  - All other values are rejected (`ZXC_ERROR_BAD_BLOCK_SIZE`).
  - Valid block sizes are powers of 2 in the range **4 KB – 2 MB**.
- **Flags** (`u8`):
  - Bit 7 (`0x80`): `HAS_CHECKSUM`.
  - Bits 0..3: checksum algorithm id (`0` = RapidHash-based folding).
  - Bits 4..6: reserved.
- **Reserved**: 7 bytes set to zero.
- **Header CRC16** (`u16`): computed with `zxc_hash16` on the 16-byte header where bytes `0x0E..0x0F` are zeroed.

---

## 4. Generic Block Container

Each block starts with a fixed 8-byte block header.

```text
Offset  Size  Field
0x00    1     Block Type
0x01    1     Block Flags
0x02    1     Reserved
0x03    4     Compressed Payload Size (comp_size)
0x07    1     Header CRC8
```

### 4.1 Header semantics

- **Block Type**:
  - `0` = RAW
  - `1` = GLO
  - `2` = NUM
  - `3` = GHI
  - `254` = SEK
  - `255` = EOF
- **Block Flags**: currently not used by implementation (written as `0`).
- **Reserved**: must be 0.
- **comp_size**: payload size in bytes (does **not** include the optional trailing 4-byte block checksum).
- **Header CRC8**: `zxc_hash8` over the 8-byte header with byte `0x07` forced to zero before hashing.

### 4.2 Block physical layout

```text
[8B Block Header] + [comp_size bytes payload] + [optional 4B checksum]
```

When checksums are enabled at file level, each non-EOF block carries one trailing 4-byte checksum of its compressed payload.

---

## 5. Block Types and Payload Formats

## 5.1 RAW block (`type=0`)

Payload is uncompressed data.

```text
Payload = raw bytes
raw_size = comp_size
```

No internal sub-header.

---

## 5.2 NUM block (`type=2`)

Used for numeric data (32-bit integer stream), delta/zigzag + bitpacking.

### NUM payload layout

```text
+--------------------------+
| NUM Header (16 bytes)    |
+--------------------------+
| Frame #0 header (16B)    |
| Frame #0 packed bits     |
+--------------------------+
| Frame #1 header (16B)    |
| Frame #1 packed bits     |
+--------------------------+
| ...                      |
+--------------------------+
```

### NUM Header (16 bytes)

```text
Offset  Size  Field
0x00    8     n_values (u64)
0x08    2     frame_size (u16, currently 128)
0x0A    6     reserved
```

### NUM frame record (repeated)

```text
Offset  Size  Field
0x00    2     nvals in frame (u16)
0x02    2     bits per value (u16)
0x04    8     base/running seed (u64)
0x0C    4     packed_size in bytes (u32)
0x10    ...   packed delta bitstream
```

Notes:
- Values are reconstructed by bit-unpacking, zigzag decode, then prefix accumulation.
- `packed_size` bytes immediately follow each 16-byte frame header.

---

## 5.3 GLO block (`type=1`)

General LZ-style format with separated streams.

### GLO payload layout

```text
+-------------------------------+
| GLO Header (16 bytes)         |
+-------------------------------+
| 4 Section Descriptors (32B)   |
+-------------------------------+
| Literals stream               |
+-------------------------------+
| Tokens stream                 |
+-------------------------------+
| Offsets stream                |
+-------------------------------+
| Extras stream                 |
+-------------------------------+
```

### GLO Header (16 bytes)

```text
Offset  Size  Field
0x00    4     n_sequences (u32)
0x04    4     n_literals (u32)
0x08    1     enc_lit   (0=RAW, 1=RLE)
0x09    1     enc_litlen (reserved)
0x0A    1     enc_mlen   (reserved)
0x0B    1     enc_off    (0=16-bit offsets, 1=8-bit offsets)
0x0C    4     reserved
```

### GLO section descriptors (4 × 8 bytes)

Descriptor format (packed `u64`):
- low 32 bits: compressed size
- high 32 bits: raw size

Section order:
1. Literals
2. Tokens
3. Offsets
4. Extras

### GLO stream content

- **Literals stream**:
  - raw literal bytes, or
  - RLE tokenized if `enc_lit=1`.
- **Tokens stream**:
  - one byte per sequence: `(LL << 4) | ML`.
  - `LL` and `ML` are 4-bit fields.
- **Offsets stream**:
  - `n_sequences × 1` byte if `enc_off=1`, else `n_sequences × 2` bytes LE.
  - Values are **biased**: stored value = `actual_offset - 1`. Decoder adds `+ 1`.
  - This makes `offset == 0` impossible by construction (minimum decoded offset = 1).
- **Extras stream**:
  - Prefix-varint overflow values for token saturations:
    - if `LL == 15`, read varint and add to LL
    - if `ML == 15`, read varint and add to ML
  - actual match length is `ML + 5` (minimum match = 5).

---

## 5.4 GHI block (`type=3`)

High-throughput LZ format with packed 32-bit sequences.

### GHI payload layout

```text
+-------------------------------+
| GHI Header (16 bytes)         |
+-------------------------------+
| 3 Section Descriptors (24B)   |
+-------------------------------+
| Literals stream               |
+-------------------------------+
| Sequences stream (N * 4B)     |
+-------------------------------+
| Extras stream                 |
+-------------------------------+
```

### GHI Header (16 bytes)

Same binary layout as GLO header:
- `n_sequences`, `n_literals`, `enc_lit`, `enc_litlen`, `enc_mlen`, `enc_off`, reserved.

In practice for GHI:
- `enc_lit = 0` (raw literals)
- `enc_off` is metadata (sequence words always store 16-bit offsets)

### GHI section descriptors (3 × 8 bytes)

Section order:
1. Literals
2. Sequences
3. Extras

Each descriptor uses the same packed size encoding as GLO (`u64`: comp32|raw32).

### GHI sequence word format (32 bits)

```text
Bits 31..24 : LL (literal length, 8 bits)
Bits 23..16 : ML (match length minus 5, 8 bits)
Bits 15..0  : Offset - 1 (16 bits, biased; decode: stored + 1)
```

Memory order (little-endian word):

```text
byte0 = offset low
byte1 = offset high
byte2 = ML
byte3 = LL
```

Overflow rules:
- if `LL == 255`, read varint from Extras and add it to LL.
- if `ML == 255`, read varint, then add minimum match (`+5`).
- otherwise decoded match length is `ML + 5`.

---

## 5.5 EOF block (`type=255`)

EOF marks end of block stream.

Constraints:
- block header is present (8 bytes)
- `comp_size` **must be 0**
- no payload
- no per-block trailing checksum

Immediately after EOF block header comes the Optional SEK block, followed by the 12-byte file footer.

---

## 5.6 SEK block (`type=254`)

The **Seek Table** block is an optional block appended between the EOF block and the File Footer. It provides `O(1)` random-access capabilities by recording the compressed size of every block in the archive. Decompressed sizes and block indices are derived from the file header's `block_size` (all blocks are `block_size` except the last, which may be smaller).

**Layout of a SEK Block**:
```text
  Offset             Size    Field
  0x00               8       Block Header (type=254, comp_size=N*4)
  0x08               4       Block 0 Compressed Size (u32 LE)
  0x0C               4       Block 1 Compressed Size (u32 LE)
  ...                ...     ...
  8 + (N-1)*4        4       Block N-1 Compressed Size (u32 LE)
```

**Backward Detection Strategy**:
1. Read the **File Header** (first 16 bytes) → extract `block_size`.
2. Read the **File Footer** (last 12 bytes) → extract `total_decompressed_size`.
3. Derive `num_blocks = ceil(total_decompressed_size / block_size)`.
4. Calculate `seek_block_size = 8 + (N × 4)`.
5. Seek backward by `seek_block_size` bytes from the start of the footer to read the Block Header.
6. Validate `block_type == 254 (SEK)` and `comp_size == N × 4`.

---

## 6. Prefix Varint (Extras stream)

ZXC extras use a prefix-length varint.

Length is encoded in unary form in the high bits of first byte:

- `0xxxxxxx` → 1 byte total
- `10xxxxxx` → 2 bytes total
- `110xxxxx` → 3 bytes total
- `1110xxxx` → 4 bytes total
- `11110xxx` → 5 bytes total

Payload bits from following bytes are concatenated little-endian style (low bits first).

Used by GLO/GHI to carry LL/ML overflows beyond token/sequence inline limits.

---

## 7. Checksums and Integrity

## 7.1 Header checksums

- File header: 16-bit (`zxc_hash16`).
- Block header: 8-bit (`zxc_hash8`).

These protect metadata/navigation fields.

## 7.2 Per-block checksum (optional)

When file header has `HAS_CHECKSUM=1`:
- each data block appends a 4-byte checksum after payload.
- checksum input is **compressed payload bytes only** (not block header).
- algorithm id currently `0` (RapidHash folded to 32-bit).

## 7.3 Global stream hash

A rolling global hash is maintained from per-block checksums in stream order:

```text
global = 0
for each data block checksum b:
    global = ((global << 1) | (global >> 31)) XOR b
```

This value is stored in the file footer (or zeroed when checksum mode is disabled).

---

## 8. File Footer (12 bytes)

Footer is mandatory and placed immediately after EOF block header.

```text
Offset  Size  Field
0x00    8     original_source_size (u64)
0x08    4     global_hash (u32)
```

- **original_source_size**: full uncompressed size of the file.
- **global_hash**:
  - valid when checksum mode is active;
  - set to zero when checksum mode is disabled.

---

## 9. Decoder Validation Checklist (Practical)

1. Validate file header magic/version/CRC16.
2. Parse blocks sequentially:
   - validate block header CRC8,
   - check block bounds using `comp_size`,
   - if enabled, verify trailing block checksum.
3. Decode payload according to block type.
4. On EOF:
   - require `comp_size == 0`,
   - read footer,
   - compare footer `original_source_size` with produced output size,
   - if enabled, compare footer `global_hash` with recomputed rolling hash.

---

## 10. Versioning Policy

### 10.1 Format version field

The format version is a single byte at offset `0x04` of the file header.
A conforming decoder **MUST** reject any file whose version it does not support.

### 10.2 Version bump criteria

| Change class | Version action | Example |
|---|---|---|
| New block type added | **No bump** (forward-compatible) | Adding a hypothetical `GLR` block type |
| New flag bit defined | **No bump** (forward-compatible) | Using a reserved flag bit |
| Existing block encoding changed | **Major bump** | Changing GLO token layout |
| Header/footer layout changed | **Major bump** | Resizing the file header |
| Checksum algorithm changed | **Major bump** | Replacing RapidHash with Komihash |

### 10.3 Compatibility rules

- **Backward compatibility**: a decoder supporting version *N* **MUST** decode all files produced by encoders of version *N*. It **MAY** also accept earlier versions.
- **Forward compatibility**: a decoder encountering an **unknown block type** (not RAW, GLO, NUM, GHI, or EOF) **SHOULD** skip it using `comp_size` to advance past its payload (and optional checksum), rather than rejecting the file outright. This allows older decoders to partially process files from newer encoders that introduce additive block types.
- **Reserved fields**: all reserved bytes and flag bits **MUST** be written as zero by encoders. Decoders **MUST** ignore reserved fields (not reject non-zero values), unless a future version assigns them meaning.

### 10.4 Minimum conforming decoder

A minimal conforming decoder for version 5 **MUST** support:
- File header parsing and CRC16 validation.
- **RAW** blocks (type 0) — passthrough copy.
- **GLO** blocks (type 1) — full LZ decode with extras varint.
- **GHI** blocks (type 3) — full LZ decode with extras varint.
- **EOF** block (type 255) — stream termination.
- File footer validation (source size check).

Support for **NUM** (type 2) and checksum verification is **RECOMMENDED** but not strictly required for a minimal implementation.

---

## 11. Error Handling

### 11.1 Error classes

Decoders **MUST** detect and handle the following error conditions.
The recommended behavior for each class is specified below.

| Error | Detection point | Required behavior |
|---|---|---|
| **Bad magic** | File header, offset 0x00 | Reject immediately. Not a ZXC file. |
| **Unsupported version** | File header, offset 0x04 | Reject immediately. Version not supported. |
| **Header CRC16 mismatch** | File header, offset 0x0E | Reject. Header is corrupt or truncated. |
| **Invalid chunk size code** | File header, offset 0x05 | Reject. Code outside valid range `[12..21]` and not legacy `64`. |
| **Block header CRC8 mismatch** | Block header, offset 0x07 | Reject block. Stream is corrupt. |
| **Unknown block type** | Block header, offset 0x00 | Skip block using `comp_size` (see §10.3), or reject. |
| **Block payload truncated** | During `fread` of `comp_size` bytes | Reject. Unexpected end of stream. |
| **Block checksum mismatch** | Trailing 4-byte checksum | Reject block. Payload is corrupt. |
| **EOF block with non-zero comp_size** | EOF block header | Reject. Malformed EOF marker. |
| **Footer source size mismatch** | File footer, offset 0x00 | Reject. Output size does not match declared original size. |
| **Footer global hash mismatch** | File footer, offset 0x08 | Reject (if checksum mode active). Integrity failure. |
| **Decompressed output exceeds chunk size** | During LZ decode | Reject. Corrupt or malicious payload. |
| **Match offset out of bounds** | During LZ copy | Reject. Offset references data before output start. |
| **Varint exceeds maximum length** | Extras stream | Reject. Overflow or corrupt extras data. |

### 11.2 Severity levels

- **Fatal**: the decoder **MUST** stop processing and report an error. All errors in the table above are fatal by default.
- **Warning**: not currently defined. Future versions may introduce non-fatal conditions (e.g. unknown flag bits set in reserved positions).

### 11.3 Partial output

When a fatal error occurs mid-stream, the decoder **SHOULD**:
1. Stop producing output immediately.
2. Report the specific error condition (see `zxc_error_name` in the reference implementation).
3. Not return partially decompressed data as a valid result.

Buffer-mode decoders **MUST** return a negative error code. Stream-mode decoders **MUST** signal the error and cease writing to the output.

### 11.4 Decoder hardening recommendations

For decoders processing untrusted input (e.g. network data, user uploads):
- Validate **all** header checksums before processing payloads.
- Enforce maximum allocation limits based on `comp_size` and chunk size code.
- Reject files where `comp_size` exceeds `zxc_compress_bound(chunk_size)`.
- Use bounded memory copies — never trust decoded lengths without cross-checking against output buffer capacity.

---

## 12. Summary of Useful Fixed Sizes

- File header: **16** bytes
- Block header: **8** bytes
- Block checksum (optional): **4** bytes
- NUM header: **16** bytes
- GLO header: **16** bytes
- GHI header: **16** bytes
- Section descriptor: **8** bytes
- GLO descriptors total: **32** bytes
- GHI descriptors total: **24** bytes
- File footer: **12** bytes

---

## 13. Worked Example (Real Hexdump)

This example was produced with the CLI from a 10-byte input (`Hello ZXC\n`) using:

```bash
zxc -z -C -1 sample.txt
```

Generated archive size: **58 bytes**.

### 13.1 Full hexdump

```text
00000000: F5 2E B0 9C 05 12 80 00 00 00 00 00 00 00 9E 53
00000010: 00 00 00 0A 00 00 00 69 48 65 6C 6C 6F 20 5A 58
00000020: 43 0A 90 BB A1 75 FF 00 00 00 00 00 00 02 0A 00
00000030: 00 00 00 00 00 00 90 BB A1 75
```

### 13.2 Byte-level decoding

#### A) File Header (offset `0x00`, 16 bytes)

```text
F5 2E B0 9C | 05 | 12 | 80 | 00 00 00 00 00 00 00 | 9E 53
```

- `F5 2E B0 9C` → magic word (LE) = `0x9CB02EF5`.
- `05` → format version 5.
- `12` → chunk-size code 18 (exponent encoding: `2^18 = 262144` bytes, i.e. 256 KiB).
- `80` → checksum enabled (`HAS_CHECKSUM=1`, algo id 0).
- next 7 bytes are reserved zeros.
- `9E 53` → header CRC16.

#### B) Data Block #0 (RAW)

Block header at offset `0x10`:

```text
00 | 00 | 00 | 0A 00 00 00 | 69
```

- type `00` = RAW.
- flags `00`, reserved `00`.
- `comp_size = 0x0000000A = 10` bytes.
- header CRC8 = `0x69`.

Payload at `0x18..0x21` (10 bytes):

```text
48 65 6C 6C 6F 20 5A 58 43 0A
```

ASCII: `Hello ZXC\n`.

Trailing block checksum at `0x22..0x25`:

```text
90 BB A1 75
```

LE value: `0x75A1BB90`.

#### C) EOF Block (offset `0x26`, 8 bytes)

```text
FF | 00 | 00 | 00 00 00 00 | 02
```

- type `FF` = EOF.
- `comp_size = 0` (mandatory).
- header CRC8 = `0x02`.

#### D) File Footer (offset `0x2E`, 12 bytes)

```text
0A 00 00 00 00 00 00 00 | 90 BB A1 75
```

- original source size = `10` bytes.
- global hash = `0x75A1BB90`.

Since there is exactly one data block, the global hash equals that block checksum:

```text
global0 = 0
global1 = rotl1(global0) XOR block_crc = block_crc
```

### 13.3 Structural view with absolute offsets

```text
0x00..0x0F  File Header (16)
0x10..0x17  RAW Block Header (8)
0x18..0x21  RAW Payload (10)
0x22..0x25  RAW Block Checksum (4)
0x26..0x2D  EOF Block Header (8)
0x2E..0x39  File Footer (12)
```
