# ZXC Compressed File Format (Technical Specification)

**Date**: September 2026
**Format Version**: 9

This document describes the on-disk binary format of a ZXC compressed file.
It formalizes the current reference implementation of format version **9**.

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
|  - Optional 4B Cksum |
+----------------------+
| Block #1             |
|  ...                 |
+----------------------+
| EOF Block            | 8 bytes (type=255, comp_size=0)
+----------------------+
| SEK Block           | iff HAS_SEEK_TABLE: table of contents for random access
+----------------------+
| File Footer          | 8 bytes, 16 with a digest
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
0x0E    2     Header Checksum
```

### 3.1 Field definitions

- **Magic Word** (`u32`): `0x9CB02EF5`.
- **Format Version** (`u8`): `9`. Any other value is rejected as an unsupported version;
- **Chunk Size Code** (`u8`):
  - The value is an **exponent** in the range `[12, 21]`: `block_size = 2^code`.
    - `12` = 4 KB, `13` = 8 KB, ..., `19` = 512 KB (default), ..., `21` = 2 MB.
  - All other values are rejected as an invalid chunk size code.
  - Valid block sizes are powers of 2 in the range **4 KB – 2 MB**.
- **Flags** (`u8`):
  - Bit 7 (`0x80`): `HAS_CHECKSUM`.
  - Bit 6 (`0x40`): `HAS_DICTIONARY` — a pre-trained dictionary is required for decompression.
  - Bit 5 (`0x20`): `HAS_SEEK_TABLE` — a SEK block (§5.5) sits between the EOF block and the
    footer. Clear, the footer follows the EOF block directly.
  - Bits 0..3: checksum algorithm id (`0` = RapidHash-based folding).
  - Bit 4: reserved.
- **Reserved / Dictionary ID**: 7 bytes.
  - When `HAS_DICTIONARY` is set: bytes `0x07..0x0A` contain a `dict_id` (`u32` LE), a 32-bit hash of the dictionary content. Bytes `0x0B..0x0D` remain zero.
  - When `HAS_DICTIONARY` is clear: all 7 bytes are zero.
- **Header Checksum** (`u16`): the 16-bit header checksum of [7.1](#71-header-checksums), computed over the 16-byte header with bytes `0x0E..0x0F` zeroed.

---

## 4. Generic Block Container

Each block starts with a fixed 8-byte block header.

```text
Offset  Size  Field
0x00    1     Block Type
0x01    1     Block Flags
0x02    1     Reserved
0x03    4     Compressed Payload Size (comp_size)
0x07    1     Header Checksum
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
- **Compressed Payload Size** (`comp_size`): payload size in bytes (does **not** include the optional trailing 4-byte block
  checksum). For a data block (RAW, GLO, GHI) it never exceeds the `block_size` declared in the
  file header: a block that would grow falls back to RAW, whose payload equals its content, so
  `block_size` is reached exactly and never passed. A decoder **MUST** reject a larger value.
  This does not apply to SEK, which is not a data block: its payload is a table of block
  groups (a `u64` anchor and one `u32` size per block) and routinely exceeds `block_size`.
