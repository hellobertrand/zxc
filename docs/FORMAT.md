# ZXC Compressed File Format (Technical Specification)

**Date**: June 2026
**Format Version**: 6

This document describes the on-disk binary format of a ZXC compressed file.
It formalizes the current reference implementation of format version **6**.

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
- **Format Version** (`u8`): currently `6`.
- **Chunk Size Code** (`u8`):
  - The value is an **exponent** in the range `[12, 21]`: `block_size = 2^code`.
    - `12` = 4 KB, `13` = 8 KB, ..., `19` = 512 KB (default), ..., `21` = 2 MB.
  - All other values are rejected (`ZXC_ERROR_BAD_BLOCK_SIZE`).
  - Valid block sizes are powers of 2 in the range **4 KB – 2 MB**.
- **Flags** (`u8`):
  - Bit 7 (`0x80`): `HAS_CHECKSUM`.
  - Bit 6 (`0x40`): `HAS_DICTIONARY` — a pre-trained dictionary is required for decompression.
  - Bits 0..3: checksum algorithm id (`0` = RapidHash-based folding).
  - Bits 4..5: reserved.
- **Reserved / Dictionary ID**: 7 bytes.
  - When `HAS_DICTIONARY` is set: bytes `0x07..0x0A` contain a `dict_id` (`u32` LE), a 32-bit hash of the dictionary content. Bytes `0x0B..0x0D` remain zero.
  - When `HAS_DICTIONARY` is clear: all 7 bytes are zero.
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
  - `2` = GHI
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

