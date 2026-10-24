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
| **v6** | ZXC 0.12.x | v6 only | **NUM** block removed; **PAK** renumbered type 3 → 2 |
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
- **The footer stores its sizes on the bytes they need** (§ 8): the digest,
  then the source size, the compressed frame size and a last byte giving their lengths.
  A tool that read the v8 size 12 bytes from the end must parse it back from
  that last byte instead, or call `zxc_get_frame_info()`. The public macro
  `ZXC_FILE_FOOTER_SIZE` is gone with the fixed footer: use
  `ZXC_FILE_FOOTER_MIN_SIZE` / `ZXC_FILE_FOOTER_MAX_SIZE` (digest excluded) for
  bounds.
- **The seek table is rebuilt as self-validating groups** of one 64-bit anchor
  and 64 block sizes, and its presence is announced by the `HAS_SEEK_TABLE`
  header flag (bit 5) instead of being probed for.
- **Dictionaries move to `.zxd` version 2**, re-signed with the new header
  checksum.

> A v8 build is ZXC **0.14.x**. Keep it until every archive you care about is
> transcoded — it is the only thing that can read v8 data.

### C API: option structs become context parameters (0.15.x)

This is a **source and ABI break for C callers**, independent of the format
change: the SOVERSION moves from 5 to **6**. The two option structs
`zxc_compress_opts_t` / `zxc_decompress_opts_t`, their size helpers
`zxc_compress_opts_size()` / `zxc_decompress_opts_size()` and the trailing
`opts` argument of every entry point are gone. Settings now live on a context
and are set one at a time by identifier (`zxc_cparam_t`, `zxc_dparam_t` in
`zxc_opts.h`); the one-shot forms take a level and nothing else. Two new
symbols report the bounds, `zxc_cparam_bounds()` / `zxc_dparam_bounds()`, and
one error code is new, `ZXC_ERROR_BAD_PARAM` (-20). The full contract is in
[API.md §6.1 and §9](API.md#61-parameter-identifiers).

The same SOVERSION break renames the dictionary functions under one `zxc_dict_`
prefix, makes the bound functions return `size_t` and adds
`zxc_version_number()`:

| 0.14.x | 0.15.x |
| :--- | :--- |
| `zxc_train_dict(...)` | `zxc_dict_train_content(...)` (same arguments) |
| `zxc_train_dict_huf(...)` | `zxc_dict_train_huf(...)` (same arguments) |
| `zxc_dict_id(dict, size, huf)` | `zxc_dict_compute_id(dict, size, huf)` |
| `zxc_dict_get_id(buf, size)` | `zxc_dict_read_id(buf, size)` |
| `uint64_t zxc_compress_bound()`, `zxc_compress_block_bound()`, `zxc_decompress_block_bound()`, `zxc_estimate_cctx_size()` | the same functions return `size_t`; 0 still means "too large for this platform" |
| — | `zxc_version_number()`: `MAJOR*10000 + MINOR*100 + PATCH` of the loaded library, with the `ZXC_VERSION_NUMBER` macro for the headers |

Option structs and their call sites:

| 0.14.x | 0.15.x |
| :--- | :--- |
| `zxc_compress(src, n, dst, cap, NULL)` | `zxc_compress(src, n, dst, cap, 0)` (0 = default level) |
| `zxc_compress(src, n, dst, cap, &(zxc_compress_opts_t){.level = 5})` | `zxc_compress(src, n, dst, cap, 5)` |
| `zxc_compress(..., &(zxc_compress_opts_t){.level = 5, .block_size = 65536, .checksum_enabled = 1})` | `zxc_cctx* c = zxc_create_cctx();`<br>`zxc_cctx_set_param(c, ZXC_C_LEVEL, 5);`<br>`zxc_cctx_set_param(c, ZXC_C_BLOCK_SIZE, 65536);`<br>`zxc_cctx_set_param(c, ZXC_C_CHECKSUM, 1);`<br>`zxc_compress_cctx(c, src, n, dst, cap);`<br>`zxc_free_cctx(c);` |
| `zxc_decompress(src, n, dst, cap, NULL)` | `zxc_decompress(src, n, dst, cap)` |
| `zxc_decompress(src, n, dst, cap, &(zxc_decompress_opts_t){.checksum_enabled = 1})` | `zxc_dctx* d = zxc_create_dctx();`<br>`zxc_dctx_set_param(d, ZXC_D_CHECKSUM, 1);`<br>`zxc_decompress_dctx(d, src, n, dst, cap);`<br>`zxc_free_dctx(d);` |
| `zxc_decompress_inplace(buf, cap, comp_size, opts)` | `zxc_decompress_inplace(buf, cap, comp_size)`, or `zxc_decompress_inplace_dctx(d, buf, cap, comp_size)` with a configured `d` |
| `zxc_create_cctx(&opts)` | `zxc_create_cctx()` then `zxc_cctx_set_param()` per setting |
| `zxc_compress_cctx(c, src, n, dst, cap, &opts)` | `zxc_compress_cctx(c, src, n, dst, cap)`; change a setting between calls with `zxc_cctx_set_param()` |
| `zxc_compress_block(c, ..., &opts)` / `zxc_decompress_block(d, ..., &opts)` / `zxc_decompress_block_safe(d, ..., &opts)` | same calls without the last argument |
| `zxc_decompress_dctx(d, src, n, dst, cap, &opts)` | `zxc_decompress_dctx(d, src, n, dst, cap)` |
| `.dict`, `.dict_size`, `.dict_huf` | `zxc_cctx_set_dict(c, dict, size, huf)` / `zxc_dctx_set_dict(d, dict, size, huf)`; NULL `dict` detaches |
| `.progress_cb`, `.user_data` | `zxc_cctx_set_progress(c, cb, ud)` / `zxc_dctx_set_progress(d, cb, ud)` |
| `.n_threads` | `ZXC_C_THREADS` / `ZXC_D_THREADS` |
| `.seekable = 1` | `zxc_cctx_set_param(c, ZXC_C_SEEKABLE, 1)` |
| `zxc_init_static_cctx(ws, sz, &(zxc_compress_opts_t){.level = L, .block_size = B, .checksum_enabled = 1})` | `zxc_cctx* c = zxc_init_static_cctx(ws, sz, B, L);`<br>`zxc_cctx_set_param(c, ZXC_C_CHECKSUM, 1);` |
| `zxc_stream_compress(fin, fout, &opts)` / `zxc_stream_decompress(fin, fout, &opts)` | `zxc_stream_compress(fin, fout, c)` / `zxc_stream_decompress(fin, fout, d)` with a configured context (`NULL` still means defaults) |
| `zxc_cstream_create(&opts)` / `zxc_dstream_create(&opts)` | `zxc_cstream_create(c)` / `zxc_dstream_create(d)`; the parameters are copied, the context can be freed right after |
| `zxc_compress_opts_size()` / `zxc_decompress_opts_size()` | gone; bindings use `zxc_cparam_bounds()` / `zxc_dparam_bounds()` to discover what the loaded library knows |

Behaviour changes to know about, beyond the mechanical rewrite:

- **The simple `zxc_decompress()` no longer verifies checksums**, and neither
  does `zxc_decompress_inplace()`: with no options argument there is nothing to
  turn verification on with. An archive that carries checksums decodes
  unverified. To keep the check, decode through a `zxc_dctx` with
  `zxc_dctx_set_param(d, ZXC_D_CHECKSUM, 1)` (`zxc_decompress_dctx()`,
  `zxc_decompress_inplace_dctx()`). The same holds for the push stream
  (`zxc_dstream_create(NULL)` verifies nothing) and the `FILE*` pipeline
  (`zxc_stream_decompress(fin, fout, NULL)`). Dictionary archives likewise need
  a `zxc_dctx` with `zxc_dctx_set_dict()`: the simple form returns
  `ZXC_ERROR_DICT_REQUIRED`.
- **A setter refuses instead of clamping.** The structs let any `level` through
  and clamped it to 1–7; `zxc_cctx_set_param(c, ZXC_C_LEVEL, 9)` returns
  `ZXC_ERROR_BAD_LEVEL` and leaves the level unchanged. An invalid
  `ZXC_C_BLOCK_SIZE` is `ZXC_ERROR_BAD_BLOCK_SIZE`; an out-of-bounds thread
  count, checksum or seekable flag, or an unknown identifier, is
  `ZXC_ERROR_BAD_PARAM`. The one-shot `zxc_compress(..., level)` is the only
  call that still clamps. Check the return value of each setter.
- **`0` means default, not "keep".** The structs' zero field fell back to the
  creation value; `zxc_cctx_set_param(c, ZXC_C_LEVEL, 0)` restores the library
  default (3), whatever the context held before. `zxc_cctx_reset()` /
  `zxc_dctx_reset()` restore everything and detach the dictionary and callback.
- **`zxc_decompress_dctx()` reports the same error codes as `zxc_decompress()`.**
  A truncated input is `ZXC_ERROR_SRC_TOO_SMALL` and a malformed header
  reports what the header parse found (`ZXC_ERROR_BAD_MAGIC`,
  `ZXC_ERROR_BAD_VERSION`, `ZXC_ERROR_BAD_BLOCK_SIZE`), where 0.14 flattened
  both to `ZXC_ERROR_NULL_INPUT` or `ZXC_ERROR_BAD_HEADER`.
- **Settings persist by construction.** There is no "sticky" rule to remember:
  a parameter stays in force until set again. Code that passed a fresh struct
  to each call to override one field now calls `zxc_cctx_set_param()` once and
  compresses as many times as it likes.
- **Static-context errors move to the setters.** On a context from
  `zxc_init_static_cctx()`, a different block size
  (`ZXC_ERROR_BAD_BLOCK_SIZE`), a level raise into 6–7 on a workspace carved
  below it (`ZXC_ERROR_BAD_LEVEL`) and a dictionary
  (`ZXC_ERROR_DICT_UNSUPPORTED`, also on a static `zxc_dctx`) are refused by
  `zxc_cctx_set_param()` / `zxc_*ctx_set_dict()`, no longer by the compress or
  decompress call. `zxc_init_static_cctx()` takes its block size and level as
  explicit arguments; `0` is invalid there, not a default.
- **Dictionary pointers are kept, not copied.** As with the struct fields, the
  context stores the `dict` / `dict_huf` pointers; the bytes must outlive every
  call that uses them (`zxc_seekable_set_dict()` still copies).
- **The `FILE*` and push creators take a context.** `zxc_stream_compress()` /
  `zxc_stream_decompress()` read a `const zxc_cctx*` / `const zxc_dctx*` for
  their parameters and never touch its buffers, so one context can configure a
  one-shot call and a pipeline alike; `zxc_cstream_create()` /
  `zxc_dstream_create()` copy the parameters at creation. A dictionary on the
  context passed to a push creator fails creation (returns `NULL`), as the
  dictionary fields did.
- **SOVERSION 6.** Binaries built against `libzxc.so.5` do not load against
  this release; rebuild them against the new headers.

### Windows archives (0.15.x)

The Windows archives are now built with MinGW-w64 (UCRT) and ship the shared
library, `bin/libzxc.dll`, with its MinGW import library and its export list
`lib/libzxc.def`. The `lib/zxc.lib` of 0.14.x, an MSVC static library, is gone,
and the archive's CMake package and `libzxc.pc` now target MinGW: MSVC projects
build an import library from the `.def`
(`lib /def:lib\libzxc.def /out:zxc.lib /machine:x64`, or `arm64`) and link the
DLL, or build zxc from source for a static library. A CMake MSVC build names the
DLL `libzxc.dll` (0.14.x: `zxc.dll`) and keeps the import library `zxc.lib`.

### CLI output (0.15.x)

`zxc -r` no longer follows symbolic links, junctions or mount points: each is
reported and skipped, and the exit status is non-zero when a directory or an
entry cannot be read.

Concatenated archives (`cat a.zxc b.zxc`) now decode as one stream, so the
listing describes every frame. Scripts that parse it need these changes:

- `zxc -l -j` and `zxc -t -j`: the `checksum_method` key (`RapidHash`/`none`) is
  now `checksum`, valued `enabled`, `disabled` or `mixed` (frames that differ);
  `-l -j` adds `seekable` (same values) and `frames`.
- `zxc -l -j`: `block_size_kb` and `dict_id` stay a number and a string (or
  `null`) when every frame agrees, and become arrays when they differ.
- `zxc -l`: the table gains `Seekable` and `Frames` columns, shows `mixed` where
  frames differ, and names the file only when several are listed.
- `zxc -l -v`: `Checksum Method:` is now `Checksum:`, and the block sizes and
  dictionary IDs of every frame are listed.

---

## v7 → v8 (ZXC 0.14.x)

Format **v8** is a deliberate, non-backward-compatible break with **v7**. Nothing
about the compression changed — the break is entirely in how a block describes
itself:

- The SPL/PAK **sub-header shrank from 16 to 12 bytes**: the four reserved bytes
  are gone.
- **Section descriptors** no longer form a fixed table. Only the two sizes the
  header cannot imply are written — the literal section's compressed size when it
  is RLE- or entropy-coded, and the token section's at level 7 — so a block
  carries 0, 4 or 8 descriptor bytes instead of 32 (SPL) or 24 (PAK). Everything
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

Block **types** are unchanged from v6 (PAK stays type 2, and so on) — the break is
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
   bytes `0x0E..0x0F` zeroed, and store it little-endian at `0x0E`.

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
- **PAK** was renumbered from type 3 to **type 2**.
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