- **Header Checksum**: the 8-bit header checksum of [7.1](#71-header-checksums), computed over the 8-byte header with byte `0x07` zeroed.

### 4.2 Block physical layout

```text
[8B Block Header] + [comp_size bytes payload] + [optional 4B checksum]
```

When checksums are enabled at file level, each data block carries one trailing 4-byte checksum of its decompressed bytes (§7.2).

---

## 5. Block Types and Payload Formats

## 5.1 RAW block (`type=0`)

Payload is uncompressed data.

### RAW payload layout

```text
+-------------------------------+
| Literal bytes (uncompressed)  |
+-------------------------------+
```

No internal sub-header: unlike GLO and GHI, the payload starts at the first
byte. The decoded size is therefore the Compressed Payload Size, and a RAW
block is the only one whose payload reaches that bound exactly.

---

## 5.2 GLO block (`type=1`)

"General LZ, LOw throughput": LZ-style format with separated streams, trading
decode throughput for ratio.

### GLO payload layout

```text
+-------------------------------+
| GLO Header (12 bytes)         |
+-------------------------------+
| Section descriptors (0/4/8B)  |
+-------------------------------+
| Literals section              |
+-------------------------------+
| Tokens section                |
+-------------------------------+
| Offsets section               |
+-------------------------------+
| Extras section (+ slack pad)  |
+-------------------------------+
```

### GLO Header (12 bytes)

```text
Offset  Size  Field
0x00    4     Sequence Count (n_sequences)
0x04    4     Literal Count (n_literals)
0x08    1     Literal Encoding (enc_lit)
0x09    1     Token Encoding (enc_tok)
0x0A    1     Match Length Encoding (enc_mlen)
0x0B    1     Offset Encoding (enc_off)
```

#### GLO header semantics

- **Sequence Count** (`u32`): number of sequences in the block. It sizes the
  token and offset sections, so it cannot be forged against them.
- **Literal Count** (`u32`): number of literal bytes before entropy coding.
- **Literal Encoding** (`u8`): `0` = RAW, `1` = RLE, `2` = HUFFMAN,
  `3` = HUFFMAN_DICT.
- **Token Encoding** (`u8`): `0` = RAW tokens, `2` = HUFFMAN tokens (level 7 only).
- **Match Length Encoding** (`u8`): reserved — match lengths share the token
  byte. It follows the § 10.3 rule for reserved fields: encoders **MUST**
  write `0`, decoders ignore it.
- **Offset Encoding** (`u8`): `0` = 16-bit offsets, `1` = 8-bit offsets.

**Literal Encoding**, **Token Encoding** and **Offset Encoding** are closed value
sets: a GLO decoder **MUST** reject any value outside the ones listed above
rather than fall back to a default. This is not a memory-safety requirement — a
GLO block sizes its offset section and reads it from the same width, so the two
cannot disagree — but it keeps the undefined byte values genuinely free for a
later format version instead of aliasing them onto an existing meaning.

### GLO section descriptors (0, 4 or 8 bytes)

Only the two sizes the header cannot imply are stored, each a `u32` and only
when present, in this order:

| Descriptor | Present when | Meaning |
|---|---|---|
| **Literal Section Size** (`lit_comp`) | Literal Encoding `!= 0` | compressed size of the literal section |
| **Token Section Size** (`tok_comp`) | Token Encoding `== 2` | compressed size of the token section |

Everything else is derived, so it cannot be forged inconsistently:

- literals raw size = `n_literals`; when Literal Encoding is `0` the section
  size is `n_literals` too, which is why no descriptor is written.
- tokens size = `n_sequences` when Token Encoding is `0`.
- offsets size = `n_sequences × (enc_off ? 1 : 2)`.
- **extras size = whatever payload remains** after the three sections above.

Levels 3 to 5 therefore carry **no descriptor at all** (RAW literals, RAW
tokens), level 6 carries 4 bytes and level 7 carries 8.

Because the extras section is sized as the residue and its values are read on
demand, an end pointer sitting past the last real extra is harmless. That is
what lets the slack padding below hide there at no cost in the header.

### Literal section slack (normative)

At least **32 bytes** of payload **MUST** follow the literal section:

```text
comp_size - header - descriptors - lit_comp >= 32
```

Decoders copy literals with an over-reading wild copy, and a RAW literal
section points straight into the caller's buffer, so those bytes are what keeps
the last copy of a block inside the payload. A decoder **MUST** reject a block
that does not satisfy the inequality rather than read past the literals.

The tokens, offsets and extras sections already cover it on any real block --
32 bytes is reached from 16 sequences with 1-byte offsets, 11 with 2-byte ones.
When they do not (a block of very few sequences), the encoder appends zero
padding until they do. The padding falls inside the extras residue, so it needs
no field of its own; its contents are unconstrained and **MUST NOT** be
validated.

The same rule and the same reasoning apply to GHI (§ 5.3), where the sequences
and extras sections play the role of tokens/offsets/extras.

### GLO stream content

- **Literals section**:
  - raw literal bytes when Literal Encoding is `0`, or
  - RLE tokenized when it is `1`, or
  - Huffman-coded when it is `2`
    (see [§ 5.2.1 Huffman literal section](#521-huffman-literal-section)), or
  - Huffman-coded with the dictionary's shared code lengths when it is `3`
    (dictionary-compressed archives only; same section layout, no inline
    lengths header).
- **Tokens section**:
  - one byte per sequence: `(LL << 4) | ML`, `LL` and `ML` being 4-bit fields.
  - when Token Encoding is `0` (all levels ≤ 6), these `n_sequences` bytes are
    stored verbatim and the Tokens section's compressed size equals `n_sequences`.
  - when Token Encoding is `2` (level 7 only), the token bytes are Huffman-coded
    over the token alphabet using the exact § 5.2.1 layout (inline 128-byte
    lengths header included); the section's compressed size is the encoded
    payload size and the decoder expands it back to `n_sequences` bytes.
- **Offsets section**:
  - `n_sequences × 1` byte when Offset Encoding is `1`, else `n_sequences × 2`
    bytes LE.
  - Values are **biased**: stored value = `actual_offset - 1`. Decoder adds `+ 1`.
  - This makes `offset == 0` impossible by construction (minimum decoded offset = 1).
- **Extras section**: prefix-varint overflow values for token saturations, per
  the rules below.

Overflow rules:
- if `LL == 15`, read varint from Extras and add it to LL.
- if `ML == 15`, read varint from Extras and add it to ML.
- otherwise decoded match length is `ML + 5` (minimum match = 5).

### 5.2.1 Huffman literal section

Literal Encoding `2` carries a length-limited **canonical Huffman code** over the
literal bytes. The bits are placed on the wire with the **PivCo layout**
(level-ordered Huffman, after
[Żukowski 2026](https://marcinzukowski.github.io/pivco-huffman/paper-1.0/ph.html)):
the encoding is ordinary Huffman — same code lengths, same code bits, same
size — and PivCo only reorders those bits, grouping them by TREE LEVEL rather
than by symbol so decoding runs data-parallel list merges instead of a serial
bit chain.

```text
Offset  Size  Field
0x00    128   256 x 4-bit code lengths, packed two-per-byte (low nibble first).
              code_len[i] in [0, 11] (0 means symbol absent).
0x80    var   One run per EMITTING node of the canonical code tree, in BFS
              order (parents before children, left before right). Runs are
              LSB-first within bytes and each run is padded to a byte
              boundary. Emitting nodes and run contents are defined below.
```

**Canonical code.** Codes are length-limited at `L = 11` bits (levels ≤ 6
never emit codes longer than 8; level 7 / ULTRA may emit up to 11). Symbols
are ordered by `(code_len, symbol)` and assigned consecutive code values in
that order, starting at 0 and left-shifting when the length increases — the
standard canonical construction. The **code tree** is the binary trie of
these codewords read MSB-first: at depth `d`, a codeword's bit `code_len-1-d`
selects the left (0) or right (1) child. The Kraft equality (validated below)
makes this trie complete: every internal node has exactly two children.

**Emitting nodes and flat subtrees.** Both sides derive, from the code
lengths alone, the set of *flat roots*: an internal node is a flat root iff

1. it is not itself inside another flat subtree (BFS order resolves this:
   parents are classified first), and
2. every leaf below it sits at the same relative depth `D`, and
3. `D >= 2`.

(Completeness of the trie makes condition 2 imply a perfect binary subtree of
`2^D` leaves. Every maximal complete subtree of depth `D >= 2` is a flat
root — a fixed format rule. The reference decoder unpacks the packed codes
directly, with SIMD kernels for `D` in 2..6 and a scalar table lookup for
`D >= 7`, instead of running the level-merge cascade.)

Every internal node that is neither a flat root nor a descendant of one is a
**bitmap node**: its run holds one branch bit per symbol routed through it,
in symbol-sequence order (0 = left child, 1 = right child). A **flat root**'s
run instead holds `D` packed bits per symbol routed through it, in symbol
sequence order: bit `j` (bit 0 first) is the branch taken at relative depth
`j` below the flat root. Strict descendants of a flat root emit **no run at
all** — they are skipped in the BFS enumeration.

**Derived sizes.** There is no stored size field of any kind: the root
handles `n_literals` symbols, the popcount of a bitmap node's bits equals its
right child's symbol count, and a flat root consuming `c` symbols occupies
exactly `ceil(c*D/8)` bytes, so every run length is derived while walking the
BFS order once.

Selection is an encoder policy, not a format rule: the reference encoder
requires level ≥ 6 and at least 1024 literals, prices every candidate
(RAW / RLE / Huffman / shared-table Huffman) as
`J = size + premium(level) * n_decoded_bytes`, and picks the minimum — a
space-speed Lagrangian with a per-level decode-time premium. Before that
pricing, the reference encoder also applies a joint flat/length *nudge* to
the fitted code lengths at levels 6-7 (literals; level 7 also nudges the
token table): it trades a few bytes of code-length optimality for a
flatter tree whose PivCo runs decode faster. The nudged lengths remain an
ordinary canonical, Kraft-exact code — decoders see nothing special.

A second encoder-side policy governs match distances. At levels 1 to 5 the
reference encoder refuses to emit a match closer than 32 bytes, so the
decoder's match copy keeps to its single widest arm. It applies per block and
only where a sampled scan of the block finds few repeats inside that distance:
data built on short repeats — periodic runs, tightly structured records —
keeps them, because banning them there costs size *and* decode time. Levels 6
and 7 keep every distance. Like the nudge, this changes which sequences an
encoder emits, never how they are encoded, so the archive stays an ordinary
GLO/GHI block and any conforming decoder reads it.

A third encoder-side policy re-emits long matches inline. A GLO token holds
the match length in its 4-bit ML field and escapes any match longer than 19
bytes (`ML == 15`) to a varint. At levels 3 to 5 the reference encoder instead
re-emits such a match, up to a per-level cap, as a chain of inline matches of
at most 19 bytes at the same offset (a 24-byte match as 19 + 5, a 38-byte match
as 19 + 19), so the decoder never takes its ML escape. It does this per
block and only when that escape looks poorly predicted: on regular data the
branch is already predictable and splitting would only add sequences for no
decode gain. Levels 1 and 2 (GHI) and levels 6 and 7 keep every escape. Like
the distance floor, this changes which sequences an encoder emits, never how
they are encoded, so the archive stays an ordinary GLO block and any
conforming decoder reads it.

Decoder validation requirements:
- Every code length must satisfy `code_len[i] ≤ 11`.
- At least one symbol must be present (`code_len[i] != 0` for some `i`).
- The Kraft sum `Σ 2^(11 − code_len[i])` over present symbols must equal
  `2^11`, except for the single-present-symbol degenerate case where exactly
  one symbol has `code_len = 1` and the Kraft sum is `2^10`.
- Every node's run (bitmap or flat) must lie within the section payload.
- A popcount that routes symbols to an absent child is a corruption error.
- A failure on any of the above **MUST** be reported as a corrupt-data error.

### 5.2.2 Shared-table Huffman literal section

Literal Encoding `3` is only valid in archives compressed with a dictionary
(`HAS_DICTIONARY` set): the payload is the same Huffman/PivCo section as
§ 5.2.1 with the 128-byte lengths header **omitted** — the code lengths come
from the shared literal
table carried by the `.zxd` dictionary (see § 12.4), validated once when the
dictionary is attached (same rules as § 5.2.1). Decoders **MUST** reject
Literal Encoding `3` sections when no dictionary table is attached. The archive's `dict_id` binds the (content, table) pair, so a
matching table is guaranteed present whenever the dictionary check passed.

The shared table is trained on the corpus' post-LZ literal distribution and
covers only the symbols seen in training; the encoder falls back to a
per-block table (Literal Encoding `2`) or RAW/RLE for any block containing a literal
byte without a code.

The level-7 token section reuses the § 5.2.1 layout (Token Encoding `2`, with the
inline lengths header) over the token byte alphabet.

## 5.3 GHI block (`type=2`)

"General LZ, HIgh throughput": LZ format with packed 32-bit sequences, trading
ratio for decode throughput.

### GHI payload layout

```text
+-------------------------------+
| GHI Header (12 bytes)         |
+-------------------------------+
| Literals section              |
+-------------------------------+
| Sequences stream (N * 4B)     |
+-------------------------------+
| Extras section (+ slack pad)  |
+-------------------------------+
```

### GHI Header (12 bytes)

Same binary layout as the GLO header: Sequence Count, Literal Count, Literal
Encoding, Token Encoding, Match Length Encoding, Offset Encoding.

In practice for GHI:
- **Literal Encoding** is `0` (raw literals), so the literal section size is
  `n_literals`.
- **Token Encoding** and **Match Length Encoding** are written as `0`.
- **Offset Encoding** is written as `0` and **must be ignored on decode**: GHI has
  no offset section, sequence words always store 16-bit offsets, so the field
  bounds nothing.

### GHI section descriptors: none

Every size follows from the header, so GHI writes no descriptor at all:

- literals = `n_literals` (always RAW)
- sequences = `n_sequences × 4`
- extras = the remaining payload, slack padding included

The § 5.2 literal-section slack rule applies here too.

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
- Compressed Payload Size **must be 0**
- no payload
- no per-block trailing checksum

Immediately after EOF block header comes the SEK block when `HAS_SEEK_TABLE=1`, then the file footer (§ 8).

---

## 5.5 SEK block (`type=254`)

The **Seek Table** block sits between the EOF block and the File Footer when, and only when, the file header sets `HAS_SEEK_TABLE`. An empty source still gets one: a bare SEK header, payload size 0. It provides `O(1)` random-access capabilities by recording, in groups, where blocks start and how long each is on disk. Decompressed sizes and block indices are derived from the file header's `block_size` (all blocks are `block_size` except the last, which may be smaller).

**Layout of a SEK Block**:
```text
  Offset             Size    Field
  0x00               8       Block Header (Block Type 254, Compressed Payload Size = table bytes, folded)
  0x08               8       Group 0 anchor: offset of block 0 (u64 LE), always 16
  0x10               4       Block 0 on-disk size (u32 LE)
  0x14               4       Block 1 on-disk size
  ...                4       ... one size per block, 64 per group
  0x110              8       Group 1 anchor: offset of block 64 (u64 LE)
  0x118              4       Block 64 on-disk size
  ...                ...     ...
```

The table is a sequence of **groups** of `K = 64` blocks. A group is its **anchor** - the byte
offset of its first block's header from the start of the archive, as a `u64` - followed by one
`u32` per block: the block's on-disk size (header + payload + optional checksum). Group `j`
starts `j × 264` bytes into the table; only the last group may hold fewer than 64 sizes. Block
`i` starts at `anchor(i / 64)` plus the sizes of the blocks before it in its group, so locating
any block costs one bounded read. The table is `T = ⌈N / 64⌉ × 8 + N × 4` bytes. The header's
32-bit Compressed Payload Size holds `T` **folded**, `(T XOR (T >> 32)) mod 2^32`: `T` itself
below 4 GiB, like every other block's payload size, with all 64 bits taking part above.
Decoders never read the length from it: they derive `N` from the footer and compare the fold
of `T`, so the field does not bound the table.

**The table is not authenticated.** Bounds checks (step 7) reject damaged entries, but a
table rewritten consistently can point a block at another well-formed block of the same
on-disk size, whose bytes the read returns with no error; any seek index checked only against
itself shares this. Only the per-block checksum, seeded with the block's position (§ 7.2),
binds a block to its index.

**Backward Reading**:
1. Read the **File Header** (first 16 bytes) -> extract `block_size`; with `HAS_SEEK_TABLE`
   clear, the archive is not seekable.
2. Read the **File Footer** (last 8 bytes, or 16 with checksums) -> its first 8 bytes are `total_decompressed_size`.
3. Derive `num_blocks = ceil(total_decompressed_size / block_size)`, in 64 bits: no field
   holds `N`, so nothing caps it but the footer's 64-bit size.
4. Calculate `seek_block_size = 8 + ⌈N / 64⌉ × 8 + N × 4`, in 64 bits.
5. Seek backward by `seek_block_size` bytes from the start of the footer to read the Block Header.
6. Validate that Block Type is `254` (SEK) and Compressed Payload Size is the fold of
   `⌈N / 64⌉ × 8 + N × 4`, and that an EOF block header sits 8 bytes before it.
7. Nothing else is read at open. In a well-formed archive anchor + sizes lands exactly on
   the next anchor, or on the EOF block for the last group. A decoder validates a group alone
   when it accesses one of its blocks: anchor 0 is `16`, every anchor lies in
   `[16, EOF block]`, every size in `[8, 8 + block_size + checksum_size]`, the group ends at
   or before the EOF block, the last one exactly on it. It rejects a block whose header
   disagrees with its entry's size, which catches an entry pointing into a block but not one
   moved onto another block of the same size. A block's size is always its own entry, never
   the gap to the next anchor. Checking that anchor is optional, and refuses an intact group
   when it is damaged.

**Sequential Reading**: the file header says what follows the EOF block, so a decoder never
guesses from those bytes, which is unreliable: the footer opens with the source size, and one
size in about 65536 parses as a valid SEK header. With `HAS_SEEK_TABLE=1`, it reads the SEK
block header and rejects it unless it is type `254` with a Compressed Payload Size equal to the
fold of `T = ⌈N / 64⌉ × 8 + N × 4`, `N` derived from the bytes it produced; it then skips `T`
bytes, counted in 64 bits, and reads the footer. With the flag clear, the footer comes next.

---

## 6. Prefix Varint (Extras section)

ZXC extras use a prefix-length varint.

The length is encoded in unary form in the high bits of the first byte: the
number of leading `1` bits, followed by a terminating `0`, indicates how
many additional payload bytes follow. The total length is therefore known
from the first byte alone, and every following byte carries eight payload
bits. This differs from LEB128, which spends a continuation bit in each byte
and reveals the length only as the value is scanned.

The scheme generalizes to N bytes
(`11110xxx` = 5, `111110xx` = 6, ...), but the current ZXC spec caps the
encoding at 3 bytes because no legitimate value exceeds 21 bits (see below).

Encodings used:

- `0xxxxxxx` -> 1 byte total (7 bits payload, value < 128)
- `10xxxxxx` -> 2 bytes total (14 bits, value < 16384)
- `110xxxxx` -> 3 bytes total (21 bits, value < 2 MiB)

The first byte's payload bits — those below its unary prefix — are the least
significant bits of the value, and each following byte contributes the next
eight bits up. Writing `b0`, `b1`, `b2` for the bytes in stream order, the
accepted forms decode as:

```
1 byte:   value = b0
2 bytes:  value = (b0 AND 0x3F) OR (b1 << 6)
3 bytes:  value = (b0 AND 0x1F) OR (b1 << 5) OR (b2 << 13)
```

For example, the two bytes `AC 04` decode as the 2-byte form:
`(0xAC AND 0x3F) OR (0x04 << 6)` = 44 + 256 = **300**. The three bytes
`C3 35 0C` decode as the 3-byte form:
`(0xC3 AND 0x1F) OR (0x35 << 5) OR (0x0C << 13)` = 3 + 1696 + 98304 =
**100003**.

Used by GLO/GHI to carry LL/ML overflows beyond token/sequence inline
limits.

**Value bound**: a varint encodes `(LL - MASK)` or `(ML - MASK)`.
Since LL/ML are bounded by the largest block size a Chunk Size Code can
select (2 MiB at code 21, see [3](#3-file-header-16-bytes)), every legitimate
varint value is strictly less than 2^21 and therefore fits in **at most 3
bytes**.

Any prefix indicating a length >= 4 bytes (first byte `>= 0xE0`) is out of
spec for this format version: encoders must never emit such a varint, and
conforming decoders reject it as corrupt input. This caps the varint
surface to the format-defined block size limit and neutralizes
integer-overflow attacks in downstream bounds arithmetic. A future version
of the format that raises the per-block size limit would also extend the
accepted prefix lengths.

---

## 7. Checksums and Integrity

## 7.1 Header checksums

The file header carries a 16-bit checksum at `0x0E..0x0F`; every block header
carries an 8-bit checksum at `0x07`. They are computed differently, because
they are checked at different rates: the block checksum runs once per block, on
the decode path, and must cost almost nothing; the file checksum runs once per
archive, and can afford a guarantee.

All arithmetic below is on unsigned 64-bit integers modulo 2^64, and all
shifts are logical.

### Block header (8 bits)

The **8-bit block header checksum** takes the 8 header bytes as a single
little-endian 64-bit integer `v`, with the checksum byte at `0x07` treated as
zero, multiplies, and keeps the top byte:

```
h = (v XOR 0x9E3779B97F4A7C15) * 0x9E3779B97F4A7C15
checksum8 = h >> 56
```

Two properties are normative intent rather than implementation detail:

- **Folding from the top is required.** A product's low bits depend only on the
  input's low bits, so a checksum taken from the bottom would ignore most of the
  header. The top byte is reached by every input bit.
- **Every single-bit error is detected, not merely likely to be.** Flipping bit
  `i` moves the product by exactly `±(constant << i)`, and no shift of the
  constant over the 56 covered bits leaves `0x00` or `0xFF` in the top byte, so
  the carry cannot absorb the difference. Errors of two or more bits are caught
  with better than the 1/256 odds of a random function, but not by construction.

The XOR with the constant before multiplying keeps an all-zero header from
hashing to zero. The cost is three instructions.

### File header (16 bits)

The **16-bit file header checksum** takes the 16 header bytes as two
little-endian 64-bit integers, `v1` at `0x00` and `v2` at `0x08`, with the two
checksum bytes at `0x0E..0x0F` treated as zero. The halves are chained, not
summed: the first is mixed, the second is added to the result and mixed again.

```
h = (v1 XOR 0xD2D84A61D2D84A61) * 0xD2D84A61D2D84A61
h = (h + v2 + 0x9E3779B97F4A7C15) * 0x9E3779B97F4A7C15
checksum16 = h >> 48
```

Why a chain and not a sum: with two independent products summed, a bit in each
half can shift the two products by amounts whose top halfwords cancel, and
that cancellation does not depend on the header content — a structural blind
spot for two-bit errors. In the chain every flip still shifts the result by an
exact amount (`±2^k` times the constants, the sign set by the bit's value), but
the two halves no longer meet as equals, and the constants are chosen so that
**no single-bit or two-bit error can leave the top halfword unchanged, whatever
the header holds**. The order matters: `0xD2D84A61D2D84A61` must be the inner
constant and `0x9E3779B97F4A7C15` the outer one; swapped, two-bit cancellations
reappear. The reference decoder's test suite re-derives this from the
constants. Beyond two bits, misses sit at the 1/65536 of a random function.

The same function checks the dictionary file header
([§ 12](#12-pre-trained-dictionary-support)), over the same bytes.

These protect metadata/navigation fields.

## 7.2 Per-block checksum (optional)

When file header has `HAS_CHECKSUM=1`:
- each data block appends a 4-byte checksum after payload.
- checksum input is the block's **decompressed bytes**, dictionary prefix
  excluded. For a RAW block the payload is those bytes.
- seed is the block's zero-based position among the frame's data blocks, as a
  64-bit value; a frameless block (block API) uses `0`.
- algorithm id currently `0`: `fold32(rapidhash(decompressed_block, index))`,
  where `rapidhash` is rapidhash v3 (default secret) seeded with `index` and
  `fold32(h) = (h XOR (h >> 32)) AND 0xFFFFFFFF`.

The seed binds a block to its position: a block moved elsewhere in the frame
fails its own check, also under a range read through the seek table, which sees
only the blocks it touches. The archive digest of § 7.3 is the ordered fold of
these checksums, a whole-archive identity that a full decode verifies.

A decoder therefore verifies a block **after** decoding it, and a corrupted
block reports whatever the decoder tripped on first. In exchange the checksum
covers the whole pipeline: it catches a wrong dictionary accepted through a
`dict_id` collision, an encoder or decoder defect, and a divergence between
SIMD variants -- none of which touch the compressed bytes.

## 7.3 Archive digest (optional)

When the file header sets `HAS_CHECKSUM=1`, an 8-byte **digest** follows the
source size in the footer. It is an ordered 64-bit fold of every data block's
checksum, in stream order:

```text
digest = 0
for each data block, checksum c (the 4-byte value stored after the block):
    x      = ((u64)c + 1) * 0x9E3779B97F4A7C15
    digest = mix(digest XOR x, 0x2545F4914F6CDD1D)
mix(a, b): p = (u128)a * b; result = (u64)p XOR (u64)(p >> 64)
```

The digest identifies the archive as a whole: two archives of the same content
at the same block size share it, and any block reordered, dropped or altered
changes it. It is verified on a full decode with checksum verification on (and
by `zxc -t`), never on a seekable range read, which cannot see every block. It
is written into the footer and read back from it; it is not folded from the
block payloads on the wire beyond their checksums.

## 8. File Footer (8 or 16 bytes)

Footer is mandatory, immediately after the EOF block header (or after the SEK
block, on the seekable layout of § 5.5). The source size is the first 8 bytes;
the digest, when `HAS_CHECKSUM=1`, follows it.

```text
Offset  Size  Field
0x00    8     original_source_size (u64)
0x08    8     archive_digest (u64)          -- only when HAS_CHECKSUM=1
```

- **original_source_size**: full uncompressed size of the file, the first 8 footer bytes.
- **archive_digest**: the fold of § 7.3; present iff `HAS_CHECKSUM=1`, the last 8 bytes.

---

## 9. Decoder Validation Checklist (Practical)

1. Validate file header magic/version/checksum.
2. Parse blocks sequentially:
   - validate block header checksum,
   - check block bounds using the Compressed Payload Size.
3. Decode payload according to block type. For GLO/GHI:
   - reject Literal Encoding, Token Encoding and (GLO) Offset Encoding values
     outside § 5.2,
   - reject a block leaving fewer than 32 bytes behind its literal section,
   - check the derived section sizes still fit the payload.
4. If enabled, verify the trailing block checksum over the decoded bytes,
   seeded with the block's position (§ 7.2).
5. On EOF:
   - require `comp_size == 0`,
   - if `HAS_SEEK_TABLE=1`, read the SEK block header, require the payload size
     § 5.5 derives from the output, and skip the table,
   - read the footer (8 bytes, 16 when `HAS_CHECKSUM=1`),
   - compare the first 8 footer bytes (`original_source_size`) with produced output size,
   - if verifying, compare the trailing archive digest (§ 7.3) with the fold of the block checksums.

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

- **Version compatibility**: a decoder accepts **only** the format version it implements and **MUST** reject any other version. Because block-type numbering and payload formats may change between versions, a decoder **MUST NOT** attempt to interpret an archive whose version byte it does not recognise.
- **Unknown block types**: a decoder **MUST reject** any block whose type is not defined for its format version. The block-type set is fixed per version; introducing a new type is a version bump (decoders do **not** skip unknown blocks — silently advancing past untrusted, unrecognised data is unsafe).
- **Reserved fields**: all reserved bytes and flag bits **MUST** be written as zero by encoders. The current decoder tolerates (ignores) non-zero reserved values — they are covered by the header checksum, so accidental corruption is still caught — but assigning a reserved field any meaning is a **version bump**, never a same-version extension.
- **Defined-but-bounded fields**: where only specific values are defined (e.g. the checksum-algorithm id, currently `0` = RapidHash only), the decoder **rejects** out-of-range values as a corrupt header.

### 10.4 Minimum conforming decoder

A minimal conforming decoder for version 9 **MUST** support:
- File header parsing and checksum validation
- **RAW** blocks (type 0) - passthrough copy.
- **GLO** blocks (type 1) - full LZ decode with extras varint, including Huffman
  entropy sections (§5.2.1, PivCo layout) with code lengths up to 11 bits.
- **GHI** blocks (type 2) - full LZ decode with extras varint.
- **EOF** block (type 255) - stream termination.
- File footer validation (source size check).
- Deriving section sizes from the header and the two optional descriptors
  (§5.2), and rejecting a block that leaves fewer than 32 bytes behind its
  literal section.

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
| **File header checksum mismatch** | File header, offset 0x0E | Reject. Header is corrupt or truncated. |
| **Invalid chunk size code** | File header, offset 0x05 | Reject. Code outside the valid range `[12..21]`. |
| **Block header checksum mismatch** | Block header, offset 0x07 | Reject block. Stream is corrupt. |
| **Unknown block type** | Block header, offset 0x00 | Reject. The block-type set is fixed per format version (see §10.3); a decoder must not skip past unrecognised data. |
| **Block payload truncated** | While reading the Compressed Payload Size bytes | Reject. Unexpected end of stream. |
| **Block checksum mismatch** | Trailing 4-byte checksum, after decoding the block | Reject block. The decoded bytes are wrong: corrupt payload, wrong dictionary, or a block out of place. |
| **EOF block with non-zero comp_size** | EOF block header | Reject. Malformed EOF marker. |
| **Data block comp_size above block size** | Block header, offset 0x03 | Reject. A data block never compresses past its own content (§4.1). |
| **Block walk ends without an EOF block** | End of the block walk | Reject. A forged Compressed Payload Size can span the EOF marker; the resulting short decode must not be reported as success. |
| **Seek-table flag disagrees with the tail** | Between the EOF block and the footer | Reject. `HAS_SEEK_TABLE=1` without the SEK block §5.5 derives from the output, or any byte there with `HAS_SEEK_TABLE=0`. |
| **Seek table group inconsistent** | SEK payload | Reject. A size outside `[8, one block]`, an anchor outside the data area, a group running past the EOF block, or a last group not ending on it (§5.5). |
| **Block disagrees with its seek entry** | Block header, when a seekable reader accesses the block | Reject. The entry's size is not header + payload + checksum of the block found there (§5.5). |
| **Footer source size mismatch** | File footer, offset 0x00 | Reject. Output size does not match declared original size. |
| **Archive digest mismatch** | File footer, after the size (when `HAS_CHECKSUM=1`) | Reject (if verifying). Blocks were reordered, dropped or altered (§7.3). |
| **Decompressed output exceeds chunk size** | During LZ decode | Reject. Corrupt or malicious payload. |
| **Match offset out of bounds** | During LZ copy | Reject. Offset references data before output start. |
| **Varint exceeds maximum length** | Extras section | Reject. Overflow or corrupt extras data. |

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
- Enforce maximum allocation limits based on the Compressed Payload Size and Chunk Size Code.
- Reject files where the Compressed Payload Size exceeds `zxc_compress_bound(chunk_size)`.
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
1. Verify that a dictionary is provided; reject the archive if not.
2. Verify that the dictionary id matches `header.dict_id`
   ; reject on mismatch. For a raw in-memory dictionary without
   a shared table, the id is `fold32(rapidhash(content))`
   (`zxc_dict_id(dict, dict_size, NULL)`). When a shared literal table is
   attached, the id also binds the table:
   `id = fold32(rapidhash(table_128_bytes, seed = fold32(rapidhash(content))))`
   (`zxc_dict_id(content, size, table)`). The seed is the **folded 32-bit**
   content hash, zero-extended to rapidhash's 64-bit seed. `rapidhash` and
   `fold32` are defined in [7.2](#72-per-block-checksum-optional).

Older decoders that do not recognize the `HAS_DICTIONARY` flag will ignore it
(per §10.3: reserved flag bits are ignored). However, blocks compressed with a
dictionary contain match offsets that reference dictionary content; decoding
without the dictionary produces corrupt output. Per-block checksums (when
enabled) detect it, because they cover the decompressed bytes (§7.2). This also
covers the residual risk of a 32-bit `dict_id` collision, where the header check
passes on the wrong dictionary.

### 12.4 Dictionary file format (`.zxd`)

Dictionaries are stored as standalone `.zxd` files with the following layout:

```text
Offset  Size  Field
0x00    4     Magic Word (0x9CB0D1C7 LE)
0x04    1     Dictionary format version (currently 2)
0x05    1     Flags (bits 0..3: checksum algorithm id; bits 4..7 reserved)
0x06    2     Content size (u16 LE, max 65535)
0x08    4     dict_id (u32 LE, binds content AND shared table, see below)
0x0C    2     Reserved (0)
0x0E    2     Header Checksum (see 7.1, computed with 0x0C-0x0F zeroed)
0x10    N     Dictionary content (raw bytes)
0x10+N  128   Shared literal Huffman table (256 × 4-bit packed code lengths,
              same layout as the § 5.2.1 code-length header; always present)
```

- **Magic Word**: `0x9CB0D1C7`. Allows immediate rejection of non-dictionary files.
- **Version**: `2`. Decoders reject any other version with
  an unsupported dictionary version. Version 1 shipped with format v8: its header
  was signed with the pre-v9 checksum, not the one § 7.1 now specifies, so it is
  rejected on the version byte before the checksum is ever compared.
- **Flags**: bits `0..3` carry the checksum algorithm id (`0` = RapidHash-based folding), matching the ZXC file header flags; bits `4..7` are reserved (must be 0).
- **Shared literal Huffman table**: code lengths for the Literal Encoding `3` literal
  sections (§ 5.2.2), trained on the corpus' post-LZ literal distribution.
- **dict_id**: `fold32(rapidhash(table_128_bytes, seed = fold32(rapidhash(content))))`
  (see [12.3](#123-file-header-encoding)) — binds the exact (content, table)
  pair. Must match the `dict_id` stored in any ZXC file header that references
  this dictionary.
- **Header Checksum**: the 16-bit header checksum of [7.1](#71-header-checksums), computed over the 16-byte header with bytes `0x0C..0x0F` zeroed.
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
  a dictionary-required error (the `dict_id` in the header is still verified
  against the supplied dictionary, yielding a dictionary-mismatch error on a
  mismatch).

---

## 13. Summary of Useful Fixed Sizes

- File header: **16** bytes
- Block header: **8** bytes
- Block checksum (optional): **4** bytes
- GLO header: **12** bytes
- GHI header: **12** bytes
- Section descriptor: **4** bytes (`u32`, written only when needed)
- GLO descriptors total: **0**, **4** or **8** bytes (levels 3-5 / 6 / 7)
- GHI descriptors total: **0** bytes
- Minimum slack behind the literal section: **32** bytes
- File footer: **8** bytes (**16** with a digest)
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

Generated archive size: **62 bytes**.

### 14.1 Full hexdump

```text
00000000: F5 2E B0 9C 09 13 80 00 00 00 00 00 00 00 6D 86
00000010: 00 00 00 0A 00 00 00 A0 48 65 6C 6C 6F 20 5A 58
00000020: 43 0A 90 BB A1 75 FF 00 00 00 00 00 00 83 0A 00
00000030: 00 00 00 00 00 00 BD 8A 9E 74 2A A2 9A B6
```

### 14.2 Byte-level decoding

#### A) File Header (offset `0x00`, 16 bytes)

```text
F5 2E B0 9C | 09 | 13 | 80 | 00 00 00 00 00 00 00 | 6D 86
```

- `F5 2E B0 9C` -> magic word (LE) = `0x9CB02EF5`.
- `09` -> format version 9.
- `13` -> chunk-size code 19 (exponent encoding: `2^19 = 524288` bytes, i.e. 512 KiB, the default).
- `80` -> checksum enabled (`HAS_CHECKSUM=1`, algo id 0).
- next 7 bytes are reserved zeros.
- `6D 86` -> header checksum (LE value `0x866D`).

#### B) Data Block #0 (RAW)

Block header at offset `0x10`:

```text
00 | 00 | 00 | 0A 00 00 00 | A0
```

- type `00` = RAW.
- flags `00`, reserved `00`.
- `comp_size = 0x0000000A = 10` bytes.
- header checksum = `0xA0`.

Payload at `0x18..0x21` (10 bytes):

```text
48 65 6C 6C 6F 20 5A 58 43 0A
```

ASCII: `Hello ZXC\n`.

Trailing block checksum at `0x22..0x25`:

```text
90 BB A1 75
```

LE value: `0x75A1BB90`. This is a RAW block, so its payload already is the
decompressed data and §7.2's input is those same ten bytes, seeded with the
block's position, 0.

#### C) EOF Block (offset `0x26`, 8 bytes)

```text
FF | 00 | 00 | 00 00 00 00 | 83
```

- type `FF` = EOF.
- `comp_size = 0` (mandatory).
- header checksum = `0x83`.

#### D) File Footer (offset `0x2E`, 16 bytes)

```text
0A 00 00 00 00 00 00 00 | BD 8A 9E 74 2A A2 9A B6
```

- original source size = `10` bytes (the first 8 footer bytes).
- archive digest = `0xB69AA22A749E8ABD` (the fold of block #0's checksum, §7.3).

### 14.3 Structural view with absolute offsets

```text
0x00..0x0F  File Header (16)
0x10..0x17  RAW Block Header (8)
0x18..0x21  RAW Payload (10)
0x22..0x25  RAW Block Checksum (4)
0x26..0x2D  EOF Block Header (8)
0x2E..0x35  File Source Size (8)
0x36..0x3D  Archive Digest (8)
```

### 14.4 Seekable Variant (with Seek Table)

Same 10-byte input (`Hello ZXC\n`), compressed with seekable mode enabled:

```bash
zxc -z -C -1 -S sample.txt
```

Generated archive size: **82 bytes** (20 bytes larger than the non-seekable variant).

#### Full hexdump

```text
00000000: F5 2E B0 9C 09 13 A0 00 00 00 00 00 00 00 0D 45
00000010: 00 00 00 0A 00 00 00 A0 48 65 6C 6C 6F 20 5A 58
00000020: 43 0A 90 BB A1 75 FF 00 00 00 00 00 00 83 FE 00
00000030: 00 0C 00 00 00 6F 10 00 00 00 00 00 00 00 16 00
00000040: 00 00 0A 00 00 00 00 00 00 00 BD 8A 9E 74 2A A2
00000050: 9A B6
```

#### Byte-level decoding

**A) File Header** (offset `0x00`, 16 bytes) - as non-seekable, but for two fields:

- `A0` -> `HAS_CHECKSUM=1` and `HAS_SEEK_TABLE=1` (`0x80 | 0x20`), algo id 0.
- `0D 45` -> header checksum (LE value `0x450D`).

**B) Data Block #0 (RAW)** (offset `0x10`, 22 bytes) - identical to non-seekable.

**C) EOF Block** (offset `0x26`, 8 bytes) - identical to non-seekable.

**D) SEK Block** (offset `0x2E`, 20 bytes)

Block header at `0x2E`:

```text
FE | 00 | 00 | 0C 00 00 00 | 6F
```

- `FE` -> type 254 = SEK (Seek Table).
- flags `00`, reserved `00`.
- `comp_size = 0x0000000C = 12` bytes (one group: an 8-byte anchor and one 4-byte size; below 4 GiB the fold is the size itself).
- header checksum = `0x6F`.

Group 0 at `0x36`:

```text
10 00 00 00 00 00 00 00 | 16 00 00 00
```

- Anchor: block #0 starts at offset `0x10 = 16`, right after the file header.
- Size of block #0: `0x16 = 22` bytes = header (8) + payload (10) + checksum (4).
  Anchor + size = `0x26`, the EOF block: the group's sum lands where it must. ✓

**E) File Footer** (offset `0x42`, 16 bytes)

```text
0A 00 00 00 00 00 00 00 | BD 8A 9E 74 2A A2 9A B6
```

- original source size = `10` bytes.
- archive digest = `0xB69AA22A749E8ABD` (§7.3).

#### Structural view with absolute offsets

```text
0x00..0x0F  File Header (16)
0x10..0x17  RAW Block Header (8)
0x18..0x21  RAW Payload (10)
0x22..0x25  RAW Block Checksum (4)
0x26..0x2D  EOF Block Header (8)
0x2E..0x35  SEK Block Header (8)    <- seek table
0x36..0x3D  Group 0 anchor (8)      <- offset of block #0
0x3E..0x41  Block #0 size (4)
0x42..0x49  File Source Size (8)
0x4A..0x51  Archive Digest (8)
```

> **Compatibility note**: The SEK block is inserted between the EOF block and the file footer, so the footer stays at the very **end of the file** (its last 8 bytes, or 16 with checksums; § 8). Decoders that locate it from the end (`src + src_size - footer_len` for buffer APIs, `fseek(END - footer_len)` for file APIs, `footer_len` being 8 or 16) work unchanged with seekable archives. **Streaming decoders**, which reach the footer sequentially, learn from `HAS_SEEK_TABLE` whether a SEK block comes first.

---

## 15. Worked Example: Dictionary File (`.zxd` Hexdump)

A minimal dictionary whose content is the 5 ASCII bytes `hello`. Total file size: **149 bytes** (16-byte header + 5-byte content + 128-byte shared Huffman table). This is the on-disk form produced by `zxc_dict_save()` (see §12.4); the table is always present.

### 15.1 Full hexdump

```text
00000000: C7 D1 B0 9C 02 00 05 00 34 07 FC 0C 00 00 C6 51
00000010: 68 65 6C 6C 6F 00 00 00 00 00 00 00 00 00 00 00
00000020: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
00000030: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
00000040: 00 00 00 00 00 00 00 20 00 02 00 02 20 00 00 00
00000050: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
00000060: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
00000070: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
00000080: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
00000090: 00 00 00 00 00
```

### 15.2 Byte-level decoding

#### A) Dictionary Header (offset `0x00`, 16 bytes)

```text
C7 D1 B0 9C | 02 | 00 | 05 00 | 34 07 FC 0C | 00 00 | C6 51
```

- `C7 D1 B0 9C` -> magic word (LE) = `0x9CB0D1C7` (`.zxd` dictionary).
- `02` -> dictionary format version 2.
- `00` -> flags (bits 0..3 = checksum algorithm id `0` = RapidHash; bits 4..7 reserved).
- `05 00` -> content size (LE) = `5` bytes.
- `34 07 FC 0C` -> `dict_id` (LE) = `0x0CFC0734`. Binds the **(content, table)** pair (see §12.4) and must match the `dict_id` stored in the file header of any `.zxc` archive compressed with this dictionary.
- `00 00` -> reserved.
- `C6 51` -> header checksum (LE) = `0x51C6`, computed over the 16-byte header with bytes `0x0C..0x0F` zeroed (same method as the ZXC file header — the checksum is the last 2 bytes of the header).

#### B) Dictionary Content (offset `0x10`, 5 bytes)

```text
68 65 6C 6C 6F
```

ASCII: `hello`. Raw bytes that prefill the LZ77 window — not compressed.

#### C) Shared Huffman Table (offset `0x15`, 128 bytes)

```text
... 20 00 02 00 02 20 ...   (remaining bytes 0x00)
```

256 × 4-bit code lengths, packed two-per-byte (low nibble first), for the shared literal table (§5.2.2). Symbols absent from the training distribution have length `0`; here only the four bytes of `hello` carry codes (e.g. the nibble at table index `'e'`=0x65 gives length `2`), so all other entries are zero.

### 15.3 Structural view with absolute offsets

```text
0x00..0x0F  Dictionary Header (16)
0x10..0x14  Dictionary Content (5)
0x15..0x94  Shared Huffman Table (128)
```
