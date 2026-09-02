# ZXC Decoder Conformance Suite

Reference test vectors for validating any ZXC decoder implementation.

## Contents

```
valid/
  *.zxc         Compressed files (frozen wire format)
  *.expected    Expected decompressed output (plaintext reference)
  *.zxd         Dictionaries used by the dictionary vectors
invalid/
  *.zxc         Malformed files that must be rejected
vectors.sha256  Byte-stability manifest for the whole corpus (sha256sum format)
valid_cases.h   Recipe for every valid vector (options it was produced with)
gen_valid.c     Maintainer tool: rebuilds valid/*.zxc from that recipe
gen_invalid.c   Maintainer tool: rebuilds invalid/*.zxc at the current version
```

## Validating a decoder

For each `valid/*.zxc`:

1. Decompress the file with your decoder
2. Compare the output byte-for-byte against the matching `.expected` file
3. Any mismatch is a decoder bug

For each `invalid/*.zxc`:

1. Attempt to decompress the file with your decoder
2. The decoder **must** reject it (return an error, not produce output)
3. Accepting a malformed file is a decoder bug

## Quick check (shell)

```sh
pass=0; fail=0
for f in valid/*.zxc; do
    expected="${f%.zxc}.expected"
    your-decoder "$f" /tmp/out
    if cmp -s /tmp/out "$expected"; then
        pass=$((pass + 1))
    else
        echo "FAIL: $f"; fail=$((fail + 1))
    fi
done
for f in invalid/*.zxc; do
    if your-decoder "$f" /dev/null 2>/dev/null; then
        echo "FAIL (should reject): $f"; fail=$((fail + 1))
    else
        pass=$((pass + 1))
    fi
done
echo "Passed: $pass  Failed: $fail"
```

## Vector coverage

| Vector               | What it alone covers                                    |
|----------------------|---------------------------------------------------------|
| `empty`              | No data block: EOF only, `src_size = 0`                 |
| `all_256_values`     | RAW block; all 256 byte values                          |
| `all_zeros_4k`       | Offset-1 overlap run (long match at minimum distance)   |
| `max_offset_128k`    | Maximum back-reference distance; GLO `enc_off=0`        |
| `text_64k_level1`    | The GHI block type                                      |
| `glo_rle_4k`         | RLE literals, `enc_lit=1`                               |
| `glo_pivco_wide_l7`  | PivCo literals, `enc_lit=2`                             |
| `random_4k_checksum` | Checksums on their own, clear of the dictionary path    |
| `multiblock_mixed`   | 16 data blocks; the 4 KB minimum chunk size             |
| `text_64k_bs2m`      | The 2 MB maximum chunk size                             |
| `seekable_4blocks`   | Seek table with several entries                         |
| `dict_http`          | Dictionary **without** a shared Huffman table           |
| `dict_no_checksum`   | Same input and dictionary, checksums off                |
| `dict_seekable_l7`   | Dictionary **with** one (`enc_lit=3`), plus seek + checksum |

14 valid vectors, one per decoder-visible trait, plus 20 invalid ones. A decoder
never sees the compression level, only the block type and the encodings it
selects, so levels 3 to 6 all emit the same `GLO enc_lit=0 enc_off=1` block and
one vector stands for them. All four literal encodings the format defines
(`enc_lit` 0 to 3) are exercised.

Counting header traits alone would suggest fewer vectors suffice. It would be
wrong: `all_zeros_4k` and `max_offset_128k` earn their place on payload shape,
which no header field shows, and `dict_http` on a combination (dictionary
without the shared Huffman table) rather than on any trait taken alone.

`dict_no_checksum` differs from `dict_http` in exactly one flag, so a decoder
that passes one and fails the other has already named its own bug.

| Invalid | 20 | Bad magic, bad version, bad CRC, bad block size/type, bad checksum algorithm, truncated, corrupt payload, garbage, forged `enc_lit`/`enc_off`, forged GHI offset, insufficient literal slack, missing dictionary |
|---------|----|--------------------------------------------------------|

The categories above mirror the grouping in `valid_cases.h`, which is the source
of truth: keep the two in step when adding a vector.

## Byte stability

`vectors.sha256` freezes the whole corpus — the archives, the `.expected`
plaintexts they decode to, and the `.zxd` dictionaries. The
[`Conformance Vector Stability`](../.github/workflows/conformance-vectors.yml)
CI job runs `sha256sum -c` against it, so **any single changed byte fails CI**.
It also checks that the file set and the manifest stay in sync, and that every
`valid/<name>.zxc` still has its `<name>.expected`.

The `.expected` files are covered deliberately: they are the reference every
external decoder is measured against, so moving one silently would be at least
as damaging as changing an archive.

Refresh the manifest whenever the corpus changes on purpose:

```sh
sha256sum conformance/valid/*.zxc conformance/valid/*.expected \
          conformance/valid/*.zxd conformance/invalid/*.zxc \
  | sort -k2 > conformance/vectors.sha256
```

## Regenerating the valid vectors

Their recipe — level, block size, checksum, seekable, dictionary — is declared
in `valid_cases.h`. The input of each vector is its own committed `.expected`
file (and, for dictionary vectors, the committed `.zxd`), so regeneration only
recompresses; it never rewrites the plaintext third-party decoders check
against.

```sh
cmake --build build --target zxc_valid_gen
./build/zxc_valid_gen conformance/valid
```

Every field in the recipe is stated explicitly, including those that match the
library defaults today, so that a change of default shows up as a corpus diff
instead of silently re-cutting the vectors. Run this after a format bump, then
re-run the conformance suite and review the diff.

Every invalid vector is a well-formed archive of the **current** format version
with exactly one field corrupted, except the malformed-preamble cases, which
never reach version checking. Regenerate them after a format bump with:

```sh
cmake --build build --target zxc_invalid_gen
./build/zxc_invalid_gen conformance/invalid
```

Both generators change committed bytes, so refresh `vectors.sha256` in the same
commit.

Without this, a bump makes every vector fail on the version byte instead of on
its own defect — the suite still reports "correctly rejected" while testing
nothing. Check each vector's error code after regenerating, not just that it was
rejected.

## License

BSD-3-Clause. Same as the ZXC library.
