# ZXC Decoder Conformance Suite

Reference test vectors for validating any ZXC decoder implementation.

## Contents

```
valid/
  *.zxc         Compressed files (frozen wire format)
  *.expected    Expected decompressed output (plaintext reference)
invalid/
  *.zxc         Malformed files that must be rejected
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
| Text                 | 3     | Compressible text with and without checksum       |
| Random               | 3     | Incompressible data (stored as raw blocks)        |
| Match patterns       | 3     | Long matches, short matches, max offset distance  |
| Compression levels   | 6     | Same input compressed at levels 1 through 6       |
| Block size variants  | 2     | 4 KB and 2 MB block sizes                        |
| Checksum             | 3     | Per-block and global checksum enabled             |
| Multi-block          | 2     | 16 blocks per file (4 KB block size)             |
| Seekable             | 3     | Seekable archives with seek table                |
| Invalid              | 12    | Bad magic, bad version, bad CRC, truncated, corrupt payload, garbage |

## License

BSD-3-Clause. Same as the ZXC library.
