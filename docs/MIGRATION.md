# Migrating ZXC archives across format versions

ZXC's on-disk format has had four deliberate, **non-backward-compatible** breaks.
A decoder accepts **only its own format version** — it compares the header
version byte for exact equality and rejects anything else with
`ZXC_ERROR_BAD_VERSION`, rather than risk misreading it.

| Format | Introduced by | A decoder reads | Headline change |
| :--- | :--- | :--- | :--- |
| **v9** | ZXC **0.15.x** | v9 only | Multiplicative header and block checksums; block checksums cover the decoded bytes and the footer carries an optional archive digest; seek table rebuilt as self-validating groups, announced by `HAS_SEEK_TABLE`; dictionaries move to `.zxd` version 2 |
| **v8** | ZXC 0.14.x | v8 only | Block sub-header 16 → 12 bytes; section descriptors cut to the sizes the header cannot imply |
| **v7** | ZXC 0.13.x | v7 only | Huffman bits use the **PivCo** wire layout; new **level 7 (ULTRA)** adds Huffman-coded tokens |
| **v6** | ZXC 0.12.x | v6 only | **NUM** block removed; **GHI** renumbered type 3 → 2 |
| **v5** | earlier | v5 only | — |

Because each decoder rejects other versions outright, migrating an archive means
a one-time **transcode**: decompress it with a build that understands the *old*
format, then recompress the bytes with the *new* build. The same recipe covers
every jump.

Dictionaries carry their own version byte, and it moved to **2** with v9. A v9
build cannot read a version-1 `.zxd` at all — it is rejected on the version byte,
before the header checksum is compared — and no shipped tool upgrades one, so a
dictionary is migrated alongside the archives that use it: see
[Migrating a dictionary](#migrating-a-dictionary) below.

---

## v8 → v9 (ZXC 0.15.x)

Format **v9** is a deliberate, non-backward-compatible break with **v8**. The
compressed data itself is unchanged; the break is in the integrity fields and in
the seek table:

- **Header and block checksums are multiplicative** (§ 7.1). The previous
  construction mixed with an XOR mask, which left part of the compressed size
  unprotected; the same 2 bytes now cover it.
- **Block checksums are taken over the decoded bytes**, not the compressed ones,
  and each is **seeded with the block's frame position**, so a block moved or
  duplicated within an archive no longer verifies.
- **The whole-archive hash is gone**; the footer instead carries an optional
  8-byte **archive digest** folded from the block checksums (§ 7.3).
- **The seek table is rebuilt as self-validating groups** of one 64-bit anchor
  and 64 block sizes, and its presence is announced by the `HAS_SEEK_TABLE`
  header flag (bit 5) instead of being probed for.
- **Dictionaries move to `.zxd` version 2**, re-signed with the new header
  checksum.

> A v8 build is ZXC **0.14.x**. Keep it until every archive you care about is
> transcoded — it is the only thing that can read v8 data.

---

## v7 → v8 (ZXC 0.14.x)

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

> A v8 build is ZXC **0.14.x**; a v7 build is any **0.13.x**. Keep your
> 0.13.x binary until every archive you care about is transcoded.

---

## v6 → v7 (ZXC 0.13.x)

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

> A v7 build is ZXC **0.13.x**; a v6 build is any **0.12.x**. Keep your
> old 0.12.x binary until every archive you care about is transcoded — it is the
> only thing that can read v6 data.

## Do I need to migrate?

Only if **both** are true:

1. You have archives produced by an older build of ZXC (**v8** or earlier), **and**
2. You want to read them with **v9-only** tools (ZXC 0.15.x+), or standardize your
   stored data on v9.

If you keep the old build around, it can still read its own archives — migration
is not urgent. There is no rush to convert data at rest.

## Check an archive's format version

The version is a single byte at offset `0x04` of the file header:

```sh
xxd -s 4 -l 1 archive.zxc      # -> "05" = v5 ... "08" = v8, "09" = v9
```

(`zxc -V` reports the *tool* version, not the archive's.)

## Migrate: transcode with the old build, recompress with v9

Migration is a one-time **transcode**: decompress with a build that reads the old
format and recompress with a **v9** build (ZXC 0.15.x+). This rebuilds the seek
table and checksums as needed and, for a v5 source, handles legacy NUM blocks by
decoding them and re-encoding as ordinary LZ/RAW.

Assuming `zxc-old` is your existing binary and `zxc-new` is ZXC 0.15.x+:

```sh
zxc-old -dc old.zxc | zxc-new -z -c > new.zxc
```

- `zxc-old -dc old.zxc` — decompress the old archive to stdout.
- `zxc-new -z -c` — compress stdin and write the v9 archive to stdout.

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
build on the decompress side: `zxc-old -dc -D dict.zxd old.zxc | ...`. The new
build needs a version-2 dictionary of its own — see
[Migrating a dictionary](#migrating-a-dictionary).

### Two-step variant (no pipe)

```sh
zxc-old -dc old.zxc > tmp.raw       # decompress to a plain file
zxc-new -z -c tmp.raw > new.zxc     # recompress as v9
rm tmp.raw
```

### Bulk migration

```sh
for f in *.zxc; do
    zxc-old -dc "$f" | zxc-new -z -c > "migrated/$f" || echo "FAILED: $f"
done
```

## Migrating a dictionary

A `.zxd` written by 0.14.x declares version 1, and a v9 build rejects it on that
byte before the header checksum is ever compared — `zxc -D dict.zxd` reports
`ZXC_ERROR_BAD_VERSION`. Nothing in the toolchain upgrades a dictionary in place,
so pick one of two routes.

**Retrain** — the simple one. The transcode reads the old archives with the old
build, which still has its version-1 dictionary; only the recompression side
needs a new one:

```sh
zxc-new --train samples/* -o dict-v9.zxd
zxc-old -dc -D dict-v8.zxd old.zxc | zxc-new -z -D dict-v9.zxd -c > new.zxc
```

The retrained dictionary gets its own `dict_id`, and that is the id the new
archives record.

**Convert byte for byte** if you need the same content, the same shared Huffman
table and therefore the same `dict_id` — to keep an external inventory keyed on
it, say. Two edits to the 16-byte header (§ 12.4 of `FORMAT.md`):

1. set the version byte at `0x04` to `02`;
2. recompute the 2-byte header checksum of § 7.1 over the 16-byte header with
   bytes `0x0C..0x0F` zeroed, and store it little-endian at `0x0E`.

Nothing else moves: `dict_id` covers the content and the table only, so it comes
out unchanged and the pairing survives.

## Verify the result

```sh
zxc-new -t new.zxc                             # integrity check (v9)
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
