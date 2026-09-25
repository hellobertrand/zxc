---
title: "ZXC: A Seekable, Block-Oriented Compressed File Format"
abbrev: ZXC Format
docname: draft-lebonnois-zxc-format-00
category: info
ipr: trust200902
area: Applications
submissionType: independent
keyword:
  - compression
  - lossless
  - lz77
  - random access

stand_alone: yes
pi:
  toc: yes
  tocompact: yes
  tocdepth: 3
  symrefs: yes
  sortrefs: yes
  comments: no
  inline: no
  iprnotified: no
  strict: yes

author:
  -
    ins: B. Lebonnois
    name: Bertrand Lebonnois
    email: zxc.codec@gmail.com

normative:
  RAPIDHASH:
    title: "RapidHash: a fast, high-quality non-cryptographic hash function"
    target: https://github.com/Nicoshev/rapidhash
    author:
      -
        ins: N. De Carli
        name: Nicolas De Carli
    date: false

informative:
  RFC5116:
  RFC8478:
  LZ4:
    title: "LZ4 - Extremely fast compression"
    target: https://lz4.github.io/lz4/
    author:
      -
        ins: Y. Collet
        name: Yann Collet
    date: false
  PIVCO:
    title: "PivCo-Huffman"
    target: https://marcinzukowski.github.io/pivco-huffman/paper-1.0/ph.html
    author:
      -
        ins: M. Żukowski
        name: Marcin Żukowski
        asciiInitials: M.
        asciiSurname: Zukowski
        asciiFullname: Marcin Zukowski
    date: 2026
  ZXC-WP:
    title: "The ZXC Compressor: design and implementation notes"
    target: https://github.com/hellobertrand/zxc/blob/main/docs/WHITEPAPER.md
    author:
      -
        ins: B. Lebonnois
        name: Bertrand Lebonnois
    date: false

--- abstract

ZXC is a lossless data compression format designed for high
decompression throughput and random access. A ZXC file is a sequence
of independently decodable blocks, optionally indexed by a seek table,
so any block can be decompressed without reading the blocks before it.

This document describes the format, registers the "application/zxc"
media type, and specifies wire-format version 9, intended for
publication as version 1.0 of the ZXC specification.

--- middle

# Introduction

ZXC is a general-purpose lossless compression format designed around
three goals:

1. Decompression throughput in the multi-gigabyte-per-second range
   on commodity CPUs, in the same family as LZ4 {{LZ4}} rather than
   the higher-ratio family of Zstandard {{RFC8478}}.
2. Random access via an optional seek table appended to the archive,
   allowing constant-time navigation to any block.
3. Kernel-space friendliness: no dynamic allocation in the hot path,
   no dependency on the C standard I/O library, and a small
   self-contained core suitable for embedding in operating system
   kernels or freestanding environments.

The format is block-oriented. A file is a sequence of independently
decodable blocks of a fixed maximum decompressed size, terminated by
a distinguished end-of-stream block and followed by a small footer
carrying the original source size and, when checksums are enabled, a
digest of the whole archive.

Three payload encodings are defined: RAW (uncompressed), GLO
(general LZ at higher ratio, with separated streams and optional
Huffman coding of literals), and GHI (general LZ at higher
throughput, with packed 32-bit sequence words).

This document specifies the on-disk binary format only. It does not
mandate any particular encoder strategy. Any byte sequence that
satisfies the syntactic and integrity constraints of this document
is a conforming ZXC stream.

# Conventions and Terminology

## Requirements Language

{::boilerplate bcp14-tagged}

## Byte Order and Units

All multi-byte integers in this format are encoded in little-endian
byte order. Offsets are expressed in bytes and are zero-based from
the start of each enclosing structure unless otherwise stated.

## Definitions

Block:
: A self-contained unit produced by the encoder, consisting of an 8-byte
  block header, a payload of Compressed Payload Size bytes, and an
  OPTIONAL 4-byte trailing checksum.

Block size:
: The maximum decompressed size of a single block, derived from the
  chunk-size code in the file header. A block size is a power of two
  in the range \[4 KiB, 2 MiB\].

Sequence:
: In LZ-coded blocks, the triple (literal length, match length,
  match offset) emitted by the LZ77-style parser.

Conforming decoder:
: An implementation that, given a syntactically valid ZXC stream
  produced by a conforming encoder at the same major version,
  reproduces the original input bytes exactly and detects every
  error condition listed in {{error-handling}}.

# Overall Structure of a ZXC File {#overall-structure}

A ZXC file is the concatenation, in this order, of:

~~~
+-----------------------------+
|   File Header (16 bytes)    |
+-----------------------------+
|   Data Block #0             |
|   Data Block #1             |
|   ...                       |
|   Data Block #N-1           |
+-----------------------------+
|   EOF Block (8 bytes)       |
+-----------------------------+
|   SEK Block (if flagged)    |
+-----------------------------+
|   File Footer (8/16 bytes)  |
+-----------------------------+
~~~

The File Footer always ends the file. It is 8 bytes long, or 16 when
HAS_CHECKSUM = 1 ({{file-footer}}), so its length follows from the File
Header alone. Decoders MAY rely on this invariant to locate the footer
by seeking that many bytes before the end of the file.

The SEK block is present if and only if the File Header sets
HAS_SEEK_TABLE ({{file-header}}). A conforming encoder MUST emit exactly
one EOF block per stream, then the SEK block when HAS_SEEK_TABLE = 1,
then the footer.

# File Header {#file-header}

The File Header is 16 bytes long and is laid out as follows:

~~~
 Offset  Size  Field
 0x00    4     Magic
 0x04    1     Format Version
 0x05    1     Chunk Size Code
 0x06    1     Flags
 0x07    7     Reserved / Dictionary ID
 0x0E    2     Header Checksum
~~~

## Field Definitions

Magic (u32):
: MUST be 0x9CB02EF5. Stored little-endian, the on-disk byte
  sequence is F5 2E B0 9C.

Format Version (u8):
: The wire-format version. This document specifies version 9. A
  conforming decoder MUST reject any file whose Format Version it
  does not support, as an unsupported version ({{error-handling}}).

Chunk Size Code (u8):
: Values in the range \[12, 21\] are interpreted as exponents, where
  block size = 2^code. This yields valid block sizes from 4 KiB
  (code 12) to 2 MiB (code 21). The default block size in the
  reference implementation is 512 KiB (code 19). All other values
  MUST be rejected.

Flags (u8):
: Bit 7 (0x80) is HAS_CHECKSUM. When set, each data block carries a
  trailing 4-byte checksum of its decompressed bytes
  ({{per-block-checksum}}) and the File Footer carries an Archive Digest
  ({{archive-digest}}). Bit 6 (0x40) is HAS_DICTIONARY; when set, the
  archive was compressed against a pre-trained dictionary
  ({{dictionary}}) and the Reserved field carries that dictionary's
  identifier. Bit 5 (0x20) is HAS_SEEK_TABLE; when set, a SEK block
  ({{sek-block}}) sits between the EOF block and the File Footer, and
  when clear the footer follows the EOF block directly. Bits 0..3 encode
  the checksum algorithm identifier; value 0 denotes a 32-bit folding of
  the RapidHash function {{RAPIDHASH}}, and other values are RESERVED.
  Bit 4 is RESERVED; encoders MUST set it to zero and decoders MUST
  ignore it.

Reserved / Dictionary ID:
: 7 bytes. When HAS_DICTIONARY is clear, all seven bytes are
  RESERVED: encoders MUST set them to zero and decoders MUST ignore
  their content. When HAS_DICTIONARY is set, the four bytes at
  offsets 0x07..0x0A carry the Dictionary ID as a u32 in
  little-endian order ({{dictionary-header-encoding}}) and the three
  bytes at offsets 0x0B..0x0D MUST be zero.

Header Checksum (u16):
: The 16-bit header checksum of {{header-checksums}}, computed over
  the 16-byte header with the two bytes at 0x0E..0x0F treated as
  zero.

# Block Container {#block-container}

