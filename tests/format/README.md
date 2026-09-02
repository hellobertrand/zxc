# Golden-File Format Conformance Suite

Byte-frozen reference archives that pin the **ZXC on-disk wire format**
(`docs/FORMAT.md`, format version 8). Unlike `conformance/`, which checks that a
decoder produces the right *output*, this suite asserts the exact *bytes* and
*structure* of compressed files, field by field.

## Contents

```
golden_cases.h     Single source of truth: input generators + per-file expectations
gen_golden.c       Maintainer tool that (re)produces golden/*.zxc deterministically
test_golden.c      Structural validator (ctest target: `format_golden`)
golden/*.zxc       The frozen archives — never edit by hand
golden/*.zxc.txt   Annotated field dumps — generated, never edit by hand
golden.sha256      Byte-stability manifest (sha256sum format)
```

## The corpus

Each file maps onto sections of `docs/FORMAT.md §5` and the integrity fields:

| File                       | Exercises                                                        |
|----------------------------|-----------------------------------------------------------------|
| `01_empty_eof_only.zxc`    | EOF block only + footer (zero-length source)                    |
| `02_block_raw.zxc`         | RAW block (incompressible input)                                |
| `03_block_ghi.zxc`         | GHI block (level <= 2)                                           |
| `04_block_glo.zxc`         | GLO block (level >= 3)                                           |
| `05_block_glo_huffman.zxc` | GLO block with the Huffman literal section (`enc_lit == 2`, §5.2.1) |
| `06_checksum_per_block.zxc`| Per-block checksum + non-zero global stream hash                |
| `07_multiple_blocks.zxc`   | Multiple data blocks → rolling global hash (§7.3)               |
| `08_seekable_table.zxc`    | SEK seek-table block (§5.5)                                     |
| `09_block_dict.zxc`        | Raw in-memory dictionary (no shared table): `HAS_DICTIONARY` flag + content-only `dict_id` (§3.1, §12.3) |
| `10_glo_offset16.zxc`      | GLO block with 16-bit offsets (`enc_off == 0`, large-distance matches) |
| `11_glo_rle.zxc`           | GLO block with RLE literal encoding (`enc_lit == 1`)            |
| `12_glo_huffman_dict.zxc`  | Dictionary archive with the shared-table Huffman literal section (`enc_lit == 3`, §5.2.2) |

## What the validator checks

For every file, `test_golden.c` walks the bytes and verifies, against
`docs/FORMAT.md`:

- **File header (§3):** magic, version, chunk-size code, flags (incl.
  `HAS_CHECKSUM` / `HAS_DICTIONARY`), the reserved bytes — or the `dict_id` when
  a dictionary is used — and the 16-bit header checksum (`zxc_hash16`).
- **Each block header (§4):** type, flags, reserved byte, `comp_size` bounds,
  and the 8-bit header checksum (`zxc_hash8`).
- **Every payload type (§5):** RAW; GLO/GHI header + section descriptors
  (sizes must tile the payload); the Huffman literal section flag.
- **EOF block (§5.4):** `comp_size == 0`.
- **Optional SEK block (§5.5):** type, header checksum, `comp_size == n_blocks * 4`, and
  each entry equal to the physical size of its data block.
- **Per-block checksum (§7.2):** recomputed (`zxc_checksum`) over the compressed
  payload and matched against the trailing 4 bytes.
- **Global stream hash (§7.3):** reconstructed with `zxc_hash_combine_rotate`
  from the per-block checksums and matched against the footer.
- **File footer (§8):** trailing 12 bytes, original source size, global hash.

Each file is also decompressed and compared byte-for-byte against its
deterministically regenerated input.

## Running

```sh
cmake -S . -B build
cmake --build build --target zxc_format_golden_test
ctest --test-dir build -R format_golden --output-on-failure
```

## Annotated dumps (`golden/*.zxc.txt`)

Each golden file has a committed, human-readable dump of every field the
validator parses — raw header bytes next to their decoded meaning:

```
[block 0 @16]
raw:            01 00 00 A7 00 00 00 17
type:           GLO (1)
comp_size:      167
header_crc:     0x17
  n_sequences:  1
  enc_lit:      0
```

They exist so a format change is **reviewable**: a binary diff only ever says
`Binary files differ`, whereas the dump shows the PR exactly which field moved.

The dump is emitted by the validation walk itself, so there is a single parser
of the wire format and the two cannot drift apart — and `test_golden` compares
the committed dumps against that walk on every run. A golden edited without
refreshing its dump therefore fails the suite locally, not only in CI.

## Byte stability

`golden.sha256` is the frozen reference. The
[`Golden Format Stability`](../../.github/workflows/golden.yml) CI job runs
`sha256sum -c` against it, so **any single changed byte in any golden file fails
CI**. The job also checks that the file set and the manifest stay in sync.

A failure here means **the encoder's output changed**, which is not the same as
a format change: an encoder improvement moves these bytes while still emitting a
valid archive of the same format version. That is legitimate — it only has to be
deliberate. The decoder's contract is pinned separately, by the conformance
suite; a failure there is the grave one.

## Regenerating (intentional format changes only)

The golden files are static artifacts; CI never re-compresses them. Regenerate
only when the wire format changes on purpose:

```sh
cmake --build build --target zxc_golden_gen
./build/zxc_golden_gen tests/format/golden
sha256sum tests/format/golden/*.zxc | sort -k2 > tests/format/golden.sha256
./build/zxc_format_golden_test --dump tests/format/golden   # refresh the dumps
```

Commit the `.zxc` files, the refreshed `golden.sha256` **and** the refreshed
`.zxc.txt` dumps in the same change: the dumps are what makes that change
reviewable, so read their diff — it is the one that shows what actually moved.

To add a new case, append an entry to `GOLDEN_CASES[]` in `golden_cases.h`
(input generator + options + expectations), regenerate, and refresh both the
manifest and the dumps.
