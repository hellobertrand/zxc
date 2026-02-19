# ZXC Compressed File Format (Technical Specification)

**Date**: February 17, 2026
**Format Version**: 4

This document describes the on-disk binary format of a ZXC compressed file.
It is formalizes the current reference implementation of format version **4**.

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
- **Format Version** (`u8`): currently `4`.
- **Chunk Size Code** (`u8`):
  - `0` means default legacy value = 64 units.
  - otherwise actual chunk size = `code * 4096` bytes.
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

Immediately after EOF block header comes the 12-byte file footer.

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

## 10. Summary of Useful Fixed Sizes

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

## 11. Worked Example (Real Hexdump)

This example was produced with the CLI from a 10-byte input (`Hello ZXC\n`) using:

```bash
zxc -z -C -1 sample.txt
```

Generated archive size: **58 bytes**.

### 11.1 Full hexdump

```text
00000000: F5 2E B0 9C 04 40 80 00 00 00 00 00 00 00 F8 F4
00000010: 00 00 00 0A 00 00 00 50 48 65 6C 6C 6F 20 5A 58
00000020: 43 0A 90 BB A1 75 FF 00 00 00 00 00 00 DC 0A 00
00000030: 00 00 00 00 00 00 90 BB A1 75
```

### 11.2 Byte-level decoding

#### A) File Header (offset `0x00`, 16 bytes)

```text
F5 2E B0 9C | 04 | 40 | 80 | 00 00 00 00 00 00 00 | F8 F4
```

- `F5 2E B0 9C` → magic word (LE) = `0x9CB02EF5`.
- `04` → format version 4.
- `40` → chunk-size code 64 (`64 * 4096 = 262144` bytes, i.e. 256 KiB).
- `80` → checksum enabled (`HAS_CHECKSUM=1`, algo id 0).
- next 7 bytes are reserved zeros.
- `F8 F4` → header CRC16.

#### B) Data Block #0 (RAW)

Block header at offset `0x10`:

```text
00 | 00 | 00 | 0A 00 00 00 | 50
```

- type `00` = RAW.
- flags `00`, reserved `00`.
- `comp_size = 0x0000000A = 10` bytes.
- header CRC8 = `0x50`.

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
FF | 00 | 00 | 00 00 00 00 | DC
```

- type `FF` = EOF.
- `comp_size = 0` (mandatory).
- header CRC8 = `0xDC`.

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

### 11.3 Structural view with absolute offsets

```text
0x00..0x0F  File Header (16)
0x10..0x17  RAW Block Header (8)
0x18..0x21  RAW Payload (10)
0x22..0x25  RAW Block Checksum (4)
0x26..0x2D  EOF Block Header (8)
0x2E..0x39  File Footer (12)
```
