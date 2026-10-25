# Tests

Everything that exercises the library and the CLI lives here, one suite per
directory. `wrappers/*/tests` hold the language bindings' own suites.

```
unit/          C unit tests of the public API and internals (ctest target: zxc_test)
cli/           End-to-end tests of the zxc binary (test_cli.sh)
fuzz/          libFuzzer harnesses (ClusterFuzzLite, OSS-Fuzz, fuzzing.yml)
conformance/   Public decoder vectors, frozen per format version
format/        Golden files that pin the encoder's exact output bytes
packaging/     Consumer project that checks the installed CMake package and .pc
```

## unit/

One file per API surface (`test_buffer_api.c`, `test_stream_api.c`, ...), all
linked into a single `zxc_test` binary by `cmake/zxcTests.cmake` and
`meson.build`. `test_main.c` lists every case in a `TEST_CASE(...)` table;
CMake reads that table and registers one ctest per case, so a new test needs
a declaration in `test_common.h`, a definition in the right `test_*.c`, and a
`TEST_CASE` line. Nothing else.

```sh
cmake -S . -B build -DZXC_BUILD_TESTS=ON && cmake --build build
ctest --test-dir build -j8                 # everything, incl. conformance and format_golden
./build/zxc_test --exact test_dict_zxd_roundtrip
```

## cli/

`test_cli.sh` drives the built `zxc` binary through compression, decompression,
streaming, metadata preservation, `zxcless` and `zxcgrep`. It takes the binary
path as its only argument and defaults to `../../build/zxc`.

```sh
./tests/cli/test_cli.sh build/zxc
```

CI runs it in `build.yml` (Linux, macOS, Windows), `multiarch.yml` and
`coverage.yml`.

## fuzz/

One `fuzz_<target>.c` per harness: `decompress`, `roundtrip`, `seekable`,
`pstream`, `dict`, `stream`, `inplace`, `block`. They are not part of the CMake
build. `.clusterfuzzlite/build.sh` compiles them for ClusterFuzzLite and for
OSS-Fuzz, whose upstream `build.sh` delegates to it; `fuzzing.yml` replays the
corpus and reports coverage on pull requests. Adding a harness means a new file
here plus its name in the `FUZZERS` list of `.clusterfuzzlite/build.sh` and a
line in `fuzzing.yml`.

## conformance/ and format/

Both are byte-frozen corpora with their own README and `sha256` manifest.
`conformance/` is the contract any decoder must honour: valid archives with
their expected output, invalid ones that must be rejected, one directory per
format version. `format/` pins the bytes this encoder emits for every block
type. `vector-stability.yml` fails on any change to either manifest;
`tests/format/vector_io.h` is the file I/O both tools share.

## packaging/

A minimal consumer built against the installed library, by `packaging.yml` and
`build.yml`, to check `find_package(zxc)` and the `pkg-config` file.