Every block in a ZXC file begins with an 8-byte block header:

~~~
 Offset  Size  Field
 0x00    1     Block Type
 0x01    1     Block Flags
 0x02    1     Reserved
 0x03    4     Compressed Payload Size
 0x07    1     Header Checksum
~~~

## Block Types

The following Block Type values are assigned by this document:

| Value | Mnemonic | Reference          |
|------:|----------|--------------------|
|   0   | RAW      | {{raw-block}}      |
|   1   | GLO      | {{glo-block}}      |
|   2   | GHI      | {{ghi-block}}      |
|  254  | SEK      | {{sek-block}}      |
|  255  | EOF      | {{eof-block}}      |

All other values are unassigned. A decoder MUST reject any block
whose type is not defined for the version it implements, rather
than skipping it ({{compatibility-rules}}).

## Block Header Semantics

Block Flags (u8):
: RESERVED. Encoders MUST write 0. Decoders MUST ignore non-zero
  values from future revisions.

Reserved (u8):
: MUST be set to 0 by encoders. MUST be ignored by decoders.

Compressed Payload Size:
: The size in bytes of the block payload. This size does NOT
  include the 8-byte block header nor the OPTIONAL trailing 4-byte
  checksum.

  For a data block (RAW, GLO, GHI) this size MUST NOT exceed the block
  size selected by the File Header's Chunk Size Code ({{file-header}}).
  A block that would grow under compression falls back to RAW, whose
  payload equals its content, so the block size is reached exactly and
  never passed. Decoders MUST reject a larger value.

  This bound does not apply to the SEK block ({{sek-block}}), which is
  not a data block: its payload is a table of block groups, an 8-byte
  anchor and one 4-byte size per data block, and therefore routinely
  exceeds the block size. Its Compressed Payload Size holds the table
  size folded to 32 bits ({{sek-layout}}).

Header Checksum (u8):
: The 8-bit header checksum of {{header-checksums}}, computed over
  the 8-byte block header with byte 0x07 treated as zero.

## Physical Block Layout

~~~
[ 8-byte Block Header ]
[ Payload (Compressed Payload Size bytes) ]
[ 4-byte Trailing Checksum (only if HAS_CHECKSUM=1, data blocks) ]
~~~

Only data blocks (RAW, GLO, GHI) carry the trailing checksum. The EOF
block (type 255) and the SEK block (type 254) MUST NOT carry one, even
when HAS_CHECKSUM=1 is set in the file header.

# RAW Block (Type 0) {#raw-block}

A RAW block carries uncompressed data. The payload of a RAW block
is Compressed Payload Size bytes of literal input data, with no internal
sub-header.

~~~
decoded size = Compressed Payload Size
~~~

Encoders MAY emit a RAW block whenever a compressed encoding would
not yield a worthwhile size reduction. A conforming decoder MUST
support RAW blocks.

# GLO Block (Type 1) {#glo-block}

The GLO block ("General LZ, LOw throughput") is the primary
compressed encoding.
It uses an LZ77-style sequence stream with literal, token, offset,
and extras streams stored contiguously, and OPTIONAL Huffman coding
of the literal stream.

## GLO Payload Layout

~~~
+-------------------------------+
| GLO Header (12 bytes)         |
| Section descriptors (0/4/8 B) |
| Literals stream               |
| Tokens stream                 |
| Offsets stream                |
| Extras stream (+ slack pad)   |
+-------------------------------+
~~~

## GLO Header

~~~
 Offset  Size  Field
 0x00    4     Sequence Count
 0x04    4     Literal Count
 0x08    1     Literal Encoding
 0x09    1     Token Encoding
 0x0A    1     Match-Length Encoding
 0x0B    1     Offset Encoding
~~~

Sequence Count:
: The number of LZ77 sequences encoded in the block.

Literal Count:
: The total number of literal bytes the Literals section decodes to.

Literal Encoding:
: 0 = RAW, 1 = RLE, 2 = Huffman ({{huffman-literal-section}}),
  3 = shared-table Huffman ({{shared-huffman-literal-section}}).

Token Encoding:
: 0 = RAW tokens, 2 = Huffman tokens (level 7 only).

Match-Length Encoding:
: RESERVED; match lengths share the token byte.

Offset Encoding:
: 0 = 16-bit offsets, 1 = 8-bit offsets.

Literal Encoding, Token Encoding, and Offset Encoding are closed value
sets: a GLO decoder MUST reject any value outside those listed above
rather than fall back to a default, so that unused values stay available
to a later format version.

Match-Length Encoding is RESERVED: encoders MUST write 0, and decoders
MUST ignore it ({{compatibility-rules}}).

## Section Descriptors {#glo-section-descriptors}

Only the two section sizes that the header cannot imply are stored.
Each present descriptor is a little-endian u32, and they appear in
this order:

Literals Section Size:
: Present when Literal Encoding != 0. The compressed size, in bytes, of
  the Literals section.

Tokens Section Size:
: Present when Token Encoding = 2. The compressed size, in bytes, of the
  Tokens section.

A GLO block therefore carries 0, 4, or 8 bytes of descriptors.
Every other section size is derived from the header, and so cannot
be forged inconsistently with it:

- The Literals section decodes to Literal Count bytes. When Literal
  Encoding = 0 it also occupies Literal Count bytes on the wire, which
  is why no descriptor is written for it.
- The Tokens section occupies Sequence Count bytes when Token
  Encoding = 0.
- The Offsets section occupies Sequence Count bytes when Offset
  Encoding = 1, and twice that otherwise.
- The Extras section occupies whatever payload remains after the three
  sections above.

## Literal Section Slack {#literal-slack}

At least 32 bytes of payload MUST follow the Literals section:

~~~
 Compressed Payload Size - header - descriptors
   - Literals Section Size >= 32
~~~

Decoders copy literals with an over-reading wild copy, so these
trailing bytes keep a block's last copy inside its own payload. A
decoder MUST reject a block that does not satisfy the inequality
above.

The Tokens, Offsets, and Extras sections normally cover the
requirement on their own. When they do not, the encoder appends
zero padding, which falls inside the Extras residue and so needs no
wire field of its own; its contents are unconstrained and MUST NOT
be validated.

The same rule applies to GHI ({{ghi-block}}), where the Sequences
and Extras sections play the role of Tokens, Offsets, and Extras.

## Stream Content

Literals:
: If Literal Encoding = 0, raw literal bytes. If Literal Encoding = 1,
  RLE-tokenised literals. If Literal Encoding = 2, Huffman-coded
  literals ({{huffman-literal-section}}). If Literal Encoding = 3,
  Huffman-coded using the dictionary's shared literal table
  ({{shared-huffman-literal-section}}); valid only in
  dictionary-compressed archives.

Tokens:
: One byte per sequence, formed as (LL << 4) | ML. The high nibble is LL
  (literal length) and the low nibble is ML (match length minus the
  minimum match of 5). When Token Encoding = 0 (all levels <= 6), the
  Sequence Count token bytes are stored verbatim and the Tokens
  section's compressed size equals Sequence Count. When Token
  Encoding = 2 (level 7 only), the token bytes are Huffman-coded over
  the token alphabet using the {{huffman-literal-section}} layout,
  including the inline 128-byte code-length header; the section's
  compressed size is the encoded payload size, and the decoder expands
  it back to Sequence Count bytes.

Offsets:
: Sequence Count entries, each 1 byte if Offset Encoding = 1 or 2 bytes
  (little-endian) if Offset Encoding = 0. Stored values are biased: the
  decoder MUST add +1 to the stored value to obtain the actual match
  offset. This makes a stored offset of zero impossible by
  construction; the minimum decoded offset is 1.

Extras:
: A sequence of prefix-varints ({{varint}}) carrying the overflow
  values of {{glo-overflow-rules}}.

## Overflow Rules {#glo-overflow-rules}

When LL == 15 is read from a token, the decoder MUST read a varint
from the Extras stream and add it to LL.

When ML == 15 is read from a token, the decoder MUST read a varint
from the Extras stream and add it to ML. The actual match length is
then ML + 5.

