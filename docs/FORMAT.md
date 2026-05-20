---
title: "The ZXC Compressed Data Format Specification"
abbrev: ZXC Format
docname: draft-lebonnois-zxc-format-00
category: info
ipr: trust200902
area: Applications
submissionType: independent
date: 2026-05-20
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
  RAPIDHASH:
    title: "RapidHash: a fast, high-quality non-cryptographic hash function"
    target: https://github.com/Nicoshev/rapidhash
    date: false
  ZXC-WP:
    title: "The ZXC Compressor: design and implementation notes"
    target: https://github.com/blebon/zxc/blob/main/docs/WHITEPAPER.md
    author:
      -
        ins: B. Lebonnois
        name: Bertrand Lebonnois
    date: false

--- abstract

This document describes the ZXC compressed data format. ZXC is a
block-oriented, seekable, integrity-checked compression container
targeting high decompression throughput and kernel-space integration.
It defines a fixed-size file header, an extensible block container,
four payload encodings (RAW, GLO, GHI, NUM), an optional seek table,
and a fixed-size file footer.

The format described in this document corresponds to wire-format
version 5 of the reference implementation and is intended to be
published as Version 1.0 of the ZXC specification.

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
carrying the original source size and a global integrity hash.

Four payload encodings are defined: RAW (uncompressed), GLO
(general-purpose LZ with separated streams and optional Huffman
coding of literals), GHI (high-throughput LZ with packed 32-bit
sequence words), and NUM (specialised encoding for 32-bit integer
streams using delta-zigzag bitpacking).

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
: A self-contained unit produced by the encoder, consisting of an
  8-byte block header, a payload of comp_size bytes, and an OPTIONAL
  4-byte trailing checksum.

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
|   SEK Block (optional)      |
+-----------------------------+
|   File Footer (12 bytes)    |
+-----------------------------+
~~~

The File Footer is always the last 12 bytes of the file. Decoders
MAY rely on this invariant to locate the footer by seeking to
file_size - 12.

A conforming encoder MUST emit exactly one EOF block per stream,
and MUST write the footer immediately after the EOF block or, when
present, immediately after the SEK block.

# File Header {#file-header}

The File Header is 16 bytes long and is laid out as follows:

~~~
 Offset  Size  Field
 0x00    4     Magic
 0x04    1     Format Version
 0x05    1     Chunk Size Code
 0x06    1     Flags
 0x07    7     Reserved
 0x0E    2     Header CRC16
~~~

## Field Definitions

Magic (u32):
: MUST be 0x9CB02EF5. Stored little-endian, the on-disk byte
  sequence is F5 2E B0 9C.

Format Version (u8):
: The wire-format version. This document specifies version 5. A
  conforming decoder MUST reject any file whose Format Version it
  does not support.

Chunk Size Code (u8):
: Values in the range \[12, 21\] are interpreted as exponents, where
  block_size = 2^code. This yields valid block sizes from 4 KiB
  (code 12) to 2 MiB (code 21). The default block size in the
  reference implementation is 512 KiB (code 19). The value 64 is
  RESERVED for backward compatibility with legacy encoders and maps
  to 256 KiB; new encoders SHOULD NOT emit this value. All other
  values MUST be rejected.

Flags (u8):
: Bit 7 (0x80) is HAS_CHECKSUM. When set, each non-EOF block
  carries a trailing 4-byte checksum ({{per-block-checksum}}). Bits
  0..3 encode the checksum algorithm identifier; value 0 denotes a
  32-bit folding of the RapidHash function {{RAPIDHASH}}, and other
  values are RESERVED. Bits 4..6 are RESERVED; encoders MUST set
  them to zero and decoders MUST ignore them.

Reserved:
: 7 bytes. Encoders MUST set these bytes to zero. Decoders MUST
  ignore their content.

Header CRC16 (u16):
: A 16-bit hash computed by zxc_hash16 over the 16-byte header with
  the two bytes at offset 0x0E..0x0F treated as zero during hashing.

# Block Container {#block-container}

Every block in a ZXC file begins with an 8-byte block header:

~~~
 Offset  Size  Field
 0x00    1     Block Type
 0x01    1     Block Flags
 0x02    1     Reserved
 0x03    4     Compressed Payload Size (comp_size)
 0x07    1     Header CRC8
~~~

## Block Types

The following Block Type values are assigned by this document:

