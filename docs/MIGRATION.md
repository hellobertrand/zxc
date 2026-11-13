# Migrating ZXC archives across format versions

Format v9 is frozen as of ZXC 1.0.0: every later release reads v9 archives, and a decoder
for version N reads every version from 9 to N. Archives older than v9 (v5 to v8) are
rejected with `ZXC_ERROR_BAD_VERSION` and take a one-time
**[transcode](#migrate-transcode-with-the-old-build-recompress-with-v9)**.

| Format | Introduced by | Headline change |
| :--- | :--- | :--- |
| **v9** | ZXC **0.15.x**, kept by 1.0.x | New header and block checksums, optional archive digest, seek table rebuilt, `.zxd` version 2 |
| **v8** | ZXC 0.14.x | Block sub-header 16 → 12 bytes; smaller section descriptors |
| **v7** | ZXC 0.13.x | Huffman bits use the **PivCo** layout; new **level 7 (ULTRA)** |
| **v6** | ZXC 0.12.x | **NUM** block removed; **PAK** renumbered type 3 → 2 |
| **v5** | earlier | — |

Upgrading from 0.15.x takes no transcode, but the APIs and the CLI defaults change:
see [0.15.x → 1.0.0](#015x--100-format-v9-unchanged).

---

## 0.15.x → 1.0.0 (format v9 unchanged)

**No archive migration.** Archives and `.zxd` dictionaries read in both directions
between 0.15.x and 1.0.0. Non-zero reserved bits and bytes are now rejected in file,
block and `.zxd` headers (`ZXC_ERROR_BAD_HEADER` for a dictionary). 0.15.x writes
zeros there, so only a third-party writer can be affected.

### C API: option structs become context parameters

This is a **source and ABI break**. The SOVERSION moves from 5 to **6**, so binaries
built against `libzxc.so.5` must be rebuilt. The option structs and the `opts`
arguments are gone: settings live on a context, set one at a time by identifier
(`zxc_cparam_t`, `zxc_dparam_t` in `zxc_opts.h`). `zxc_cctx_get_param()` /
`zxc_dctx_get_param()` read a setting back, `zxc_cparam_bounds()` /
`zxc_dparam_bounds()` report its bounds, and `ZXC_ERROR_BAD_PARAM` (-20) is new. See
[API.md §6.1 and §9](API.md#61-parameter-identifiers).

| 0.15.x | 1.0.x |
| :--- | :--- |
| `zxc_train_dict(...)` | `zxc_dict_train_content(...)` (same arguments) |
| `zxc_train_dict_huf(...)` | `zxc_dict_train_huf(...)` (same arguments) |
| `zxc_dict_id(dict, size, huf)` | `zxc_dict_compute_id(dict, size, huf)` |
| `zxc_dict_get_id(buf, size)` | `zxc_dict_read_id(buf, size)` |
| `uint64_t zxc_compress_bound()`, `zxc_compress_block_bound()`, `zxc_decompress_block_bound()`, `zxc_estimate_cctx_size()` | the same functions return `size_t`; 0 still means "too large for this platform" |
| — | `zxc_version_number()`: `MAJOR*10000 + MINOR*100 + PATCH` of the loaded library (`ZXC_VERSION_NUMBER` for the headers) |
| — | `zxc_version_codename()`: release code name of the loaded library (`ZXC_VERSION_CODENAME` for the headers) |
| `zxc_compress(src, n, dst, cap, NULL)` | `zxc_compress(src, n, dst, cap, 0)` (0 = default level) |
| `zxc_compress(src, n, dst, cap, &(zxc_compress_opts_t){.level = 5})` | `zxc_compress(src, n, dst, cap, 5)` |
| `zxc_compress(..., &(zxc_compress_opts_t){.level = 5, .block_size = 65536, .checksum_enabled = 1})` | `zxc_cctx* c = zxc_create_cctx();`<br>`zxc_cctx_set_param(c, ZXC_C_LEVEL, 5);`<br>`zxc_cctx_set_param(c, ZXC_C_BLOCK_SIZE, 65536);`<br>`zxc_cctx_set_param(c, ZXC_C_CHECKSUM, 1);`<br>`zxc_compress_cctx(c, src, n, dst, cap);`<br>`zxc_free_cctx(c);` |
| `zxc_decompress(src, n, dst, cap, NULL)` | `zxc_decompress(src, n, dst, cap)` |
| `zxc_decompress(src, n, dst, cap, &(zxc_decompress_opts_t){.checksum_enabled = 1})` | `zxc_dctx* d = zxc_create_dctx();`<br>`zxc_dctx_set_param(d, ZXC_D_CHECKSUM, 1);`<br>`zxc_decompress_dctx(d, src, n, dst, cap);`<br>`zxc_free_dctx(d);` |
| `zxc_decompress_inplace(buf, cap, comp_size, opts)` | `zxc_decompress_inplace(buf, cap, comp_size)`, or `zxc_decompress_inplace_dctx(d, buf, cap, comp_size)` with a configured `d` |
| `zxc_create_cctx(&opts)` | `zxc_create_cctx()` then `zxc_cctx_set_param()` per setting |
| `zxc_compress_cctx(c, src, n, dst, cap, &opts)` | `zxc_compress_cctx(c, src, n, dst, cap)` |
| `zxc_compress_block(c, ..., &opts)` / `zxc_decompress_block(d, ..., &opts)` / `zxc_decompress_block_safe(d, ..., &opts)` | same calls without the last argument |
| `zxc_decompress_dctx(d, src, n, dst, cap, &opts)` | `zxc_decompress_dctx(d, src, n, dst, cap)` |
| `.dict`, `.dict_size`, `.dict_huf` | `zxc_cctx_set_dict(c, dict, size, huf)` / `zxc_dctx_set_dict(d, dict, size, huf)`; NULL `dict` detaches |
| `.progress_cb`, `.user_data` | `zxc_cctx_set_progress(c, cb, ud)` / `zxc_dctx_set_progress(d, cb, ud)` |
| `.n_threads` | `ZXC_C_THREADS` / `ZXC_D_THREADS` |
| `.seekable = 1` | `zxc_cctx_set_param(c, ZXC_C_SEEKABLE, 1)` |
| `zxc_init_static_cctx(ws, sz, &(zxc_compress_opts_t){.level = L, .block_size = B, .checksum_enabled = 1})` | `zxc_cctx* c = zxc_init_static_cctx(ws, sz, B, L);`<br>`zxc_cctx_set_param(c, ZXC_C_CHECKSUM, 1);` |
| `zxc_stream_compress(fin, fout, &opts)` / `zxc_stream_decompress(fin, fout, &opts)` | `zxc_stream_compress(fin, fout, c)` / `zxc_stream_decompress(fin, fout, d)` (`NULL` still means defaults) |
| `zxc_cstream_create(&opts)` / `zxc_dstream_create(&opts)` | `zxc_cstream_create(c)` / `zxc_dstream_create(d)` |
| `zxc_compress_opts_size()` / `zxc_decompress_opts_size()` | gone; use `zxc_cparam_bounds()` / `zxc_dparam_bounds()` |

Behaviour changes beyond the rewrite:

- **The simple forms no longer verify checksums:** `zxc_decompress()`,
  `zxc_decompress_inplace()`, `zxc_dstream_create(NULL)` and
  `zxc_stream_decompress(fin, fout, NULL)`. To keep the check, use a `zxc_dctx` with
  `ZXC_D_CHECKSUM` set. A dictionary archive also needs a `zxc_dctx`, with
  `zxc_dctx_set_dict()`; otherwise the call returns `ZXC_ERROR_DICT_REQUIRED`.
- **Invalid values are refused, not clamped**, so check what each setter returns. A
  bad level gives `ZXC_ERROR_BAD_LEVEL`, and leaves the setting unchanged. That also
  holds for the one-shot `zxc_compress(..., 99)`, which was ULTRA in 0.15. A bad
  `ZXC_C_BLOCK_SIZE` gives `ZXC_ERROR_BAD_BLOCK_SIZE`. A bad thread count, checksum or
  seekable flag, or an unknown identifier, gives `ZXC_ERROR_BAD_PARAM`.
- **`0` means default, not "keep".** In the structs, a zero `level` or `block_size`
  reused the previous call's value; `zxc_cctx_set_param(c, ZXC_C_LEVEL, 0)` restores
  the default (3). `zxc_cctx_reset()` / `zxc_dctx_reset()` restore everything and
  detach the dictionary and callback.
- **Settings persist** until set again. Code that passed a fresh struct per call now
  calls the setter once.
- **Static contexts fail at the setter.** A different block size
  (`ZXC_ERROR_BAD_BLOCK_SIZE`), a level raised into 6–7 on a smaller workspace
  (`ZXC_ERROR_BAD_LEVEL`) and a dictionary (`ZXC_ERROR_DICT_UNSUPPORTED`, also on a
  static `zxc_dctx`) are refused there. In `zxc_init_static_cctx()`, a block size or
  level of `0` is invalid.
- **Dictionaries are attached once and copied.** They were passed on each call. The
  setters now copy up to `ZXC_DICT_SIZE_MAX` + 128 bytes, so the caller's buffers are
  free on return. A failed copy gives `ZXC_ERROR_MEMORY`.
- **Stream functions read a context.** `zxc_stream_compress()` /
  `zxc_stream_decompress()` take it `const`, and `zxc_cstream_create()` /
  `zxc_dstream_create()` copy its parameters, so it can be freed right after. A
  context with a dictionary makes a push creator return `NULL`.
- **Thread counts mean workers**, with the reader and writer threads on top. `0` is
  [auto-detection](API.md#61-parameter-identifiers).
- **`ZXC_DECOMPRESS_TAIL_PAD` goes from 2112 to 2084 bytes**, and
  `zxc_decompress_block_bound()` follows. A destination sized with the old value stays
  large enough.

### CLI behaviour

- **Input files are kept.** Add `--rm` to remove them; it is ignored with `-c`, `-o`,
  an output file name, `-t` and `-l`, and `-k` takes precedence.
- **The output takes the input's metadata:** permission bits, access and modification
  times, group, and owner as root. A build rule comparing timestamps now sees the
  input's date. `--no-preserve` turns this off.
- **Every file operand counts.** 0.15.x ignored the extra ones, and `zxc -z -f a b c`
  overwrote `b`. More than two operands now need `-m`, and `-o` takes one input;
  anything else fails with `too many file operands`. `-c` and `-t` treat every operand
  as an input.
- **`zxc -t -j` over several files prints one JSON array.** One file still prints one
  object.
- **`-f` replaces the output only once the new one is complete.** A failed run used to
  lose the existing file.
- **A fatal signal removes the partial output**, except on Windows.
- **A read error on the input is a failure** (`ZXC_ERROR_IO`). 0.15.x took it for the
  end of the input and exited 0.
- **`-r` selects files by name.** Compressing skips `.zxc` files; decompressing and
  `-t` select only them. A link given as the starting path is ignored.
- **An output that is the input under another name is refused**, even with `-f`:
  another letter case on a case-insensitive filesystem, a short name, or, on Windows,
  a hard or symbolic link.
- **`-T N` means N workers**, with the reader and writer threads on top.
- **`zxc -V` changes format:** `ZXC CLI v1.0.0 "Hydrogen" (build arm64 64-bit LE), by ...`,
  without the license line.
- **On Windows, long options that take no argument reject one**, as on Linux and macOS.

### Bindings

The bindings move from `wrappers/` to `bindings/`. Their high-level calls keep their
shape, except:

- **The Go module moves to `github.com/hellobertrand/zxc/bindings/go`,** tagged
  `bindings/go/vX.Y.Z`; update your imports. Versions up to v0.15.0 stay under
  `wrappers/go`. The module now carries the C sources, so it needs cgo and a C
  compiler but no separate build of the library.
- **Go, Python, Node.js and WASM verify checksums by default when decoding,** so a bad
  checksum is now an error. Archives without checksums decode either way. Opt out with
  `zxc.WithChecksum(false)`, `checksum=False` or `checksum: false`. The Go block API
  stays off by default, because a bare block does not say whether it has a checksum.
- **Rust writes no checksums by default.** Ask with `with_checksum()`;
  `without_checksum()` is deprecated. Decoding still verifies by default.
- **Go and Rust refuse an output that is the input** in their file functions.
- **The Node.js package needs Node.js `^20.17.0` or `>=22.9.0`,** up from `>=16`.
- **An out-of-range level is an error:** `ErrBadLevel` in Go, `RuntimeError` in Python,
  a thrown error in Node.js and WASM.
- **Rust:** `compress_bound()`, `compress_block_bound()` and
  `decompress_block_bound()` return `usize`. `zxc-compress-sys` follows the C API.

---

## v8 → v9 (ZXC 0.15.x)

The compressed data is unchanged; the break is in the integrity fields and the seek
table. Tools that parse archives directly need these changes:

- **Header and block checksums are multiplicative** (`FORMAT.md` § 7.1). Block
  checksums cover the decoded bytes and are seeded with the block's position.
- **The whole-archive hash is gone**, replaced by an optional 8-byte **archive digest**
  in the footer (§ 7.3).
- **The footer stores its sizes on the bytes they need** (§ 8). Read the lengths from
  its last byte, or call `zxc_get_frame_info()`. `ZXC_FILE_FOOTER_SIZE` is replaced by
  `ZXC_FILE_FOOTER_MIN_SIZE` / `ZXC_FILE_FOOTER_MAX_SIZE` (digest excluded).
- **The seek table is rebuilt as groups** of one 64-bit anchor and 64 block sizes,
  announced by the `HAS_SEEK_TABLE` header flag (bit 5).
- **Dictionaries move to `.zxd` version 2:** see
  [Migrating a dictionary](#migrating-a-dictionary).

### Windows archives (0.15.x)

The archives are built with MinGW-w64 (UCRT) and ship `bin/libzxc.dll`, its MinGW
import library and `lib/libzxc.def`. The MSVC static `lib/zxc.lib` of 0.14.x is gone,
and the archive's CMake package and `libzxc.pc` target MinGW. MSVC projects build an
import library (`lib /def:lib\libzxc.def /out:zxc.lib /machine:x64`, or `arm64`) or
build zxc from source for a static library. A CMake MSVC build names the DLL
`libzxc.dll` (0.14.x: `zxc.dll`) and keeps the import library `zxc.lib`.

### CLI output (0.15.x)

- `zxc -r` skips symbolic links, junctions and mount points, and exits non-zero when
  a directory or an entry cannot be read.
- Concatenated archives decode as one stream, and the listing describes every frame.
- `zxc -l -j` and `zxc -t -j`: `checksum_method` becomes `checksum`, valued `enabled`,
  `disabled` or `mixed`. `-l -j` adds `seekable` (same values) and `frames`.
- `zxc -l -j`: `block_size_kb` and `dict_id` become arrays when frames differ.
- `zxc -l`: new `Seekable` and `Frames` columns, `mixed` where frames differ, and the
  file name only when several are listed.
- `zxc -l -v`: `Checksum Method:` becomes `Checksum:`, and every frame's block sizes
  and dictionary IDs are listed.

## Older format breaks

The table at the top gives the headline; the recipes below apply to each.

- **v7 → v8:** nothing changed in the compression. Blocks carry 0, 4 or 8 descriptor
  bytes instead of 32 (SPL) or 24 (PAK), and `enc_litlen` is renamed **`enc_tok`**. A
  block must leave 32 readable bytes behind its literal section. This saves about 2 %
  at 4 KB blocks.
- **v6 → v7:** Huffman literal sections (`enc_lit = 2`) use the same code with the bits
  grouped by tree level. Level 7 also Huffman-codes the token stream. Block types are
  unchanged.
- **v5 → v6:** the **NUM** block was removed, and **PAK** moved from type 3 to type 2.
  To land straight on v9, pipe a v5 build into a v9 build; there is no need to stop at
  the versions in between.

---

## Do I need to migrate?

Only if you have archives from **v8** or earlier **and** want to read them with
**v9** tools or standardize on v9. The old build still reads its own archives, so
there is no rush.

## Check an archive's format version

The version is the byte at offset `0x04` of the file header (`zxc -V` reports the
*tool* version):

```sh
xxd -s 4 -l 1 archive.zxc      # -> "05" = v5 ... "08" = v8, "09" = v9
```

## Migrate: transcode with the old build, recompress with v9

`zxc-old` reads the old format and `zxc-new` is ZXC 0.15.x or later:

```sh
zxc-old -dc old.zxc | zxc-new -z -c > new.zxc
```

The archive does not record its encoding level, so pick the new options explicitly:

```sh
zxc-old -dc old.zxc | zxc-new -z -6 -c > new.zxc     # densest fast-decode tier
zxc-old -dc old.zxc | zxc-new -z -7 -c > new.zxc     # ULTRA, densest level
zxc-old -dc old.zxc | zxc-new -z -N -c > new.zxc     # no checksums
zxc-old -dc old.zxc | zxc-new -z -B 1M -c > new.zxc  # 1 MB blocks
zxc-old -dc old.zxc | zxc-new -z -S -c > new.zxc     # keep it seekable
```

An archive compressed **with a dictionary** needs it on the old side
(`zxc-old -dc -D dict.zxd old.zxc | ...`), and a
[version-2 dictionary](#migrating-a-dictionary) on the new side.

Without a pipe, and in bulk:

```sh
zxc-old -dc old.zxc > tmp.raw && zxc-new -z -c tmp.raw > new.zxc && rm tmp.raw

for f in *.zxc; do
    zxc-old -dc "$f" | zxc-new -z -c > "migrated/$f" || echo "FAILED: $f"
done
```

## Migrating a dictionary

A v9 build rejects a version-1 `.zxd` (0.14.x) with `ZXC_ERROR_BAD_VERSION`, and no
tool upgrades one in place. There are two routes.

**Retrain**, the simple one. The new dictionary gets its own `dict_id`:

```sh
zxc-new --train samples/* -o dict-v9.zxd
zxc-old -dc -D dict-v8.zxd old.zxc | zxc-new -z -D dict-v9.zxd -c > new.zxc
```

**Convert byte for byte** to keep the content, the shared Huffman table and the
`dict_id`. Edit the 16-byte header (`FORMAT.md` § 12.4):

1. set the version byte at `0x04` to `02`;
2. recompute the 2-byte header checksum of § 7.1 over the 16-byte header with
   bytes `0x0E..0x0F` zeroed, and store it little-endian at `0x0E`.

## Verify the result

```sh
zxc-new -t new.zxc                             # integrity check (v9)
diff <(zxc-old -dc old.zxc) <(zxc-new -dc new.zxc) && echo OK   # byte-identical output
```

## Notes

- **Keep the old build** until every archive you care about is migrated: nothing else
  reads the old format.
- A transcode **re-encodes** the data, so the archive's bytes and size may differ even
  at the same level; the *decompressed output* is identical.