## 5.2 GLO block (`type=1`)

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
0x08    1     enc_lit   (0=RAW, 1=RLE, 2=HUFFMAN)
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
  - raw literal bytes if `enc_lit=0`, or
  - RLE tokenized if `enc_lit=1`, or
  - Huffman-coded if `enc_lit=2` (see [§ 5.2.1 Huffman literal section](#521-huffman-literal-section)).
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

### 5.2.1 Huffman literal section

Selected by the encoder only at compression level ≥ 6, only when at least
`ZXC_HUF_MIN_LITERALS = 1024` literals are present, and only when the Huffman
payload is at least ~3 % smaller than the corresponding RAW or RLE encoding of
the same literals. Any block where the heuristic does not pick HUFFMAN keeps
`enc_lit ∈ {0, 1}`.

The Huffman literal section payload is structured as follows:

```text
Offset  Size  Field
0x00    128   Code-length header
              256 × 4-bit code lengths, packed two-per-byte (low nibble first).
              code_len[i] ∈ [0, 8] (0 means symbol absent).
0x80    6     Sub-stream sizes
              s1, s2, s3 as little-endian u16 (size of streams 0, 1, 2 in bytes).
              The size of stream 3 is implied: s4 = total_payload_size - 134 - s1 - s2 - s3.
0x86    var   Stream 0 bit-stream (s1 bytes, LSB-first)
        var   Stream 1 bit-stream (s2 bytes)
        var   Stream 2 bit-stream (s3 bytes)
        var   Stream 3 bit-stream (s4 bytes)
```

Codes are canonical, length-limited at `L = 8`, emitted **LSB-first**.
The `n_literals` value from the GLO header is split into 4 contiguous regions
of size `Q = ceil(n_literals / 4)` (the last region may be shorter), each
encoded into its own bit-stream so that 4 decoders can run in parallel.

The decoder reconstructs the canonical code table from the 128-byte length
header, validates the Kraft equality, and decodes each sub-stream into its
output region. See [WHITEPAPER §5.7](WHITEPAPER.md) for the multi-symbol
2048-entry lookup table strategy used on the decode hot path.

Decoder validation requirements:
- Every code length must satisfy `code_len[i] ≤ 8`.
- At least one symbol must be present (`code_len[i] != 0` for some `i`).
- The Kraft sum `Σ 2^(8 − code_len[i])` over present symbols must equal `2^8`,
  except for the single-present-symbol degenerate case where exactly one
  symbol has `code_len = 1` and the Kraft sum is `2^7`.
- A failure on any of the above results in `ZXC_ERROR_CORRUPT_DATA`.

---

## 5.3 GHI block (`type=2`)

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

## 5.4 EOF block (`type=255`)

EOF marks end of block stream.

Constraints:
- block header is present (8 bytes)
- `comp_size` **must be 0**
- no payload
- no per-block trailing checksum

Immediately after EOF block header comes the Optional SEK block, followed by the 12-byte file footer.

---

## 5.5 SEK block (`type=254`)

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
1. Read the **File Header** (first 16 bytes) -> extract `block_size`.
2. Read the **File Footer** (last 12 bytes) -> extract `total_decompressed_size`.
3. Derive `num_blocks = ceil(total_decompressed_size / block_size)`.
4. Calculate `seek_block_size = 8 + (N × 4)`.
5. Seek backward by `seek_block_size` bytes from the start of the footer to read the Block Header.
6. Validate `block_type == 254 (SEK)` and `comp_size == N × 4`.

---

## 6. Prefix Varint (Extras stream)

ZXC extras use a prefix-length varint.

The length is encoded in unary form in the high bits of the first byte: the
number of leading `1` bits, followed by a terminating `0`, indicates how
many additional payload bytes follow. The scheme generalizes to N bytes
(`11110xxx` = 5, `111110xx` = 6, ...), but the current ZXC spec caps the
encoding at 3 bytes because no legitimate value exceeds 21 bits (see below).

Encodings used:

- `0xxxxxxx` -> 1 byte total (7 bits payload, value < 128)
- `10xxxxxx` -> 2 bytes total (14 bits, value < 16384)
- `110xxxxx` -> 3 bytes total (21 bits, value < 2 MiB)

Payload bits from the following bytes are concatenated little-endian style
(low bits first). Used by GLO/GHI to carry LL/ML overflows beyond
token/sequence inline limits.

**Value bound**: a varint encodes `(LL - MASK)` or `(ML - MASK)`.
Since LL/ML are bounded by `ZXC_BLOCK_SIZE_MAX = 2 MiB` (2^21), every
legitimate varint value is strictly less than 2^21 and therefore fits in
**at most 3 bytes**.

Any prefix indicating a length >= 4 bytes (first byte `>= 0xE0`) is out of
spec for this format version: encoders must never emit such a varint, and
conforming decoders reject it as corrupt input. This caps the varint
surface to the format-defined block size limit and neutralizes
integer-overflow attacks in downstream bounds arithmetic. A future version
of the format that raises `ZXC_BLOCK_SIZE_MAX` would also extend the
accepted prefix lengths.

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

ZXC has **no forward compatibility**: the set of block types and the meaning of
every field are fixed per format version. Any change a decoder must understand —
adding a block type, assigning meaning to a reserved field/flag bit, changing an
encoding, layout, or the checksum algorithm — requires a **version bump**.

| Change class | Version action | Example |
|---|---|---|
| New block type added | **Version bump** (decoders reject unknown types) | Adding a hypothetical `GLR` block type |
| Reserved field/flag bit assigned meaning | **Version bump** | Defining a reserved flag bit |
| Existing block encoding changed | **Version bump** | Changing GLO token layout |
| Header/footer layout changed | **Version bump** | Resizing the file header |
| Checksum algorithm changed | **Version bump** | Replacing RapidHash with Komihash |

### 10.3 Compatibility rules

- **Version compatibility**: a decoder accepts **only** the format version it implements and **MUST** reject any other version with `ZXC_ERROR_BAD_VERSION`. Because block-type numbering and payload formats may change between versions, a decoder **MUST NOT** attempt to interpret an archive whose version byte it does not recognise.
- **Unknown block types**: a decoder **MUST reject** any block whose type is not defined for its format version (`ZXC_ERROR_BAD_BLOCK_TYPE`). The block-type set is fixed per version; introducing a new type is a version bump (decoders do **not** skip unknown blocks — silently advancing past untrusted, unrecognised data is unsafe).
- **Reserved fields**: all reserved bytes and flag bits **MUST** be written as zero by encoders. The current decoder tolerates (ignores) non-zero reserved values — they are covered by the header CRC, so accidental corruption is still caught — but assigning a reserved field any meaning is a **version bump**, never a same-version extension.
- **Defined-but-bounded fields**: where only specific values are defined (e.g. the checksum-algorithm id, currently `0` = RapidHash only), the decoder **rejects** out-of-range values (`ZXC_ERROR_BAD_HEADER`).

### 10.4 Minimum conforming decoder

A minimal conforming decoder for version 6 **MUST** support:
- File header parsing and CRC16 validation.
- **RAW** blocks (type 0) - passthrough copy.
- **GLO** blocks (type 1) - full LZ decode with extras varint.
- **GHI** blocks (type 2) - full LZ decode with extras varint.
- **EOF** block (type 255) - stream termination.
- File footer validation (source size check).

Support for checksum verification is **RECOMMENDED** but not strictly required for a minimal implementation.

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
| **Invalid chunk size code** | File header, offset 0x05 | Reject. Code outside the valid range `[12..21]`. |
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
- Use bounded memory copies - never trust decoded lengths without cross-checking against output buffer capacity.

---

## 12. Pre-Trained Dictionary Support

### 12.1 Overview

A pre-trained dictionary improves compression ratio on small, similar payloads
(e.g. JSON API responses, game assets, structured logs) by prefilling the LZ77
sliding window at the start of each block. The dictionary is an external file
(`.zxd` format) referenced by a 32-bit ID in the ZXC file header.

### 12.2 Mechanism

The dictionary contains raw byte content (max 64 KB, bounded by the 64 KB LZ
sliding window). At compression time, the dictionary is logically prepended to
each block's input, seeding the hash tables so the match finder can reference
dictionary content immediately. At decompression time, the dictionary is
prepended to the output buffer so match copies that reference dictionary bytes
resolve naturally via pointer arithmetic.

Since each block is independent, the dictionary prefill happens per-block.
This preserves O(1) seekable random-access: load the dictionary once, then
decompress any block independently.

### 12.3 File header encoding

When `HAS_DICTIONARY` (flag bit 6) is set, the reserved bytes at offsets
`0x07..0x0A` contain the `dict_id` (`u32` LE). A decoder **MUST**:
1. Verify that a dictionary is provided (`ZXC_ERROR_DICT_REQUIRED` if not).
2. Verify that `zxc_dict_id(dict, dict_size) == header.dict_id`
   (`ZXC_ERROR_DICT_MISMATCH` if not).

Older decoders that do not recognize the `HAS_DICTIONARY` flag will ignore it
(per §10.3: reserved flag bits are ignored). However, blocks compressed with a
dictionary contain match offsets that reference dictionary content; decoding
without the dictionary produces corrupt output. Per-block and global checksums
(when enabled) will detect this corruption.

### 12.4 Dictionary file format (`.zxd`)

Dictionaries are stored as standalone `.zxd` files with the following layout:

```text
Offset  Size  Field
0x00    4     Magic Word (0x9CB0D1C7 LE)
0x04    1     Dictionary format version (currently 1)
0x05    1     Flags (bits 0..3: checksum algorithm id; bits 4..7 reserved)
0x06    2     Content size (u16 LE, max 65535)
0x08    4     dict_id (u32 LE, hash of content)
0x0C    2     Reserved (0)
0x0E    2     Header CRC16 (zxc_hash16, computed with bytes 0x0C-0x0F zeroed)
0x10    N     Dictionary content (raw bytes)
```

- **Magic Word**: `0x9CB0D1C7`. Allows immediate rejection of non-dictionary files.
- **Flags**: bits `0..3` carry the checksum algorithm id (`0` = RapidHash-based folding), matching the ZXC file header flags; bits `4..7` are reserved (must be 0).
- **dict_id**: deterministic 32-bit hash (RapidHash-folded) of the content bytes. Must match the `dict_id` stored in any ZXC file header that references this dictionary.
- **Header CRC16**: `zxc_hash16` checksum of the 16-byte header with bytes `0x0C..0x0F` zeroed before hashing — same method as the ZXC file header.
- **Content**: raw bytes that prefill the LZ77 window. Not compressed.

### 12.5 Dictionary training

The `zxc_train_dict()` function analyzes a corpus of representative samples to
select byte segments that maximize LZ77 match coverage. The most frequently
matched segments are placed at the end of the dictionary so they produce the
shortest offsets (closest to the block start in the virtual window).

### 12.6 Naming convention

The `.zxd` extension is cosmetic — files are identified by the magic word at
offset `0x00`, never by extension. This is a tooling convention, not a format
requirement; it does not affect bytes on the wire. The reference CLI applies it
as follows:

- `zxc --train -o <dir>/ <files>` writes the trained dictionary as
  `<dir>/dictionary_<dict_id>.zxd`, where `<dict_id>` is the lowercase 8-digit
  hex of the dictionary id (e.g. `dictionary_bc46eec1.zxd`). Embedding the id
  keeps the name unique per dictionary and easy to match against the `Dict ID`
  reported by `zxc -l`. With no `-o`, the file is written to the current
  directory; with `-o <file>` it is written there verbatim.
- On **decompression**, a dictionary is **not** auto-located: an archive that
  was compressed with a dictionary must be decompressed by passing that
  dictionary explicitly with `-D`. Without it, decompression fails with
  `ZXC_ERROR_DICT_REQUIRED` (the `dict_id` in the header is still verified
  against the supplied dictionary, yielding `ZXC_ERROR_DICT_MISMATCH` on a
  mismatch).

---

## 13. Summary of Useful Fixed Sizes

- File header: **16** bytes
- Block header: **8** bytes
- Block checksum (optional): **4** bytes
- GLO header: **16** bytes
- GHI header: **16** bytes
- Section descriptor: **8** bytes
- GLO descriptors total: **32** bytes
- GHI descriptors total: **24** bytes
- File footer: **12** bytes
- Dictionary file header (`.zxd`): **16** bytes

**Magic words** — both are little-endian `u32` at offset `0x00` and deliberately share the `0x9CB0...` family prefix, so check the full value (or the file extension) to tell them apart:

| File | Magic (value) | On-disk bytes (LE) |
|------|---------------|--------------------|
| ZXC archive (`.zxc`) | `0x9CB02EF5` | `F5 2E B0 9C` |
| ZXC dictionary (`.zxd`) | `0x9CB0D1C7` | `C7 D1 B0 9C` |

---

## 14. Worked Example (Real Hexdump)

This example was produced with the CLI from a 10-byte input (`Hello ZXC\n`) using:

```bash
zxc -z -C -1 sample.txt
```

Generated archive size: **58 bytes**.

### 14.1 Full hexdump

```text
00000000: F5 2E B0 9C 05 13 80 00 00 00 00 00 00 00 B8 90
00000010: 00 00 00 0A 00 00 00 69 48 65 6C 6C 6F 20 5A 58
00000020: 43 0A 90 BB A1 75 FF 00 00 00 00 00 00 02 0A 00
00000030: 00 00 00 00 00 00 90 BB A1 75
```

### 14.2 Byte-level decoding

#### A) File Header (offset `0x00`, 16 bytes)

```text
F5 2E B0 9C | 06 | 13 | 80 | 00 00 00 00 00 00 00 | FD 3B
```

- `F5 2E B0 9C` -> magic word (LE) = `0x9CB02EF5`.
- `06` -> format version 6.
- `13` -> chunk-size code 19 (exponent encoding: `2^19 = 524288` bytes, i.e. 512 KiB, the default).
- `80` -> checksum enabled (`HAS_CHECKSUM=1`, algo id 0).
- next 7 bytes are reserved zeros.
- `B8 90` -> header CRC16.

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

### 14.3 Structural view with absolute offsets

```text
0x00..0x0F  File Header (16)
0x10..0x17  RAW Block Header (8)
0x18..0x21  RAW Payload (10)
0x22..0x25  RAW Block Checksum (4)
0x26..0x2D  EOF Block Header (8)
0x2E..0x39  File Footer (12)
```

### 14.4 Seekable Variant (with Seek Table)

Same 10-byte input (`Hello ZXC\n`), compressed with seekable mode enabled:

```bash
zxc -z -C -1 -S sample.txt
```

Generated archive size: **70 bytes** (12 bytes larger than the non-seekable variant).

#### Full hexdump

```text
00000000: F5 2E B0 9C 05 13 80 00 00 00 00 00 00 00 B8 90
00000010: 00 00 00 0A 00 00 00 69 48 65 6C 6C 6F 20 5A 58
00000020: 43 0A 90 BB A1 75 FF 00 00 00 00 00 00 02 FE 00
00000030: 00 04 00 00 00 D2 16 00 00 00 0A 00 00 00 00 00
00000040: 00 00 90 BB A1 75
```

#### Byte-level decoding

**A) File Header** (offset `0x00`, 16 bytes) - identical to non-seekable.

**B) Data Block #0 (RAW)** (offset `0x10`, 22 bytes) - identical to non-seekable.