| Value | Mnemonic | Reference          |
|------:|----------|--------------------|
|   0   | RAW      | {{raw-block}}      |
|   1   | GLO      | {{glo-block}}      |
|   2   | NUM      | {{num-block}}      |
|   3   | GHI      | {{ghi-block}}      |
|  254  | SEK      | {{sek-block}}      |
|  255  | EOF      | {{eof-block}}      |

All other values are unassigned. An unassigned block type MAY be
introduced by a future minor revision of this format without bumping
the Format Version field, provided that a conforming decoder of the
present version can skip the block using comp_size. See
{{versioning}}.

## Block Header Semantics

Block Flags (u8):
: RESERVED. Encoders MUST write 0. Decoders MUST ignore non-zero
  values from future revisions.

Reserved (u8):
: MUST be set to 0 by encoders. MUST be ignored by decoders.

comp_size (u32):
: The size in bytes of the block payload. This size does NOT
  include the 8-byte block header nor the OPTIONAL trailing 4-byte
  checksum.

Header CRC8 (u8):
: An 8-bit hash computed by zxc_hash8 over the 8-byte block header
  with byte 0x07 treated as zero during hashing.

## Physical Block Layout

~~~
[ 8-byte Block Header ]
[ comp_size bytes Payload ]
[ 4-byte Trailing Checksum (only if HAS_CHECKSUM=1 and type != EOF) ]
~~~

The EOF block (type 255) MUST NOT carry a trailing checksum even
when HAS_CHECKSUM=1 is set in the file header.

# RAW Block (Type 0) {#raw-block}

A RAW block carries uncompressed data. The payload of a RAW block
is comp_size bytes of literal input data, with no internal
sub-header.

~~~
raw_size = comp_size
~~~

Encoders MAY emit a RAW block whenever a compressed encoding would
not yield a worthwhile size reduction. A conforming decoder MUST
support RAW blocks.

# GLO Block (Type 1) {#glo-block}

The GLO block ("general LZ") is the primary compressed encoding.
It uses an LZ77-style sequence stream with literal, token, offset,
and extras streams stored contiguously, and OPTIONAL Huffman coding
of the literal stream.

## GLO Payload Layout

~~~
+-------------------------------+
| GLO Header (16 bytes)         |
| 4 Section Descriptors (32 B)  |
| Literals stream               |
| Tokens stream                 |
| Offsets stream                |
| Extras stream                 |
+-------------------------------+
~~~

## GLO Header

~~~
 Offset  Size  Field
 0x00    4     n_sequences (u32)
 0x04    4     n_literals  (u32)
 0x08    1     enc_lit     (0=RAW, 1=RLE, 2=HUFFMAN)
 0x09    1     enc_litlen  (RESERVED)
 0x0A    1     enc_mlen    (RESERVED)
 0x0B    1     enc_off     (0=16-bit offsets, 1=8-bit offsets)
 0x0C    4     RESERVED
~~~

## Section Descriptors {#glo-section-descriptors}

Four section descriptors follow the GLO header. Each descriptor is
an 8-byte packed u64:

- Bits 0..31: compressed size of the section in bytes.
- Bits 32..63: raw (decoded) size of the section in bytes.

The descriptors appear in the order: Literals, Tokens, Offsets,
Extras.

## Stream Content

Literals:
: If enc_lit = 0, raw literal bytes. If enc_lit = 1, RLE-tokenised
  literals. If enc_lit = 2, Huffman-coded literals
  ({{huffman-literal-section}}).

Tokens:
: One byte per sequence. The high nibble is LL (literal length) and
  the low nibble is ML (match length minus the minimum match of 5).

Offsets:
: n_sequences entries, each 1 byte if enc_off = 1 or 2 bytes
  (little-endian) if enc_off = 0. Stored values are biased: the
  decoder MUST add +1 to the stored value to obtain the actual match
  offset. This makes a stored offset of zero impossible by
  construction; the minimum decoded offset is 1.

Extras:
: A sequence of prefix-varints ({{varint}}) carrying overflow
  values. When LL == 15 is read from a token, a varint MUST be read
  from Extras and added to LL. When ML == 15 is read from a token, a
  varint MUST be read from Extras and added to ML. The actual match
  length is ML + 5 (the minimum match).

## Huffman Literal Section {#huffman-literal-section}

When enc_lit = 2, the Literals stream is Huffman-coded as follows:

~~~
 Offset  Size  Field
 0x00    128   Code-length header (256 x 4-bit lengths, packed
               two-per-byte, low nibble first)
 0x80    6     Sub-stream sizes s1, s2, s3 (u16 LE each)
 0x86    s1    Stream 0 bit-stream (LSB-first)
 ...     s2    Stream 1 bit-stream
 ...     s3    Stream 2 bit-stream
 ...     s4    Stream 3 bit-stream (size implied)
