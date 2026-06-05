# Migrating ZXC archives: format v5 -> v6

Format **v6** is a deliberate, non-backward-compatible break with **v5**:

- The **NUM** block (v5 type 2 — a numeric delta/ZigZag/bit-packed codec) was
  removed.
- **GHI** was renumbered from type 3 to **type 2**.
- The file-header version byte changed from `5` to `6`.

Because v5 and v6 number their block types differently, a **v6 decoder rejects
v5 archives** outright with `ZXC_ERROR_BAD_VERSION` rather than risk
misinterpreting them. v5 archives are therefore **not** readable by v6 tools and
must be converted.

## Do I need to migrate?

Only if **both** are true:

1. You have archives produced by a **v5** build of ZXC, **and**
2. You want to read them with **v6-only** tools (or standardize your stored data
   on v6).

If you keep a v5 build around, it can still read its own archives — migration is
not urgent. There is no rush to convert data at rest.

## Check an archive's format version

The version is a single byte at offset `0x04` of the file header
(`0x05` = v5, `0x06` = v6):

```sh
xxd -s 4 -l 1 archive.zxc      # -> "05" = v5, "06" = v6
```

(`zxc -V` reports the *tool* version, not the archive's.)

## Migrate: decompress with v5, recompress with v6

Migration is a one-time **transcode**: decompress the archive with a **v5** build
and recompress the bytes with a **v6** build. This handles everything correctly —
including v5 NUM blocks, which are decoded by the v5 tool and re-encoded by v6 as
ordinary LZ/RAW blocks — and rebuilds the seek table and checksums as needed.

Assuming `zxc-v5` is your existing (v5) binary and `zxc-v6` is the new one:

```sh
zxc-v5 -dc old.zxc | zxc-v6 -z -c > new.zxc
```

- `zxc-v5 -dc old.zxc` — decompress the v5 archive to stdout.
- `zxc-v6 -z -c` — compress stdin and write the v6 archive to stdout.

The recompression uses the v6 encoder's options, so pick them explicitly to match
your needs (the original v5 encoding level is **not** recorded in the archive):

```sh
# Examples
zxc-v5 -dc old.zxc | zxc-v6 -z -6 -c > new.zxc       # densest output
zxc-v5 -dc old.zxc | zxc-v6 -z -N -c > new.zxc       # no checksums
zxc-v5 -dc old.zxc | zxc-v6 -z -B 1M -c > new.zxc    # 1 MB blocks
zxc-v5 -dc old.zxc | zxc-v6 -z -S -c > new.zxc       # keep it seekable
```

If the v5 archive was compressed **with a dictionary**, supply it on the
decompress side: `zxc-v5 -dc -D dict.zxd old.zxc | ...`.

### Two-step variant (no pipe)

```sh
zxc-v5 -dc old.zxc > tmp.raw        # decompress to a plain file
zxc-v6 -z -c tmp.raw > new.zxc       # recompress as v6
rm tmp.raw
```

### Bulk migration

```sh
for f in *.zxc; do
    zxc-v5 -dc "$f" | zxc-v6 -z -c > "migrated/$f" || echo "FAILED: $f"
done
```

## Verify the result

```sh
zxc-v6 -t new.zxc                              # integrity check (v6)
# strong check: decompressed output is byte-identical to the v5 original
diff <(zxc-v5 -dc old.zxc) <(zxc-v6 -dc new.zxc) && echo OK
```

## Notes

- **Keep a v5 build available** until all archives you care about are migrated —
  it is the only thing that can read v5 data (and it is what the pipe above uses).
- Migration **re-encodes** the data, so the v6 archive's bytes and exact size may
  differ from the v5 archive even at the same level; the *decompressed output* is
  identical.
- There is no in-place, byte-preserving converter: v5 NUM blocks have no v6
  equivalent and must be decoded and re-encoded, which the transcode above does.