**C) EOF Block** (offset `0x26`, 8 bytes) - identical to non-seekable.

**D) SEK Block** (offset `0x2E`, 12 bytes)

Block header at `0x2E`:

```text
FE | 00 | 00 | 04 00 00 00 | D2
```

- `FE` -> type 254 = SEK (Seek Table).
- flags `00`, reserved `00`.
- `comp_size = 0x00000004 = 4` bytes (one entry x 4 bytes/entry).
- header CRC8 = `0xD2`.

Seek table entry at `0x36`:

```text
16 00 00 00
```

- Entry #0: compressed block size = `0x00000016 = 22` bytes.
  This is the total size of data block #0 including its header (8) + payload (10) + checksum (4) = 22. ✓

**E) File Footer** (offset `0x3A`, 12 bytes)

```text
0A 00 00 00 00 00 00 00 | 90 BB A1 75
```

- original source size = `10` bytes.
- global hash = `0x75A1BB90`.

#### Structural view with absolute offsets

```text
0x00..0x0F  File Header (16)
0x10..0x17  RAW Block Header (8)
0x18..0x21  RAW Payload (10)
0x22..0x25  RAW Block Checksum (4)
0x26..0x2D  EOF Block Header (8)
0x2E..0x35  SEK Block Header (8)    <- seek table
0x36..0x39  SEK Entry #0 (4)        <- comp_size of block #0
0x3A..0x45  File Footer (12)
```