~~~

The size of Stream 3 is s4 = total_section_size - 134 - s1 - s2 - s3.

Codes are canonical, length-limited at L = 8 bits, and emitted
LSB-first. The n_literals value from the GLO header is split into
four contiguous regions of size Q = ceil(n_literals / 4) (the
fourth region MAY be shorter), each encoded into its own bit-stream
to enable parallel decoding.

### Decoder Validation Requirements

A conforming decoder MUST enforce the following constraints on the
code-length header:

1. Every code length MUST satisfy `code_len[i]` <= 8.
2. At least one symbol MUST be present (`code_len[i]` != 0 for some
   i).
3. The Kraft sum, defined as the sum of 2^(8 - `code_len[i]`) over
   present symbols, MUST equal 2^8, except for the
   single-present-symbol degenerate case in which exactly one symbol
   has code_len = 1 and the Kraft sum equals 2^7.

A violation of any of the above MUST be reported as a corrupt-data
error.

# NUM Block (Type 2) {#num-block}

The NUM block is a specialised encoding for streams of 32-bit
integers using delta encoding, zigzag mapping, and bitpacking.

## NUM Payload Layout

~~~
+--------------------------+
| NUM Header (16 bytes)    |
+--------------------------+
| Frame #0 Header (16 B)   |
| Frame #0 Packed Bits     |
| Frame #1 Header (16 B)   |
| Frame #1 Packed Bits     |
| ...                      |
+--------------------------+
~~~

## NUM Header

~~~
 Offset  Size  Field
 0x00    8     n_values   (u64)
 0x08    2     frame_size (u16, currently 128)
 0x0A    6     RESERVED
~~~

## NUM Frame Record

~~~
 Offset  Size  Field
 0x00    2     nvals          (u16)
 0x02    2     bits_per_value (u16)
 0x04    8     base_seed      (u64)
 0x0C    4     packed_size    (u32, in bytes)
 0x10    var   Packed delta bit-stream (packed_size bytes)
~~~

Values are reconstructed by bit-unpacking the delta stream, applying
the zigzag-inverse mapping, and accumulating from base_seed by
prefix sum.

Support for NUM blocks by a decoder is RECOMMENDED but not REQUIRED
for a minimal conforming implementation; see
{{minimum-conforming-decoder}}.

# GHI Block (Type 3) {#ghi-block}

The GHI block ("high-throughput LZ") is an alternate compressed
encoding optimised for raw decompression speed. It uses packed
32-bit sequence words rather than separated streams.

## GHI Payload Layout

~~~
+-------------------------------+
| GHI Header (16 bytes)         |
| 3 Section Descriptors (24 B)  |
| Literals stream               |
| Sequences stream (N x 4 B)    |
| Extras stream                 |
+-------------------------------+
~~~

## GHI Header

The GHI header is binary-identical to the GLO header. In practice
for GHI, enc_lit is always 0 (raw literals), and enc_off is
metadata only because sequence words always store 16-bit offsets.

## Section Descriptors {#ghi-section-descriptors}

Three section descriptors follow the GHI header, in the order:
Literals, Sequences, Extras. Each descriptor uses the same packed
u64 encoding as for GLO.

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

The SEK block is an OPTIONAL block appended between the EOF block
and the File Footer. It provides constant-time random access by
recording the compressed size of every data block in the archive.

## SEK Layout

~~~
 Offset           Size    Field
 0x00             8       Block Header (type=254, comp_size = N x 4)
 0x08             4       Block 0 Compressed Size (u32 LE)
 0x0C             4       Block 1 Compressed Size (u32 LE)
 ...              ...     ...
 8 + (N-1)*4      4       Block N-1 Compressed Size (u32 LE)
~~~

The decompressed size of each block and the total number of blocks
N are derived from the File Header's block_size field and the
footer's original_source_size: all blocks have decompressed size
block_size except the last, which MAY be shorter.

## Backward Detection Strategy

A decoder that wishes to access the SEK block without scanning the
archive linearly MAY use the following procedure:

1. Read the File Header (first 16 bytes) and extract block_size.
2. Read the File Footer (last 12 bytes) and extract
   original_source_size.
3. Compute N = ceil(original_source_size / block_size).
4. Compute seek_block_size = 8 + (N x 4).
5. Seek backward seek_block_size bytes from the start of the footer
   to locate the SEK block header.