Otherwise, the actual match length is ML + 5.

## Huffman Literal Section {#huffman-literal-section}

When Literal Encoding = 2, the Literals stream carries a length-limited
canonical Huffman code over the literal bytes. The code bits are
placed on the wire with the PivCo layout (a level-ordered Huffman
arrangement, after {{PIVCO}}): the code lengths, code bits, and
total size are those of ordinary Huffman, but the bits are grouped
by tree level rather than by symbol.

~~~
 Offset  Size  Field
 0x00    128   Code-length header (256 x 4-bit lengths, packed
               two-per-byte, low nibble first). code_len[i] in
               [0, 11]; 0 means the symbol is absent.
 0x80    var   One run per emitting node of the canonical code tree,
               in breadth-first order (parents before children, left
               before right). Runs are LSB-first within bytes and
               each run is padded to a byte boundary.
~~~

### Canonical Code

Codes are length-limited at L = 11 bits. Levels <= 6 never emit a
code longer than 8 bits; level 7 (ULTRA) MAY emit codes up to 11
bits. Symbols are ordered by (code_len, symbol) and assigned
consecutive code values in that order, starting at zero and
left-shifting by one whenever the length increases — the standard
canonical construction. The code tree is the binary trie of these
codewords read MSB-first: at depth d, bit code_len - 1 - d of a
codeword selects the left (0) or right (1) child. The Kraft equality
(see {{huffman-decoder-validation}}) makes this trie complete: every
internal node has exactly two children.

### Emitting Nodes and Flat Subtrees

Both encoder and decoder derive, from the code lengths alone, the set
of flat roots. An internal node is a flat root if and only if:

1. it is not itself contained in another flat subtree (breadth-first
   order resolves this: parents are classified before children);
2. every leaf below it lies at the same relative depth D; and
3. D is at least 2.

Completeness of the trie means condition 2 implies a perfect binary
subtree of 2^D leaves. Every maximal complete subtree of depth D >= 2
is a flat root: this is a fixed rule of the format.

Every internal node that is neither a flat root nor a descendant of
one is a bitmap node. Its run holds one branch bit per symbol routed
through it, in symbol-sequence order (0 = left child, 1 = right
child). A flat root's run instead holds D packed bits per symbol
routed through it, in symbol-sequence order: bit j (bit 0 first) is
the branch taken at relative depth j below the flat root. Strict
descendants of a flat root emit no run at all; they are skipped in
the breadth-first enumeration.

### Derived Sizes

The section carries no explicit size field for any run. The root
handles Literal Count symbols; the population count of a bitmap node's
bits equals its right child's symbol count; and a flat root consuming
c symbols occupies exactly ceil(c * D / 8) bytes. Every run length is
therefore derived while walking the breadth-first order once.

### Decoder Validation Requirements {#huffman-decoder-validation}

A conforming decoder MUST enforce the following constraints:

1. Every code length MUST satisfy `code_len[i]` <= 11.
2. At least one symbol MUST be present (`code_len[i]` != 0 for some
   i).
3. The Kraft sum, defined as the sum of 2^(11 - `code_len[i]`) over
   present symbols, MUST equal 2^11, except for the
   single-present-symbol degenerate case in which exactly one symbol
   has code_len = 1 and the Kraft sum equals 2^10.
4. Every node's run, whether bitmap or flat, MUST lie within the
   section payload.
5. A population count that routes symbols to an absent child MUST be
   treated as corruption.

A violation of any of the above MUST be reported as a corrupt-data
error.

### Encoder Selection Policy

Choosing among the literal encodings is an encoder policy, not a
format rule: the reference encoder prices each candidate (RAW, RLE,
per-block Huffman, shared-table Huffman) against a per-level
decode-time premium and takes the minimum.

Three further encoder-side choices are named here only because they are
easily mistaken for wire requirements. The reference encoder nudges
fitted code lengths toward a flatter tree at levels 6-7. At levels 1-5
it may refuse matches closer than 32 bytes. At levels 3-5 it may re-emit
a match longer than 19 bytes, which a GLO token would otherwise escape
to an Extras varint, as a chain of inline matches of at most 19 bytes at
the same offset, so that the decoder never takes that escape; it does so
within a per-level cap, and only in blocks where the escape looks poorly
predicted. These choices change which sequences an encoder emits, never
how they are encoded. The nudged lengths remain an ordinary canonical,
Kraft-exact code, the result is an ordinary GLO or GHI block, and none
of the choices is observable on the wire.

## Shared-Table Huffman Literal Section {#shared-huffman-literal-section}

Literal Encoding = 3 is valid only in archives compressed with a
dictionary (HAS_DICTIONARY set). The payload is the same Huffman section
as {{huffman-literal-section}} with the 128-byte code-length header
OMITTED: the code lengths are taken instead from the shared literal
table carried by the .zxd dictionary ({{zxd-format}}), which is
validated once — under the same rules as {{huffman-decoder-validation}}
— when the dictionary is attached. The section payload therefore begins
directly with the first emitting-node run.

The shared table is trained on the corpus' post-LZ literal distribution
and covers only the symbols seen during training. An encoder MUST fall
back to a per-block table (Literal Encoding = 2) or to RAW/RLE for any
block containing a literal byte that has no code in the shared table. A
decoder MUST reject a Literal Encoding = 3 section when no dictionary
table is attached ({{error-handling}}); the archive's Dictionary ID
guarantees a matching table whenever the dictionary check has passed.

The level-7 token section reuses the {{huffman-literal-section}}
layout (Token Encoding = 2, including the inline 128-byte code-length
header) over the token byte alphabet.

# GHI Block (Type 2) {#ghi-block}

The GHI block ("General LZ, HIgh throughput") is an alternate
compressed encoding optimised for raw decompression speed. It uses
packed 32-bit sequence words rather than separated streams.

## GHI Payload Layout

~~~
+-------------------------------+
| GHI Header (12 bytes)         |
| Literals stream               |
| Sequences stream (N x 4 B)    |
| Extras stream (+ slack pad)   |
+-------------------------------+
~~~

## GHI Header

The GHI header is binary-identical to the GLO header. In a GHI block
Literal Encoding is always 0 (raw literals), so the Literals section
occupies Literal Count bytes; Token Encoding and Match-Length Encoding
are 0. Offset Encoding is written as 0 and MUST be ignored by decoders:
GHI has no offset stream, and sequence words always store 16-bit
offsets.

## Section Descriptors {#ghi-section-descriptors}

A GHI block carries no section descriptors at all. Every size
follows from the header:

- The Literals section occupies Literal Count bytes, literals being
  always RAW in a GHI block.
- The Sequences section occupies 4 * Sequence Count bytes.
- The Extras section occupies whatever payload remains, including
  any slack padding.

The literal slack rule of {{literal-slack}} applies unchanged: at
least 32 bytes of payload MUST follow the Literals section, and a
decoder MUST reject a block that does not satisfy it.

## Sequence Word Format

Each sequence is encoded as a 32-bit little-endian word:

~~~
Bits 31..24 : LL  (literal length, 8 bits)
Bits 23..16 : ML  (match length minus 5, 8 bits)
Bits 15..0  : Offset - 1 (16 bits, biased: decode = stored + 1)
~~~

The little-endian memory order is:

~~~
byte0 = offset low
byte1 = offset high
byte2 = ML
byte3 = LL
~~~

## Overflow Rules

When LL == 255, the decoder MUST read a varint from the Extras
stream and add it to LL.

When ML == 255, the decoder MUST read a varint from the Extras
stream and add it to ML. The actual match length is then ML + 5.

Otherwise, the actual match length is ML + 5.

# SEK Block (Type 254) {#sek-block}

The SEK block records where every data block starts and how long it is
on disk, which gives constant-time random access to any block. It sits
between the EOF block and the File Footer when, and only when, the File
Header sets HAS_SEEK_TABLE. An archive of an empty source still carries
one when the flag is set: a bare SEK block header whose Compressed
Payload Size is 0.

## SEK Layout {#sek-layout}

