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

| Category             | Count | Description                                      |
|----------------------|-------|--------------------------------------------------|
| Basic                | 5     | Empty, 1 byte, all 256 values, all-zeros         |
| Text                 | 3     | Compressible text, with and without checksum      |
| Random               | 4     | Incompressible data (stored as raw blocks)        |
| Match patterns       | 4     | Long matches, short matches, max offset distance  |
| Compression levels   | 6     | Same input compressed at levels 1 through 6       |
| Level 7 (PivCo)      | 1     | Level-7 PivCo literal section (`enc_lit=2`)       |
| Block size variants  | 2     | 4 KB and 2 MB block sizes                        |
| Checksum             | 1     | Compressible payload with checksums enabled       |
| Multi-block          | 2     | 4 KB blocks over a 64 KB input                   |
| Seekable             | 3     | Seekable archives with seek table                |
| Dictionary           | 2     | Dictionary archives, incl. a seekable level-7 one |
| **Valid total**      | **33**|                                                  |
| Invalid              | 20    | Bad magic, bad version, bad CRC, bad block size/type, bad checksum algorithm, truncated, corrupt payload, garbage, forged `enc_lit`/`enc_off`, forged GHI offset, insufficient literal slack, missing dictionary |

The categories above mirror the grouping in `valid_cases.h`, which is the source
of truth: keep the two in step when adding a vector.

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

Without this, a bump makes every vector fail on the version byte instead of on
its own defect — the suite still reports "correctly rejected" while testing
nothing. Check each vector's error code after regenerating, not just that it was
rejected.

## License

BSD-3-Clause. Same as the ZXC library.