6. Validate that the located block has Block Type == 254 and
   comp_size == N x 4. If validation fails, the SEK block is absent
   or corrupt and the decoder MUST fall back to linear scanning.

# EOF Block (Type 255) {#eof-block}

The EOF block marks the end of the data block stream.

A conforming EOF block:

- MUST have an 8-byte block header.
- MUST have comp_size == 0.
- MUST NOT carry a payload.
- MUST NOT be followed by a trailing 4-byte checksum, regardless of
  the HAS_CHECKSUM flag.

Immediately after the EOF block header, the encoder MUST emit
either the OPTIONAL SEK block followed by the File Footer, or the
File Footer alone.

# Prefix Varint Encoding {#varint}

ZXC Extras streams use a prefix-length variable-length integer
encoding. The length of the encoded value is signalled in unary
form in the high bits of the first byte:

| First-byte prefix | Total bytes | Payload bits |
|-------------------|------------:|-------------:|
| 0xxxxxxx          |      1      |      7       |
| 10xxxxxx          |      2      |     14       |
| 110xxxxx          |      3      |     21       |
| 1110xxxx          |      4      |     28       |
| 11110xxx          |      5      |     35       |

Payload bits from following bytes are concatenated little-endian
style (low-order bits first). A decoder encountering a first byte
with more than four leading 1 bits MUST report an error.

# Checksums and Integrity {#checksums}

## Header Checksums

The File Header carries a 16-bit checksum (zxc_hash16) at offset
0x0E..0x0F ({{file-header}}). Every block header carries an 8-bit
checksum (zxc_hash8) at offset 0x07 ({{block-container}}).

These checksums protect metadata and navigation fields. A
conforming decoder MUST validate both before relying on any other
field of the corresponding header.

## Per-Block Payload Checksum {#per-block-checksum}

When the File Header has HAS_CHECKSUM = 1:

- Every non-EOF block MUST carry a 4-byte trailing checksum.
- The checksum input is the compressed payload bytes only, NOT the
  block header.
- The algorithm identifier 0 denotes a 32-bit folding of RapidHash.

## Global Stream Hash

When HAS_CHECKSUM = 1, a rolling global hash is maintained from
per-block checksums in stream order:

~~~
global = 0
for each data block checksum b (in stream order):
    global = ((global << 1) | (global >> 31)) XOR b
~~~

The 32-bit truncation of global is stored in the File Footer
({{file-footer}}). When HAS_CHECKSUM = 0, the global hash field
MUST be written as zero by encoders and MUST NOT be validated by
decoders.

# File Footer {#file-footer}

The File Footer is 12 bytes and MUST be the last 12 bytes of the
file.

~~~
 Offset  Size  Field
 0x00    8     original_source_size (u64)
 0x08    4     global_hash          (u32)
~~~

original_source_size:
: The total uncompressed size of the source data, in bytes. After
  decoding, a conforming decoder MUST verify that its produced
  output size matches this value.

global_hash:
: The rolling global hash defined in {{checksums}}, or zero when
  HAS_CHECKSUM = 0.

# Decoder Operation

A conforming decoder SHOULD process a ZXC stream according to the
following procedure:

1. Read the 16-byte File Header. Validate the Magic, Format
   Version, Chunk Size Code, and Header CRC16.
2. Loop over blocks:

   a. Read the 8-byte block header. Validate the Header CRC8.
   b. If the block is the EOF block, exit the loop.
   c. Read comp_size bytes of payload.
   d. If HAS_CHECKSUM = 1, read the 4-byte trailing checksum and
      verify it against the payload; update the rolling global
      hash.
   e. Decode the payload according to the Block Type
      ({{raw-block}}, {{glo-block}}, {{num-block}}, {{ghi-block}}).

3. If a SEK block is present, the decoder MAY validate or skip it.
4. Read the 12-byte File Footer. Verify that the produced output
   size matches original_source_size. If HAS_CHECKSUM = 1, verify
   that the recomputed rolling global hash matches the footer's
   global_hash.

A decoder MUST NOT return successfully if any of the validation
steps above fail.

# Versioning Policy {#versioning}

## Format Version Field

The Format Version is a single byte at offset 0x04 of the File
Header. A conforming decoder MUST reject any file whose Format
Version it does not support, with the sole exception described in
{{compatibility-rules}}.

## Version Bump Criteria

