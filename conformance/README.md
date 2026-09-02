# ZXC Decoder Conformance Suite

Reference test vectors for validating any ZXC decoder implementation.

## Contents

```
v8/                 Corpus for format version 8 (FORMAT_VERSION declares it)
  valid/            *.zxc archives, *.expected outputs, *.zxd dictionaries
  invalid/          *.zxc archives that must be rejected
  vectors.sha256    Byte-stability manifest
valid_cases.h       Recipe for each valid vector; gen_valid.c rebuilds them
invalid_cases.h     Recipe for the 22 generated invalid ones; gen_invalid.c likewise
```

Vectors are frozen **per format version**. Older directories are kept, never regenerated.

## Validating a decoder

For each `valid/*.zxc`: decompress it and compare byte-for-byte against the
matching `.expected`. Any mismatch is a decoder bug. The three `dict_*` vectors
need their dictionary supplied — the `.zxd` in the same directory whose
`dict_id` matches the archive header.

For each `invalid/*.zxc`: your decoder **must** reject it. Accepting a malformed
file is a decoder bug. Two need more than a sequential decode and can be skipped
if you do not implement the feature:

- `sek_forged_entry` — the seek table is advisory metadata a sequential decode
  never reads, so only a seekable reader rejects this one.
- `dict_id_mismatch` — offer `valid/dict_http.zxd` to reach the binding check;
  with no dictionary a decoder stops earlier, which is `dict_required`'s case.

```sh
cd conformance/v8            # from the repository root, or wherever you
                             # unpacked the corpus; pick the version you decode
[ -d valid ] || { echo "no corpus here"; exit 1; }

out=$(mktemp); trap 'rm -f "$out"' EXIT
pass=0; fail=0
for f in valid/*.zxc; do
    # dict_* need their .zxd passed; drop this line once yours can.
    case "$f" in *dict_*) continue;; esac
    : > "$out"   # a stale output from the previous file would compare equal
    if your-decoder "$f" "$out" && cmp -s "$out" "${f%.zxc}.expected"
    then pass=$((pass + 1))
    else echo "FAIL: $f"; fail=$((fail + 1)); fi
done
for f in invalid/*.zxc; do
    case "$f" in *sek_forged_entry*|*dict_id_mismatch*) continue;; esac
    if your-decoder "$f" /dev/null 2>/dev/null; then
        echo "FAIL (should reject): $f"; fail=$((fail + 1))
    else pass=$((pass + 1)); fi
done
echo "Passed: $pass  Failed: $fail"
```

## Coverage

15 valid vectors, one per decoder-visible trait — a decoder never sees the
compression level, only the block type and the encodings it selects:

| Vector               | What it alone covers                                    |
|----------------------|---------------------------------------------------------|
| `empty`              | No data block: EOF only, `src_size = 0`                 |
| `all_256_values`     | RAW block; all 256 byte values                          |
| `all_zeros_4k`       | Offset-1 overlap run (long match at minimum distance)   |
| `max_offset_128k`    | Maximum back-reference distance; GLO `enc_off=0`        |
| `text_64k_level1`    | The GHI block type                                      |
| `glo_rle_4k`         | RLE literals, `enc_lit=1`                               |
| `glo_pivco_wide_l7`  | PivCo literals, `enc_lit=2`                             |
| `glo_tok_huffman_l7` | Huffman-coded token section, `enc_tok=2`                |
| `random_4k_checksum` | Checksums on their own, clear of the dictionary path    |
| `multiblock_mixed`   | 16 data blocks; the 4 KB minimum chunk size             |
| `text_64k_bs2m`      | The 2 MB maximum chunk size                             |
| `seekable_4blocks`   | Seek table with several entries                         |
| `dict_http`          | Dictionary **without** a shared Huffman table           |
| `dict_no_checksum`   | Same input and dictionary, checksums off                |
| `dict_seekable_l7`   | Dictionary **with** one (`enc_lit=3`), plus seek + checksum |

All four literal encodings (`enc_lit` 0 to 3) and both token encodings
(`enc_tok` 0 and 2) are exercised.

28 invalid vectors, covering every row of the error table in `FORMAT.md` §11.1,
each a well-formed archive with exactly one field corrupted (except the six
malformed-preamble cases, which never reach version-dependent parsing).

`valid_cases.h` is the source of truth for what each vector is and why; keep it
in step when adding one.

## Byte stability

`v<N>/vectors.sha256` freezes a corpus: the archives, the `.expected` outputs
and the `.zxd` dictionaries — an `.expected` moved silently would move the
reference every external decoder is measured against. The
[`Test Vector Stability`](../.github/workflows/vector-stability.yml)
CI job fails on any changed byte, and checks the file set matches the manifest.

Refresh it whenever the corpus changes on purpose:

```sh
sha256sum conformance/v8/valid/*.zxc conformance/v8/valid/*.expected \
          conformance/v8/valid/*.zxd conformance/v8/invalid/*.zxc \
  | sort -k2 > conformance/v8/vectors.sha256
```

## Regenerating

`test_conformance` rebuilds the generated vectors from their recipes on each run
and compares with the committed bytes, so a hand-edited vector — or one left
behind by an encoder change — fails the suite rather than drifting unnoticed.
The six malformed-preamble cases are static files with no recipe; only the
manifest guards them.

```sh
cmake --build build --target zxc_valid_gen zxc_invalid_gen
./build/zxc_valid_gen   conformance/v8/valid
./build/zxc_invalid_gen conformance/v8/invalid
```

The `.expected` plaintexts and the `.zxd` dictionaries are inputs, never
rewritten. After a format bump the corpus moves to a new `conformance/v<N>/`:
create it, generate into it, then write its `FORMAT_VERSION` and manifest.