~~~
 Offset   Size  Field
 0x00     8     Block Header (type 254)
 0x08     8     Group 0 Anchor (u64 LE), always 16
 0x10     4     Block 0 Size (u32 LE)
 0x14     4     Block 1 Size (u32 LE)
 ...      4     ... one size per block, 64 per group
 0x110    8     Group 1 Anchor (u64 LE)
 0x118    4     Block 64 Size (u32 LE)
 ...      ...   ...
~~~

The table is a sequence of groups of 64 data blocks. A group is its
Anchor, the byte offset of its first block's header from the start of
the file as a u64, followed by one u32 per block: the block's on-disk
size, that is its header, payload and trailing checksum if any. Group j
starts j x 264 bytes into the table, and only the last group MAY hold
fewer than 64 sizes. Block i starts at the Anchor of group floor(i / 64)
plus the sizes of the blocks before it in that group, so locating any
block costs one bounded read.

For N data blocks the table occupies T bytes, and the block header's
32-bit Compressed Payload Size holds T folded:

~~~
T                       = ceil(N / 64) x 8 + N x 4
Compressed Payload Size = (T XOR (T >> 32)) mod 2^32
~~~

Below 4 GiB the fold is T itself, as for every other block's payload
size; above it, all 64 bits of T take part. Decoders never read the
table length from this field: they derive N from the footer and compare
the fold of T, so the field does not bound the table.

The decompressed size of each block and N are derived from the block
size encoded in the File Header's Chunk Size Code and from the footer's
Source Size: every block decompresses to the block size except the last,
which MAY be shorter. No field holds N, so N and T MUST be computed in
64-bit arithmetic.

The table is not authenticated ({{seek-table-integrity}}).

## Backward Reading

A decoder that accesses blocks without scanning the archive linearly MAY
use the following procedure:

1. Read the File Header (first 16 bytes) and derive the block size from
   the Chunk Size Code. If HAS_SEEK_TABLE is clear, the archive is not
   seekable.
2. Read the File Footer (last 8 bytes, or 16 when HAS_CHECKSUM = 1) and
   extract Source Size from its first 8 bytes.
3. Compute N = ceil(Source Size / block size).
4. Compute the size of the SEK block as 8 + T ({{sek-layout}}).
5. Seek backward by that many bytes from the start of the footer to
   locate the SEK block header.
6. Validate that the located block has Block Type == 254, that its
   Compressed Payload Size equals the fold of T, and that an EOF block
   header occupies the 8 bytes before it. A decoder MUST reject an
   archive that fails this check.
7. Read nothing else when the archive is opened. A decoder validates a
   group when it first accesses one of its blocks, and MUST reject the
   access unless the Anchor of group 0 is 16, every Anchor lies between
   16 and the offset of the EOF block, every size lies in \[8, 8 + block
   size + checksum size\], the group ends at or before the EOF block,
   and the last group ends exactly on it.
8. When it reads a block through the table, a decoder MUST reject the
   block if its header, payload and trailing checksum do not add up to
   the block's size entry. This catches an entry pointing into the
   middle of a block, but not one moved onto another block of the same
   on-disk size.

A block's size is always its own entry, never the gap to the next
Anchor. In a well-formed archive each group's Anchor plus its sizes
lands exactly on the next group's Anchor, or on the EOF block for the
last group. Checking the next Anchor is OPTIONAL, and refuses an intact
group when that Anchor is damaged.

## Sequential Reading

The File Header states what follows the EOF block, so a sequential
decoder never guesses from those bytes. Guessing would be unreliable:
the footer opens with Source Size, and about one size in 65536 parses as
a valid SEK block header.

With HAS_SEEK_TABLE = 1, a decoder reads the SEK block header and MUST
reject it unless its Block Type is 254 and its Compressed Payload Size
equals the fold of T, with N derived from the bytes it produced. It then
skips T bytes, counted in 64 bits, and reads the footer. With
HAS_SEEK_TABLE = 0, the footer comes next, and a SEK block found there
MUST be rejected.

# EOF Block (Type 255) {#eof-block}

The EOF block marks the end of the data block stream.

A conforming EOF block:

- MUST have an 8-byte block header.
- MUST have Compressed Payload Size == 0.
- MUST NOT carry a payload.
- MUST NOT be followed by a trailing 4-byte checksum, regardless of
  the HAS_CHECKSUM flag.

Immediately after the EOF block header, the encoder MUST emit the SEK
block followed by the File Footer when HAS_SEEK_TABLE = 1, or the File
Footer alone when HAS_SEEK_TABLE = 0.

# Prefix Varint Encoding {#varint}

ZXC Extras streams use a prefix-length variable-length integer
encoding. The total length is signalled in unary form in the high
bits of the first byte, so it is known from that byte alone and
every following byte carries eight payload bits. This differs from
LEB128, which spends a continuation bit in each byte and reveals the
length only as the value is scanned. The scheme is generalisable to
any number of bytes by extending the prefix-length table below.

| First-byte prefix | Total bytes | Payload bits | Version 9 |
|-------------------|------------:|-------------:|-----------|
| 0xxxxxxx          |      1      |      7       | accepted  |
| 10xxxxxx          |      2      |     14       | accepted  |
| 110xxxxx          |      3      |     21       | accepted  |
| 1110xxxx          |      4      |     28       | rejected  |
| 11110xxx          |      5      |     35       | rejected  |

The 4- and 5-byte forms are listed to define the scheme; Format
Version 9 caps encodings at three bytes and a decoder MUST reject
the other two ({{varint-cap}}).

The first byte's payload bits — those below its unary prefix — are
the least significant bits of the value, and each following byte
contributes the next eight bits up. Writing b0, b1, b2 for the
bytes in stream order, the accepted forms decode as:

~~~
1 byte:   value = b0
2 bytes:  value = (b0 AND 0x3F) OR (b1 << 6)
3 bytes:  value = (b0 AND 0x1F) OR (b1 << 5) OR (b2 << 13)
~~~

For example, the two bytes AC 04 decode as the 2-byte form:
(0xAC AND 0x3F) OR (0x04 << 6) = 44 + 256 = 300. The three bytes
C3 35 0C decode as the 3-byte form: (0xC3 AND 0x1F) OR (0x35 << 5)
OR (0x0C << 13) = 3 + 1696 + 98304 = 100003.

## Length Cap {#varint-cap}

A decoder MUST be parameterised by a maximum varint length L_MAX
(in bytes) and reject any encoding whose first byte signals a
total length greater than L_MAX.

For Format Version 9, L_MAX is **3** (21-bit payload). The maximum
legitimate decoded value is therefore (2^21 - 1) = 2,097,151: one
less than the largest block size a Chunk Size Code can select
(2 MiB at code 21, {{file-header}}), and so the largest overflow
value that can appear.

A conforming decoder:

- MUST reject a first byte whose prefix signals 4 or 5 bytes
  (high nibble 0xE.. or 0xF..).
- MUST treat such an encoding as a fatal format error
  (see {{error-handling}}).

A conforming encoder:

- MUST NOT emit a varint encoding longer than 3 bytes.
- MUST NOT emit a value greater than (2^21 - 1).

Future format versions MAY raise L_MAX (and correspondingly the
per-block size limit) without breaking the encoding scheme; the
1- to 3-byte forms remain bit-compatible across versions.

# Checksums and Integrity {#checksums}

## Header Checksums {#header-checksums}

The File Header carries a 16-bit checksum at offset 0x0E..0x0F
({{file-header}}); every block header carries an 8-bit checksum at
offset 0x07 ({{block-container}}). They are computed differently because
they are checked at different rates: the block checksum runs once per
block, on the decode path, and must cost almost nothing, while the file
checksum runs once per archive and can afford a stronger guarantee. All
arithmetic below is on unsigned 64-bit integers modulo 2^64, and all
shifts are logical.

### Block Header Checksum

The 8-bit block header checksum takes the 8 header bytes as a single
little-endian 64-bit integer v, with the checksum byte at offset 0x07
treated as zero, multiplies, and keeps the top byte:

