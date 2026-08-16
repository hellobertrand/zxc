# Migrating ZXC archives across format versions

ZXC's on-disk format has had three deliberate, **non-backward-compatible** breaks.
A decoder accepts **only its own format version** — it compares the header
version byte for exact equality and rejects anything else with
`ZXC_ERROR_BAD_VERSION`, rather than risk misreading it.

| Format | Introduced by | A decoder reads | Headline change |
| :--- | :--- | :--- | :--- |
| **v8** | ZXC **0.14.0** | v8 only | Block sub-header 16 → 12 bytes; section descriptors cut to the sizes the header cannot imply |
| **v7** | ZXC 0.13.0 | v7 only | Huffman bits use the **PivCo** wire layout; new **level 7 (ULTRA)** adds Huffman-coded tokens |
| **v6** | ZXC 0.12.x | v6 only | **NUM** block removed; **GHI** renumbered type 3 → 2 |
| **v5** | earlier | v5 only | — |

Because each decoder rejects other versions outright, migrating an archive means
a one-time **transcode**: decompress it with a build that understands the *old*
format, then recompress the bytes with the *new* build. The same recipe covers
every jump.

---

## v7 → v8 (ZXC 0.14.0)

Format **v8** is a deliberate, non-backward-compatible break with **v7**. Nothing
about the compression changed — the break is entirely in how a block describes
itself:

- The GLO/GHI **sub-header shrank from 16 to 12 bytes**: the four reserved bytes
  are gone.
- **Section descriptors** no longer form a fixed table. Only the two sizes the
  header cannot imply are written — the literal section's compressed size when it
  is RLE- or entropy-coded, and the token section's at level 7 — so a block
  carries 0, 4 or 8 descriptor bytes instead of 32 (GLO) or 24 (GHI). Everything
  else is derived, and the extras section is the payload residue.
- The field formerly called `enc_litlen` is **`enc_tok`**: it always described the
  token section, never literal lengths.
- A block must leave at least **32 readable bytes behind its literal section**;
  the encoder pads into the extras when a small block falls short.

Together these save 24 to 36 bytes per block, which is noise at 512 KB blocks and
worth about 2 % at 4 KB — the regime dictionaries target.

> A v8 build is ZXC **0.14.0** or newer; a v7 build is any **0.13.x**. Keep your
> 0.13.x binary until every archive you care about is transcoded.

---

## v6 → v7 (ZXC 0.13.0)

Format **v7** is a deliberate, non-backward-compatible break with **v6**:

- Huffman-coded literal sections (`enc_lit = 2`) now place their bits on the wire
  in the **PivCo layout** — the *code* is the same length-limited canonical
  Huffman code, but the bits are grouped by tree level so the decoder can merge
  them data-parallel. v6 and v7 Huffman blocks are therefore not interchangeable.
- A new **level 7 (ULTRA)** tier additionally Huffman-codes the **sequence-token**
  stream (`enc_tok = 2` in v8 terms, code lengths up to 11 bits) on top of the
  literals.
- The file-header version byte changed from `6` to `7`.

Block **types** are unchanged from v6 (GHI stays type 2, and so on) — the break is
the Huffman wire layout plus the new token encoding. A **v7 decoder rejects v6
archives** with `ZXC_ERROR_BAD_VERSION` rather than misinterpret them, and a v6
decoder likewise cannot read v7 archives.

> A v7 build is ZXC **0.13.0** or newer; a v6 build is any **0.12.x**. Keep your
> old 0.12.x binary until every archive you care about is transcoded — it is the
> only thing that can read v6 data.

## Do I need to migrate?

Only if **both** are true:

1. You have archives produced by an older build of ZXC (**v7** or earlier), **and**
2. You want to read them with **v8-only** tools (ZXC 0.14.0+), or standardize your
   stored data on v8.

If you keep the old build around, it can still read its own archives — migration
is not urgent. There is no rush to convert data at rest.

## Check an archive's format version

The version is a single byte at offset `0x04` of the file header:

```sh
xxd -s 4 -l 1 archive.zxc      # -> "05" = v5, "06" = v6, "07" = v7, "08" = v8
```

(`zxc -V` reports the *tool* version, not the archive's.)

## Migrate: transcode with the old build, recompress with v8

Migration is a one-time **transcode**: decompress with a build that reads the old
format and recompress with a **v8** build (ZXC 0.14.0+). This rebuilds the seek
table and checksums as needed and, for a v5 source, handles legacy NUM blocks by
decoding them and re-encoding as ordinary LZ/RAW.

Assuming `zxc-old` is your existing binary and `zxc-new` is ZXC 0.14.0+:

```sh
zxc-old -dc old.zxc | zxc-new -z -c > new.zxc
```

- `zxc-old -dc old.zxc` — decompress the old archive to stdout.
- `zxc-new -z -c` — compress stdin and write the v8 archive to stdout.

The recompression uses the new encoder's options, so pick them explicitly to match
your needs (the original encoding level is **not** recorded in the archive):

```sh
# Examples
zxc-old -dc old.zxc | zxc-new -z -6 -c > new.zxc     # densest fast-decode tier
zxc-old -dc old.zxc | zxc-new -z -7 -c > new.zxc     # ULTRA — densest level
zxc-old -dc old.zxc | zxc-new -z -N -c > new.zxc     # no checksums
zxc-old -dc old.zxc | zxc-new -z -B 1M -c > new.zxc  # 1 MB blocks
zxc-old -dc old.zxc | zxc-new -z -S -c > new.zxc     # keep it seekable
```

If the old archive was compressed **with a dictionary**, supply it to the old
build on the decompress side: `zxc-old -dc -D dict.zxd old.zxc | ...`.

### Two-step variant (no pipe)

```sh
zxc-old -dc old.zxc > tmp.raw       # decompress to a plain file
zxc-new -z -c tmp.raw > new.zxc     # recompress as v8
rm tmp.raw
```

### Bulk migration

```sh
for f in *.zxc; do
    zxc-old -dc "$f" | zxc-new -z -c > "migrated/$f" || echo "FAILED: $f"
done
```

## Verify the result

```sh
zxc-new -t new.zxc                             # integrity check (v8)
# strong check: decompressed output is byte-identical to the original
diff <(zxc-old -dc old.zxc) <(zxc-new -dc new.zxc) && echo OK
```

---

## v5 → v6 (historical)

Format **v6** was the previous break, with **v5**:

- The **NUM** block (v5 type 2 — a numeric delta/ZigZag/bit-packed codec) was
  removed.
- **GHI** was renumbered from type 3 to **type 2**.
- The file-header version byte changed from `5` to `6`.

Because v5 and v6 number their block types differently, a v6 decoder rejects v5
archives outright with `ZXC_ERROR_BAD_VERSION`. The transcode recipe above applies
directly — use a v5 build on the decompress side. To land straight on v8, pipe a
v5 build into a v8 build (`zxc-v5 -dc old.zxc | zxc-new -z -c`); there is no need
to stop at the versions in between.

## Notes

- **Keep the old build available** until all archives you care about are migrated —
  it is the only thing that can read the old format (and it is what the pipe above
  uses).
- Migration **re-encodes** the data, so the new archive's bytes and exact size may
  differ from the old one even at the same level; the *decompressed output* is
  identical.
- There is no in-place, byte-preserving converter between format versions: the wire
  layout differs at every step (v5 NUM blocks, the v6 → v7 Huffman/PivCo change,
  the v7 → v8 header and descriptor rework), so the data must be decoded and
  re-encoded, which the transcode above does.