> **Compatibility note**: The SEK block is inserted between the EOF block and the file footer. The footer always remains the **last 12 bytes of the file**, so decoders that locate the footer from the end of the file (e.g. `src + src_size - 12` for buffer APIs, or `fseek(END - 12)` for file APIs) work unchanged with seekable archives. However, **streaming decoders** that read the footer sequentially immediately after the EOF block must be updated to detect and skip the SEK block. In practice, all ZXC decoders since v0.9.0 handle both seekable and non-seekable archives transparently.

---

## 15. Worked Example: Dictionary File (`.zxd` Hexdump)

A minimal dictionary whose content is the 5 ASCII bytes `hello`. Total file size: **21 bytes** (16-byte header + 5-byte content). This is the on-disk form produced by `zxc_dict_save()` (see §12.4).

### 15.1 Full hexdump

```text
00000000: C7 D1 B0 9C 01 00 05 00 17 0F 72 9A 00 00 4A D9
00000010: 68 65 6C 6C 6F
```

### 15.2 Byte-level decoding

#### A) Dictionary Header (offset `0x00`, 16 bytes)

```text
C7 D1 B0 9C | 01 | 00 | 05 00 | 17 0F 72 9A | 00 00 | 4A D9
```

- `C7 D1 B0 9C` -> magic word (LE) = `0x9CB0D1C7` (`.zxd` dictionary).
- `01` -> dictionary format version 1.
- `00` -> flags (bits 0..3 = checksum algorithm id `0` = RapidHash; bits 4..7 reserved).
- `05 00` -> content size (LE) = `5` bytes.
- `17 0F 72 9A` -> `dict_id` (LE) = `0x9A720F17`. Must match the `dict_id` stored in the file header of any `.zxc` archive compressed with this dictionary.
- `00 00` -> reserved.
- `4A D9` -> header CRC16 (LE) = `0xD94A`, computed over the 16-byte header with bytes `0x0C..0x0F` zeroed (same method as the ZXC file header — the CRC is the last 2 bytes of the header).

#### B) Dictionary Content (offset `0x10`, 5 bytes)

```text
68 65 6C 6C 6F
```

ASCII: `hello`. Raw bytes that prefill the LZ77 window — not compressed.

### 15.3 Structural view with absolute offsets

```text
0x00..0x0F  Dictionary Header (16)
0x10..0x14  Dictionary Content (5)
```
