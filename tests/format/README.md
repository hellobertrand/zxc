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

| File                       | Exercises                                             |
|----------------------------|-------------------------------------------------------|
| `01_empty_eof_only.zxc`    | EOF block only + footer (zero-length source)          |
| `02_block_raw.zxc`         | RAW block                                             |
| `03_block_ghi.zxc`         | GHI block (level <= 2)                                |
| `04_block_glo.zxc`         | GLO block (level >= 3)                                |
| `05_block_glo_huffman.zxc` | Huffman literals, `enc_lit == 2` (§5.2.1)             |
| `06_checksum_per_block.zxc`| Per-block checksum + non-zero global hash             |
| `07_multiple_blocks.zxc`   | Several blocks → rolling global hash (§7.3)           |
| `08_seekable_table.zxc`    | SEK seek-table block (§5.5)                           |
| `09_block_dict.zxc`        | Raw in-memory dictionary, content-only `dict_id`      |
| `10_glo_offset16.zxc`      | 16-bit offsets, `enc_off == 0`                        |
| `11_glo_rle.zxc`           | RLE literals, `enc_lit == 1`                          |
| `12_glo_huffman_dict.zxc`  | Shared-table Huffman literals, `enc_lit == 3` (§5.2.2)|
| `13_glo_huffman_wide.zxc`  | Level 7 (ULTRA) Huffman literals                      |

`test_golden.c` walks each file against `docs/FORMAT.md` — headers, block
structure, section descriptors, the SEK entries, both checksum layers and the
footer — then decompresses it and compares with its regenerated input. The
annotated dumps below list exactly which fields that walk reads.

## Running

```sh
cmake -S . -B build
cmake --build build --target zxc_format_golden_test
ctest --test-dir build -R format_golden --output-on-failure
```

## Annotated dumps (`golden/*.zxc.txt`)

A committed, human-readable dump of every field the validator parses — raw
header bytes next to their decoded meaning:

```
[block 0 @16]
raw:              01 00 00 A7 00 00 00 17
type:             GLO (1)
comp_size:        167
header_checksum:  0x17
  n_sequences:    1
  enc_lit:        0
```

They make a change **reviewable**: a binary diff only says `Binary files
differ`, the dump shows which field moved. Emitted by the validation walk
itself, so there is one parser of the wire format and no drift — and
`test_golden` compares the committed dumps against that walk on every run, so a
golden edited without refreshing its dump fails locally, not just in CI.

## Byte stability

`golden.sha256` is the frozen reference; the
[`Test Vector Stability`](../../.github/workflows/vector-stability.yml) CI job runs
`sha256sum -c` against it and checks the file set still matches.

A failure means **the encoder's output changed**, which is not the same as a
format change: an encoder improvement moves these bytes while still emitting a
valid archive of the same version. That is legitimate, it only has to be
deliberate. The decoder's contract is pinned by the conformance suite; a failure
there is the grave one.

## Regenerating

```sh
cmake --build build --target zxc_golden_gen
./build/zxc_golden_gen tests/format/golden
sha256sum tests/format/golden/*.zxc | sort -k2 > tests/format/golden.sha256
./build/zxc_format_golden_test --dump tests/format/golden   # refresh the dumps
```

Commit the archives, the manifest **and** the dumps in the same change, and read
the dump diff — it is the one that shows what actually moved.

To add a case, append an entry to `GOLDEN_CASES[]` in `golden_cases.h`, then
regenerate and refresh both the manifest and the dumps.