~~~
h = (v XOR 0x9E3779B97F4A7C15) * 0x9E3779B97F4A7C15
checksum8 = h >> 56
~~~

The checksum MUST be taken from the top of the product. The low bits of
a product depend only on the low bits of its input, so a checksum taken
from the bottom would ignore most of the header, while the top byte
depends on every input bit.

Every single-bit error is detected by construction. Flipping bit i of v
moves the product by exactly plus or minus the constant shifted left by
i, and no such shift over the 56 covered bits leaves 0x00 or 0xFF in the
top byte, so the carry cannot absorb the difference. Errors of two or
more bits are detected with better than the 1/256 odds of a random
function, but not by construction. The XOR with the constant before
multiplying keeps an all-zero header from hashing to zero.

### File Header Checksum

The 16-bit File Header checksum takes the 16 header bytes as two
little-endian 64-bit integers, v1 at offset 0x00 and v2 at offset 0x08,
with the two checksum bytes at 0x0E..0x0F treated as zero. The two
halves are chained, not summed: the first is mixed, and the second is
added to the result and mixed again:

~~~
h = (v1 XOR 0xD2D84A61D2D84A61) * 0xD2D84A61D2D84A61
h = (h + v2 + 0x9E3779B97F4A7C15) * 0x9E3779B97F4A7C15
checksum16 = h >> 48
~~~

With two independent products summed, a bit in each half could shift the
two products by amounts whose top 16 bits cancel whatever the header
holds, a structural blind spot for two-bit errors. In the chain every
flip still shifts the result by an exact amount, but the two halves no
longer meet as equals, and the constants are chosen so that no
single-bit or two-bit error can leave the top 16 bits unchanged,
whatever the header holds. The order of the constants is significant:
0xD2D84A61D2D84A61 MUST be the inner constant and 0x9E3779B97F4A7C15 the
outer one; swapped, two-bit cancellations reappear. Beyond two bits,
errors go undetected at the 1/65536 rate of a random function.

The same function protects the dictionary file header ({{zxd-format}}).

These checksums protect metadata and navigation fields. A conforming
decoder MUST validate both before relying on any other field of the
corresponding header.

## Per-Block Checksum {#per-block-checksum}

When the File Header has HAS_CHECKSUM = 1:

- Every data block (RAW, GLO, GHI) MUST carry a 4-byte trailing
  checksum.
- The checksum input is the block's decompressed bytes, excluding any
  dictionary prefix ({{dictionary}}). For a RAW block these are its
  payload bytes.
- The seed is the block's zero-based position among the file's data
  blocks, as a 64-bit value. A block encoded on its own, outside any
  file, uses seed 0.
- The algorithm identifier 0 denotes fold32 ({{fold32}}) of RapidHash
  {{RAPIDHASH}} computed over that input with that seed.

The 32-bit fold of a 64-bit hash h is defined as:

~~~
fold32(h) = (h XOR (h >> 32)) AND 0xFFFFFFFF
~~~
{: #fold32}

Wherever this document refers to RapidHash, implementations MUST use
RapidHash version 3 {{RAPIDHASH}} with the function's default
secret; the seed is 0 unless a seed is stated explicitly. Other
versions of the function produce different digests and are not
interoperable.

The seed binds a block to its position: a block moved elsewhere in the
file fails its own check, including under a range read through the seek
table, which sees only the blocks it touches.

A decoder therefore verifies a block after decoding it, and a corrupted
block is reported as whatever error the decoder encounters first. In
exchange the checksum covers the whole pipeline: it also detects a wrong
dictionary accepted through a Dictionary ID collision, an encoder or
decoder defect, and a divergence between implementations, none of which
alter the compressed bytes.

## Archive Digest {#archive-digest}

When HAS_CHECKSUM = 1, the File Footer carries an 8-byte Archive Digest
after Source Size. It is an ordered 64-bit fold of the checksums of all
data blocks, in stream order, where c is the 4-byte value stored after a
block, widened to 64 bits:

~~~
digest = 0
for each data block checksum c (in stream order):
    x      = (c + 1) * 0x9E3779B97F4A7C15
    digest = mix(digest XOR x, 0x2545F4914F6CDD1D)

mix(a, b):
    p = a * b                 (full 128-bit product)
    return (p mod 2^64) XOR (p >> 64)
~~~

The digest identifies the archive as a whole: two archives of the same
content at the same block size share it, and any block reordered,
dropped, or altered changes it. A decoder that verifies checksums MUST
compare it at the end of a full decode. A range read through the seek
table cannot verify it, since it does not see every block.

# File Footer {#file-footer}

The File Footer is mandatory and ends the file. It is 8 bytes long when
HAS_CHECKSUM = 0 and 16 bytes long when HAS_CHECKSUM = 1. It follows the
EOF block header, or the SEK block when HAS_SEEK_TABLE = 1.

~~~
 Offset  Size  Field
 0x00    8     Source Size
 0x08    8     Archive Digest (only when HAS_CHECKSUM = 1)
~~~

Source Size:
: The total uncompressed size of the source data, in bytes. After
  decoding, a conforming decoder MUST verify that its produced
  output size matches this value.

Archive Digest:
: The fold of the per-block checksums defined in {{archive-digest}}.
  Present if and only if HAS_CHECKSUM = 1.

# Pre-Trained Dictionary Support {#dictionary}

ZXC defines an OPTIONAL pre-trained dictionary mechanism that
improves the compression ratio of small, mutually similar payloads
(for example JSON API responses, game assets, or structured log
records) by prefilling the LZ77 sliding window at the start of each
block.

A dictionary is a standalone file, conventionally carrying the .zxd
extension ({{zxd-format}}), referenced from a ZXC archive by a
32-bit identifier stored in the File Header
({{dictionary-header-encoding}}). Dictionary support is purely
additive: an archive compressed without a dictionary is bit-identical
to one produced by an encoder that has no dictionary support, and a
decoder that does not recognise the HAS_DICTIONARY flag is governed
by {{compatibility-rules}}.

## Mechanism

A dictionary contains raw byte content of at most 65535 bytes,
bounded by the 64 KiB LZ77 sliding window. At compression time the
dictionary is logically prepended to each block's input, seeding the
match finder so that it MAY reference dictionary content from the
first byte of the block. At decompression time the dictionary is
prepended to the output buffer, so that match copies referencing
dictionary bytes resolve by ordinary pointer arithmetic.

Because every block is independently decodable, the dictionary
prefill is applied per block. This preserves the constant-time
random access property of {{sek-block}}: a decoder loads the
dictionary once and then decodes any block in isolation.

## File Header Encoding {#dictionary-header-encoding}

When the HAS_DICTIONARY flag (Flags bit 6, 0x40; see
{{file-header}}) is set, the four reserved bytes at offsets
0x07..0x0A of the File Header carry the Dictionary ID as a u32 in
little-endian order, and the remaining reserved bytes at offsets
0x0B..0x0D are zero.

A decoder processing an archive whose File Header has HAS_DICTIONARY
set MUST:

1. Verify that a dictionary has been supplied by the caller. If not,
   it MUST reject the archive (dictionary required; see
   {{error-handling}}).
2. Verify that the identifier of the supplied dictionary equals the
   Dictionary ID in the File Header. If not, it MUST reject the
   archive (dictionary mismatch). The identifier binds both the
   dictionary content and its shared literal table ({{zxd-format}}),
   so a matching Dictionary ID guarantees the exact (content, table)
   pair required to decode Literal Encoding = 3 literal sections
   ({{shared-huffman-literal-section}}).

The identifier of a dictionary that carries a shared literal table
is defined in {{zxd-format}}. A dictionary supplied without one,
which a .zxd file cannot express and which therefore reaches the
decoder only through an in-memory interface, is identified by its
content alone:

~~~
Dictionary ID = fold32(rapidhash(content))
~~~

A decoder that does not recognise the HAS_DICTIONARY flag ignores it
per {{compatibility-rules}}. However, blocks compressed against a
dictionary contain match offsets that reference dictionary content,
so decoding them without the dictionary produces incorrect output.
When checksums are enabled, the per-block checksums
({{per-block-checksum}}) detect this divergence, because they cover the
decompressed bytes. They also cover the residual risk of a 32-bit
Dictionary ID collision, where the header check passes on the wrong
dictionary. An encoder that uses a dictionary SHOULD therefore also
enable checksums.

## Dictionary File Format {#zxd-format}

A dictionary is stored as a standalone file consisting of a 16-byte
header, the raw dictionary content, and a mandatory 128-byte shared
literal Huffman table:

~~~
 Offset  Size  Field
 0x00    4     Magic
 0x04    1     Dictionary Format Version
 0x05    1     Flags
 0x06    2     Content Size
 0x08    4     Dictionary ID
 0x0C    2     Reserved
 0x0E    2     Header Checksum
 0x10    N     Dictionary Content
 0x10+N  128   Shared Literal Huffman Table
~~~

Magic (u32):
: MUST be 0x9CB0D1C7. Stored little-endian, the on-disk byte
  sequence is C7 D1 B0 9C. It shares the 0x9CB0 family prefix with
  the ZXC archive magic ({{file-header}}); an implementation MUST
  compare the full 32-bit value to distinguish the two file types.

Dictionary Format Version (u8):
: The dictionary file format version. This document specifies version 2.
  A decoder MUST reject any other version. Version 1 dictionaries were
  signed with the header checksum of Format Version 8, not the one of
  {{header-checksums}}, so they are rejected on this byte before the
  checksum is compared. The dictionary format version is independent of
  the archive Format Version.

Flags (u8):
: Bits 0..3 encode the checksum algorithm identifier, matching the
  Flags field of the ZXC File Header; value 0 denotes the
  RapidHash-based folding {{RAPIDHASH}}. Bits 4..7 are RESERVED;
  encoders MUST set them to zero.

Content Size (u16):
: The length in bytes of the dictionary content that follows the
  header. MUST be in the range \[1, 65535\].

Dictionary ID (u32):
: A deterministic 32-bit identifier that binds the (content, table)
  pair. It is computed as fold32 ({{fold32}}) of the seeded RapidHash
  {{RAPIDHASH}} of the 128-byte Shared Literal Huffman Table, seeded
  with fold32 of the RapidHash of the Dictionary Content, so that
  each byte is hashed exactly once. It MUST equal the Dictionary ID
  stored in the File Header of any ZXC archive compressed with this
  dictionary.

Reserved:
: 2 bytes. Encoders MUST set these to zero.

Header Checksum (u16):
: The 16-bit header checksum of {{header-checksums}}, computed over
  the 16-byte header with the four bytes at 0x0C..0x0F treated as
  zero.

Dictionary Content:
: Content Size raw bytes that prefill the LZ77 window. The content
  is not compressed.

Shared Literal Huffman Table:
: A fixed 128-byte block of 256 4-bit code lengths, packed
  two-per-byte (low nibble first) using the same layout as the
  code-length header of {{huffman-literal-section}}. It is ALWAYS
  present, immediately following the Dictionary Content. The code
  lengths are trained on the corpus' post-LZ literal distribution
  and drive the Literal Encoding = 3 literal sections
  ({{shared-huffman-literal-section}}); symbols absent from the
  training distribution carry length 0.

A dictionary whose content size exceeds 65535 bytes cannot be
represented by this format and MUST be rejected when the dictionary
is loaded.

## Dictionary Training

A dictionary is produced by analysing a corpus of representative
samples and selecting byte segments that maximise LZ77 match
coverage. The reference implementation places the most frequently
matched segments at the end of the dictionary, so that they yield
the shortest match offsets (closest to the start of the block in the
virtual window). Training additionally builds the shared literal
Huffman table from the post-LZ literal distribution observed while
compressing the corpus against the trained content. Both procedures
are encoder concerns and do not affect the on-disk format; any file
that satisfies {{zxd-format}} is a conforming dictionary.

## Naming and Lookup Conventions {#dictionary-naming}

The .zxd extension is a tooling convention only. A dictionary file
is identified by its magic word at offset 0x00, never by its
extension, and the extension does not affect any bytes on the wire.
The reference CLI applies the following conventions:

- Training writes the dictionary as `dictionary_<dict_id>.zxd`, where
  `<dict_id>` is the lowercase eight-digit hexadecimal form of the
  Dictionary ID. Embedding it in the file name keeps the name
  unique per dictionary and easy to match against the value reported
  by archive-inspection tooling.
- A dictionary is never located automatically at decompression time.
  An archive compressed with a dictionary MUST be decompressed by
  supplying that dictionary explicitly. Absent it, decompression
  fails because a dictionary is required; supplied with the wrong
  dictionary, it fails because the Dictionary ID does not match.

# Decoder Operation

A conforming decoder SHOULD process a ZXC stream according to the
following procedure:

1. Read the 16-byte File Header. Validate the Magic, Format
   Version, Chunk Size Code, and Header Checksum. If the HAS_DICTIONARY
   flag is set, validate the supplied dictionary against the
   Dictionary ID ({{dictionary-header-encoding}}).
2. Loop over blocks:

   a. Read the 8-byte block header. Validate the Header Checksum.
   b. If the block is the EOF block, exit the loop.
   c. Validate Compressed Payload Size against the block size
      ({{block-container}}), then read that many bytes of payload.
   d. Decode the payload according to the Block Type
      ({{raw-block}}, {{glo-block}}, {{ghi-block}}).
   e. If HAS_CHECKSUM = 1, read the 4-byte trailing checksum, verify it
      against the decoded bytes seeded with the block's position
      ({{per-block-checksum}}), and fold it into the Archive Digest
      ({{archive-digest}}).

3. If HAS_SEEK_TABLE = 1, read the SEK block header, require the
   Compressed Payload Size that {{sek-block}} derives from the produced
   output, and skip the table. If HAS_SEEK_TABLE = 0, the footer follows
   the EOF block directly.
4. Read the File Footer, 8 bytes or 16 when HAS_CHECKSUM = 1. Verify
   that the produced output size matches Source Size. If
   HAS_CHECKSUM = 1 and checksums are being verified, verify that the
   recomputed digest matches the footer's Archive Digest.

A decoder MUST NOT return successfully if any of the validation
steps above fail. In particular, exhausting the input before reaching
an EOF block is a failure: a forged Compressed Payload Size can step
over the EOF marker, and the resulting short output MUST NOT be
reported as success.

# Versioning Policy {#versioning}

## Format Version Field

The Format Version is a single byte at offset 0x04 of the File
Header. A conforming decoder accepts only the version it implements
and MUST reject any other version. Because block-type numbering and
payload layouts may change between versions, a decoder MUST NOT
attempt to interpret an archive whose version byte it does not
recognise.

## Version Bump Criteria

Any change a decoder must understand in order to parse an archive
correctly requires a version bump:

| Change class                             | Action       | Example                         |
|------------------------------------------|--------------|---------------------------------|
| New block type added                     | Version bump | Adding a hypothetical GLR block |
| Reserved field or flag bit given meaning | Version bump | Defining a reserved flag bit    |
| Existing block encoding changed          | Version bump | Changing GLO token layout       |
| Header or footer layout changed          | Version bump | Resizing the File Header        |
| Checksum algorithm changed               | Version bump | Replacing RapidHash             |

## Compatibility Rules {#compatibility-rules}

Unknown block types:
: A decoder MUST reject any block whose type is not defined for its
  Format Version. The block-type set is fixed per version;
  introducing a new type is a version bump. Decoders MUST NOT skip
  unknown blocks: silently advancing past unrecognised data is
  unsafe.

Reserved fields:
: All reserved bytes and flag bits MUST be written as zero by
  encoders. The decoder tolerates (ignores) non-zero reserved
  values, which are covered by the header checksum; assigning a reserved
  field any meaning is a version bump, never a same-version
  extension.

## Minimum Conforming Decoder {#minimum-conforming-decoder}

A minimum conforming decoder for Format Version 9 MUST support:

- File header parsing and checksum validation.
- RAW blocks (type 0): passthrough copy.
- GLO blocks (type 1): full LZ decoding with extras varints,
  including the Huffman entropy sections ({{huffman-literal-section}},
  PivCo layout) with code lengths up to 11 bits.
- GHI blocks (type 2): full LZ decoding with extras varints.
- EOF block (type 255): stream termination.
- File footer validation (source size check).

Because level-7 archives encode both literals and tokens with the
Huffman/PivCo layout, support for {{huffman-literal-section}} is
REQUIRED. Per-block checksum verification ({{per-block-checksum}}) is
RECOMMENDED but not REQUIRED.

# Error Handling {#error-handling}

## Error Classes

A conforming decoder MUST detect and handle the following error
conditions. The recommended behaviour for each is specified below;
all errors in the table are fatal by default.

| Error                                  | Detection point             | Required behaviour                              |
|----------------------------------------|-----------------------------|-------------------------------------------------|
| Bad magic                              | File header offset 0x00     | Reject immediately. Not a ZXC file.             |
| Unsupported version                    | File header offset 0x04     | Reject immediately.                             |
| File header checksum mismatch          | File header offset 0x0E     | Reject. Header is corrupt or truncated.         |
| Invalid chunk size code                | File header offset 0x05     | Reject. Code outside the valid range \[12..21\].  |
| Block header checksum mismatch         | Block header offset 0x07    | Reject block. Stream is corrupt.                |
| Unknown block type                     | Block header offset 0x00    | Reject. Type not defined for this version.      |
| Block payload truncated                | During payload read         | Reject. Unexpected end of stream.               |
| Block checksum mismatch                | Trailing checksum, after decoding | Reject block. Wrong decoded bytes: corrupt payload, wrong dictionary, or block out of place. |
| EOF block with non-zero Compressed Payload Size      | EOF block header            | Reject. Malformed EOF marker.                   |
| Data block payload above the block size | Block header offset 0x03    | Reject. A data block never compresses past its own content. |
| Block loop ends without an EOF block   | End of the block loop       | Reject. A forged size can step over the EOF marker.         |
| Seek-table flag disagrees with the tail | Between EOF block and footer | Reject. HAS_SEEK_TABLE = 1 without the SEK block {{sek-block}} derives, or a SEK block with HAS_SEEK_TABLE = 0. |
| Seek-table group inconsistent          | SEK payload                 | Reject. A size outside one block, an Anchor outside the data area, or a group not ending where required ({{sek-block}}). |
| Block disagrees with its seek entry    | Block header, on seek access | Reject. The entry is not the block's header, payload and checksum size ({{sek-block}}). |
| Footer source-size mismatch            | File footer offset 0x00     | Reject. Output size does not match.             |
| Archive Digest mismatch                | File footer offset 0x08     | Reject (if verifying). Blocks reordered, dropped, or altered ({{archive-digest}}). |
| Decompressed output exceeds chunk size | During LZ decode            | Reject. Corrupt or malicious payload.           |
| Match offset out of bounds             | During LZ copy              | Reject. Offset references data before output.   |
| Varint exceeds L_MAX (3 bytes)         | Extras stream               | Reject. See {{varint-cap}}. Overflow or corrupt extras data.   |
| Dictionary required but not supplied   | File header offset 0x06     | Reject. HAS_DICTIONARY set; see {{dictionary-header-encoding}}. |
| Dictionary ID mismatch                 | File header offset 0x07     | Reject. Supplied dictionary does not match the Dictionary ID.  |

## Severity Levels

Fatal:
: The decoder MUST stop processing and report an error. All errors
  in the table above are fatal by default.

Warning:
: No warning-class condition is defined by this document. Future
  revisions MAY introduce non-fatal conditions (for example,
  unknown flag bits set in RESERVED positions).

## Partial Output

When a fatal error occurs mid-stream, a conforming decoder SHOULD:

1. Stop producing output immediately.
2. Report the specific error condition.
3. Not return partially decompressed data as a valid result.

Buffer-mode decoders MUST return a negative error code. Stream-mode
decoders MUST signal the error to the caller and cease writing to
the output sink.

## Decoder Hardening Recommendations

Decoders that process untrusted input (for example, network data or
user uploads) SHOULD additionally:

- Validate all header checksums before processing any payload.
- Enforce a maximum allocation limit derived from Compressed Payload
  Size and the chunk size code.
- Reject files where Compressed Payload Size exceeds the compression
  upper bound for the configured chunk size.
- Use bounded memory copies. Decoded lengths MUST NOT be trusted without
  cross-checking against the output buffer capacity.

# Security Considerations

ZXC is a lossless compression format. Like any binary format that
will be parsed from untrusted sources, a faulty or malicious ZXC
stream can be crafted to attempt to exploit a decoder. The
following considerations apply.

## Decompression Bomb Resistance

The per-block decompressed size is bounded by the Chunk Size Code
in the File Header, which is constrained to the range
\[4 KiB, 2 MiB\]. A decoder MUST enforce this bound while decoding.
A decoder SHOULD additionally enforce an external bound on the
total decompressed size (for example, derived from
Source Size) before allocating large output buffers.

## Memory Safety in LZ Decoding

The LZ decoders for GLO and GHI MUST validate that every match
reference stays strictly within the bounds of the currently
produced output. A negative offset or an offset greater than the
number of bytes already produced MUST be rejected as corrupt data.

## Integer Overflow

All length and size fields are bounded by their on-wire types. A
decoder MUST perform arithmetic on these fields using types large
enough to represent the result without overflow, and MUST treat any
arithmetic overflow as a fatal error.

## Checksum Strength {#checksum-strength}

The 8-bit and 16-bit header checksums, the 32-bit per-block checksum,
and the 64-bit Archive Digest defined in this document are designed for
the detection of accidental corruption only. They are NOT cryptographic.
A ZXC archive MUST NOT be used as the sole integrity mechanism against
an adversary capable of modifying the archive.

Applications requiring authentication SHOULD wrap or sign the ZXC
archive using a separate cryptographic mechanism such as an
authenticated encryption scheme {{RFC5116}}.

## Seek Table Integrity {#seek-table-integrity}

The seek table ({{sek-block}}) is not authenticated. Its bounds checks
reject damaged entries, but a table rewritten consistently can point a
block index at another well-formed block of the same on-disk size, whose
bytes a range read then returns without error; any seek index checked
only against itself shares this weakness. Only the per-block checksum,
seeded with the block's position ({{per-block-checksum}}), binds a block
to its index. Like every checksum in this document it guards against
accident, not against an adversary ({{checksum-strength}}).

## Reserved Fields

A decoder that follows {{compatibility-rules}} will tolerate
non-zero values in RESERVED bytes and flag bits. Encoder authors
MUST NOT rely on this tolerance to smuggle data; future revisions
of this document MAY assign meaning to any RESERVED field.

## Side Channels

The parallel-decode design of the Huffman literal section
({{huffman-literal-section}}) does not introduce data-dependent
secret memory access beyond what is intrinsic to canonical Huffman
decoding. Implementers processing secrets through a ZXC decoder
SHOULD perform their own side-channel analysis and consider
constant-time alternatives where required.

# IANA Considerations

This document requests the following actions from IANA.

## Media Type Registration

The following media type registration is requested for ZXC streams,
following the procedures of {{!RFC6838}}.

Type name:
: application

Subtype name:
: zxc

Required parameters:
: N/A

Optional parameters:
: N/A

Encoding considerations:
: binary

Security considerations:
: See {{security-considerations}}.

Interoperability considerations:
: See this document.

Published specification:
: This document.

Applications which use this media type:
: File compression and archival.

Fragment identifier considerations:
: N/A

^

Additional information:
: Deprecated alias names for this type:
  : N/A

  Magic number(s):
  : The first four bytes of a ZXC stream are F5 2E B0 9C
    (little-endian encoding of 0x9CB02EF5).

  File extension(s):
  : .zxc

  Macintosh file type code(s):
  : N/A
{: newline="true"}

^

Person & email address to contact for further information:
: Bertrand Lebonnois \<zxc.codec@gmail.com\>

Intended usage:
: COMMON

Restrictions on usage:
: N/A

Author:
: Bertrand Lebonnois

Change controller:
: Bertrand Lebonnois

Provisional registration:
: Yes

## File Extension

The conventional file extension for a ZXC archive is .zxc. A companion
pre-trained dictionary file ({{zxd-format}}) uses .zxd. Both are tooling
conventions: a file of either kind is identified by its magic word
rather than by its name ({{dictionary-naming}}).

## Block Type Registry

This document requests the creation of a "ZXC Block Types" registry
with the following initial assignments:

| Value      | Mnemonic   | Reference        |
|-----------:|------------|------------------|
| 0          | RAW        | {{raw-block}}    |
| 1          | GLO        | {{glo-block}}    |
| 2          | GHI        | {{ghi-block}}    |
| 3..253     | Unassigned | -                |
| 254        | SEK        | {{sek-block}}    |
| 255        | EOF        | {{eof-block}}    |

The registration policy for new Block Type values is Specification
Required {{!RFC8126}}.

--- back

# Worked Example {#worked-example}

This section is non-normative.

The following example was produced by the reference encoder from a
10-byte input ("Hello ZXC\\n") using the equivalent of:

~~~
zxc -z -C -1 sample.txt
~~~

The resulting archive is 62 bytes.

## Hexdump

~~~
00000000: F5 2E B0 9C 09 13 80 00 00 00 00 00 00 00 6D 86
00000010: 00 00 00 0A 00 00 00 A0 48 65 6C 6C 6F 20 5A 58
00000020: 43 0A 90 BB A1 75 FF 00 00 00 00 00 00 83 0A 00
00000030: 00 00 00 00 00 00 BD 8A 9E 74 2A A2 9A B6
~~~

## File Header (offset 0x00, 16 bytes)

~~~
F5 2E B0 9C | 09 | 13 | 80 | 00 00 00 00 00 00 00 | 6D 86
~~~

- F5 2E B0 9C decodes to Magic = 0x9CB02EF5.
- 09 means Format Version 9.
- 13 means Chunk Size Code 19 (2^19 = 524288 bytes = 512 KiB).
- 80 means HAS_CHECKSUM = 1, algorithm id 0.
- Seven RESERVED zero bytes.
- 6D 86 is the Header Checksum (LE value 0x866D).

## Data Block 0 (RAW, offset 0x10)

Block header:

~~~
00 | 00 | 00 | 0A 00 00 00 | A0
~~~

- Type 00 (RAW), flags 00, reserved 00.
- Compressed Payload Size = 10.
- Header Checksum = 0xA0.

Payload at offset 0x18..0x21:

~~~
48 65 6C 6C 6F 20 5A 58 43 0A   (ASCII "Hello ZXC\n")
~~~

Trailing checksum at 0x22..0x25:

~~~
90 BB A1 75   (LE = 0x75A1BB90)
~~~

This is a RAW block, so its payload already is the decompressed data:
the checksum input of {{per-block-checksum}} is these same ten bytes,
seeded with the block's position, 0.

## EOF Block (offset 0x26, 8 bytes)

~~~
FF | 00 | 00 | 00 00 00 00 | 83
~~~

## File Footer (offset 0x2E, 16 bytes)

~~~
0A 00 00 00 00 00 00 00 | BD 8A 9E 74 2A A2 9A B6
~~~

- Source Size = 10, the first 8 footer bytes.
- Archive Digest = 0xB69AA22A749E8ABD, the fold of the checksum of block
  0 ({{archive-digest}}).

## Structural View

~~~
0x00..0x0F  File Header                (16 B)
0x10..0x17  RAW Block Header           ( 8 B)
0x18..0x21  RAW Payload                (10 B)
0x22..0x25  RAW Block Checksum         ( 4 B)
0x26..0x2D  EOF Block Header           ( 8 B)
0x2E..0x35  Source Size                ( 8 B)
0x36..0x3D  Archive Digest             ( 8 B)
~~~

## Seekable Variant

The same input compressed with the seek table enabled (zxc -z -C -1
-S sample.txt) yields an 82-byte archive:

~~~
00000000: F5 2E B0 9C 09 13 A0 00 00 00 00 00 00 00 0D 45
00000010: 00 00 00 0A 00 00 00 A0 48 65 6C 6C 6F 20 5A 58
00000020: 43 0A 90 BB A1 75 FF 00 00 00 00 00 00 83 FE 00
00000030: 00 0C 00 00 00 6F 10 00 00 00 00 00 00 00 16 00
00000040: 00 00 0A 00 00 00 00 00 00 00 BD 8A 9E 74 2A A2
00000050: 9A B6
~~~

The File Header differs in two fields: the Flags byte A0 sets both
HAS_CHECKSUM and HAS_SEEK_TABLE (0x80 | 0x20), and the Header Checksum
becomes 0D 45 (LE value 0x450D). The data and EOF blocks are unchanged.

A 20-byte SEK block is inserted between the EOF block and the File
Footer. Its header is FE 00 00 0C 00 00 00 6F: Compressed Payload
Size = 12, for one group made of an 8-byte Anchor and one 4-byte size
(below 4 GiB the fold is the size itself), and Header Checksum = 0x6F.
The group follows at offset 0x36:

~~~
10 00 00 00 00 00 00 00 | 16 00 00 00
~~~

The Anchor 0x10 = 16 is the offset of data block 0, right after the File
Header. The size 0x16 = 22 is the total on-disk size of that block: 8
(header) + 10 (payload) + 4 (checksum). Anchor plus size is 0x26, the
offset of the EOF block, as the group's sum must be.

The File Footer is unchanged and still ends the file, so a decoder
locating it from the end of the file requires no modification to support
seekable archives. A sequential decoder learns from HAS_SEEK_TABLE that
a SEK block precedes the footer.

## Dictionary File

This subsection illustrates a minimal dictionary file
({{zxd-format}}) whose content is the five ASCII bytes "hello". The
total file size is 149 bytes: a 16-byte header, 5 bytes of content,
and the mandatory 128-byte shared literal Huffman table.

~~~
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
~~~

Dictionary header (offset 0x00, 16 bytes):

~~~
C7 D1 B0 9C | 02 | 00 | 05 00 | 34 07 FC 0C | 00 00 | C6 51
~~~

- C7 D1 B0 9C decodes to Magic = 0x9CB0D1C7.
- 02 means Dictionary Format Version 2.
- 00 means Flags = 0 (checksum algorithm id 0, no reserved bits set).
- 05 00 is Content Size = 5.
- 34 07 FC 0C is the Dictionary ID = 0x0CFC0734. It binds the
  (content, table) pair and matches the Dictionary ID stored in the
  File Header of any archive compressed with this dictionary.
- 00 00 are the two RESERVED bytes.
- C6 51 is the Header Checksum (LE value 0x51C6), computed over the
  16-byte header with bytes 0x0C..0x0F treated as zero.

Dictionary content (offset 0x10, 5 bytes):

~~~
68 65 6C 6C 6F   (ASCII "hello")
~~~

These raw bytes prefill the LZ77 window; they are not compressed.

Shared literal Huffman table (offset 0x15, 128 bytes):

~~~
... 20 00 02 00 02 20 ...   (all other bytes 0x00)
~~~

256 4-bit code lengths packed two-per-byte (low nibble first;
{{shared-huffman-literal-section}}). Symbols absent from the training
distribution carry length 0; here only the bytes of "hello" have
codes (for example the nibble at table index 'e' = 0x65 gives length
2), so every other entry is zero.

# Acknowledgements
{:numbered="false"}

The author thanks the contributors to the LZ77 and Huffman coding
literature whose work this format builds upon, and the maintainers
of the kramdown-rfc and xml2rfc toolchains used to produce this
document.

# Reference Implementation
{:numbered="false"}

A reference implementation of an encoder and decoder for the format
defined by this document is available; see {{ZXC-WP}} for design
notes.