| Change class                    | Action     | Example                          |
|---------------------------------|------------|----------------------------------|
| New block type added            | No bump    | Adding a hypothetical GLR block  |
| New flag bit defined            | No bump    | Using a reserved flag bit        |
| Existing block encoding changed | Major bump | Changing GLO token layout        |
| Header or footer layout changed | Major bump | Resizing the File Header         |
| Checksum algorithm changed      | Major bump | Replacing RapidHash              |

## Compatibility Rules {#compatibility-rules}

Backward compatibility:
: A decoder supporting Format Version N MUST decode all files
  produced by encoders at the same version. It MAY also accept
  earlier versions.

Forward compatibility:
: A decoder encountering an unknown Block Type (not RAW, GLO, NUM,
  GHI, SEK, or EOF) SHOULD skip the block using comp_size and the
  optional trailing checksum, rather than rejecting the file
  outright. This allows decoders to partially process files from
  newer encoders that introduce additive block types.

Reserved fields:
: All reserved bytes and flag bits MUST be written as zero by
  encoders. Decoders MUST NOT reject a file solely because a
  reserved field is non-zero, unless a future revision of this
  document assigns the field a meaning.

## Minimum Conforming Decoder {#minimum-conforming-decoder}

A minimum conforming decoder for Format Version 5 MUST support:

- File header parsing and CRC16 validation.
- RAW blocks (type 0): passthrough copy.
- GLO blocks (type 1): full LZ decoding with extras varints.
- GHI blocks (type 3): full LZ decoding with extras varints.
- EOF block (type 255): stream termination.
- File footer validation (source size check).

Support for NUM blocks (type 2), Huffman-coded literals
({{huffman-literal-section}}), and per-block checksum verification
({{per-block-checksum}}) is RECOMMENDED but not REQUIRED.

# Error Handling {#error-handling}

## Error Classes

A conforming decoder MUST detect and handle the following error
conditions. The recommended behaviour for each is specified below;
all errors in the table are fatal by default.

| Error                                  | Detection point             | Required behaviour                              |
|----------------------------------------|-----------------------------|-------------------------------------------------|
| Bad magic                              | File header offset 0x00     | Reject immediately. Not a ZXC file.             |
| Unsupported version                    | File header offset 0x04     | Reject immediately.                             |
| Header CRC16 mismatch                  | File header offset 0x0E     | Reject. Header is corrupt or truncated.         |
| Invalid chunk size code                | File header offset 0x05     | Reject. Code outside \[12..21\] and not legacy 64.|
| Block header CRC8 mismatch             | Block header offset 0x07    | Reject block. Stream is corrupt.                |
| Unknown block type                     | Block header offset 0x00    | Skip block per {{compatibility-rules}}, or reject.|
| Block payload truncated                | During payload read         | Reject. Unexpected end of stream.               |
| Block checksum mismatch                | Trailing 4-byte checksum    | Reject block. Payload is corrupt.               |
| EOF block with non-zero comp_size      | EOF block header            | Reject. Malformed EOF marker.                   |
| Footer source-size mismatch            | File footer offset 0x00     | Reject. Output size does not match.             |
| Footer global hash mismatch            | File footer offset 0x08     | Reject (if checksum mode active).               |
| Decompressed output exceeds chunk size | During LZ decode            | Reject. Corrupt or malicious payload.           |
| Match offset out of bounds             | During LZ copy              | Reject. Offset references data before output.   |
| Varint exceeds maximum length          | Extras stream               | Reject. Overflow or corrupt extras data.        |

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
- Enforce a maximum allocation limit derived from comp_size and the
  chunk size code.
- Reject files where comp_size exceeds the compression upper bound
  for the configured chunk size.
- Use bounded memory copies. Decoded lengths MUST NOT be trusted
  without cross-checking against the output buffer capacity.

# Worked Example

This section is non-normative.

The following example was produced by the reference encoder from a
10-byte input ("Hello ZXC\\n") using the equivalent of:

~~~
zxc -z -C -1 sample.txt
~~~

The resulting archive is 58 bytes.

## Hexdump

~~~
00000000: F5 2E B0 9C 05 13 80 00 00 00 00 00 00 00 B8 90
00000010: 00 00 00 0A 00 00 00 69 48 65 6C 6C 6F 20 5A 58
00000020: 43 0A 90 BB A1 75 FF 00 00 00 00 00 00 02 0A 00
00000030: 00 00 00 00 00 00 90 BB A1 75
~~~

## File Header (offset 0x00, 16 bytes)

~~~
F5 2E B0 9C | 05 | 13 | 80 | 00 00 00 00 00 00 00 | B8 90
~~~

- F5 2E B0 9C decodes to Magic = 0x9CB02EF5.
- 05 means Format Version 5.
- 13 means Chunk Size Code 19 (2^19 = 524288 bytes = 512 KiB).
- 80 means HAS_CHECKSUM = 1, algorithm id 0.
- Seven RESERVED zero bytes.
- B8 90 is the Header CRC16.

## Data Block 0 (RAW, offset 0x10)

Block header:

~~~
00 | 00 | 00 | 0A 00 00 00 | 69
~~~

- Type 00 (RAW), flags 00, reserved 00.
- comp_size = 10.
- Header CRC8 = 0x69.

Payload at offset 0x18..0x21:

~~~
48 65 6C 6C 6F 20 5A 58 43 0A   (ASCII "Hello ZXC\n")
~~~

Trailing checksum at 0x22..0x25:

~~~
90 BB A1 75   (LE = 0x75A1BB90)
~~~

## EOF Block (offset 0x26, 8 bytes)

~~~
FF | 00 | 00 | 00 00 00 00 | 02
~~~

## File Footer (offset 0x2E, 12 bytes)

~~~
0A 00 00 00 00 00 00 00 | 90 BB A1 75
~~~

- original_source_size = 10.
- global_hash = 0x75A1BB90.

With a single data block, the global hash equals that block's
checksum, since rotl1(0) XOR b = b.

## Structural View

~~~
0x00..0x0F  File Header                (16 B)
0x10..0x17  RAW Block Header           ( 8 B)
0x18..0x21  RAW Payload                (10 B)
0x22..0x25  RAW Block Checksum         ( 4 B)
0x26..0x2D  EOF Block Header           ( 8 B)
0x2E..0x39  File Footer                (12 B)
~~~

## Seekable Variant

The same input compressed with the seek table enabled (zxc -z -C -1
-S sample.txt) yields a 70-byte archive:

~~~
00000000: F5 2E B0 9C 05 13 80 00 00 00 00 00 00 00 B8 90
00000010: 00 00 00 0A 00 00 00 69 48 65 6C 6C 6F 20 5A 58
00000020: 43 0A 90 BB A1 75 FF 00 00 00 00 00 00 02 FE 00
00000030: 00 04 00 00 00 D2 16 00 00 00 0A 00 00 00 00 00
00000040: 00 00 90 BB A1 75
~~~

A SEK block of 12 bytes is inserted between the EOF block and the
File Footer. The SEK block header is FE 00 00 04 00 00 00 D2, with
comp_size = 4 (one 4-byte entry) and CRC8 = 0xD2. The single entry
16 00 00 00 (= 22) is the total on-disk size of data block 0: 8
(header) + 10 (payload) + 4 (checksum).

The File Footer remains the last 12 bytes of the file, so a decoder
locating the footer from the end of the file requires no
modification to support seekable archives.

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
original_source_size) before allocating large output buffers.

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

## Checksum Strength

The CRC8 and CRC16 header checksums and the 32-bit per-block
checksum defined in this document are designed for the detection of
accidental corruption only. They are NOT cryptographic. A ZXC
archive MUST NOT be used as the sole integrity mechanism against an
adversary capable of modifying the archive.

Applications requiring authentication SHOULD wrap or sign the ZXC
archive using a separate cryptographic mechanism such as an
authenticated encryption scheme {{RFC5116}}.

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
: File compression, archival, on-the-wire compression.

Fragment identifier considerations:
: N/A

Restrictions on usage:
: N/A

Provisional registration:
: Yes

Author:
: Bertrand Lebonnois

Change controller:
: Bertrand Lebonnois

Magic number(s):
: The first four bytes of a ZXC stream are F5 2E B0 9C
  (little-endian encoding of 0x9CB02EF5).

File extension(s):
: .zxc

## File Extension

The conventional file extension for a ZXC archive is .zxc.

## Block Type Registry

This document requests the creation of a "ZXC Block Types" registry
with the following initial assignments:

| Value      | Mnemonic   | Reference        |
|-----------:|------------|------------------|
| 0          | RAW        | {{raw-block}}    |
| 1          | GLO        | {{glo-block}}    |
| 2          | NUM        | {{num-block}}    |
| 3          | GHI        | {{ghi-block}}    |
| 4..253     | Unassigned | -                |
| 254        | SEK        | {{sek-block}}    |
| 255        | EOF        | {{eof-block}}    |

The registration policy for new Block Type values is Specification
Required {{!RFC8126}}.

--- back

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
