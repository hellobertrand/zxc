# Building and Integrating ZXC

Prebuilt binaries, package managers and the release archives are covered in the
[README](../README.md#installation). This document is for the two cases that
need more room: **vendoring** zxc into another build, and **building it from
source**.

## Building from source (CMake)

**Requirements:** CMake 3.14+, a C17 compiler (Clang, GCC or MSVC).

```bash
git clone https://github.com/hellobertrand/zxc.git
cd zxc
```

### Single-config generators (Unix Makefiles, Ninja)

The default on Linux and macOS. The build type is fixed at configure time.

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel

# Run tests. CMake 3.20+ can use: ctest --test-dir build --output-on-failure
cd build && ctest --output-on-failure && cd ..

# CLI usage
./build/zxc --help

# Install library, headers, pkg-config and CMake package files.
# CMake 3.15+ can use: sudo cmake --install build
sudo cmake --build build --target install
```

### Multi-config generators (Visual Studio, Xcode, Ninja Multi-Config)

Here the build type is chosen at *build* time, so `CMAKE_BUILD_TYPE` is ignored at configure time
and `--config` is required at build, test and install time. Artifacts land in a per-configuration
subdirectory: the CLI is `build/Release/zxc`, not `build/zxc`.

```bash
cmake -B build
cmake --build build --config Release --parallel

cd build
ctest -C Release --output-on-failure
cd ..

cmake --build build --config Release --target install
```

Installing into a system prefix needs `sudo` on Unix or an elevated shell on Windows. Pass
`-DCMAKE_INSTALL_PREFIX=<dir>` at configure time to install somewhere writable instead.

### CMake options

| Option | Default (standalone) | Default (vendored) | Description |
|--------|----------------------|--------------------|-------------|
| `BUILD_SHARED_LIBS` | OFF | OFF | Build shared libraries instead of static (`libzxc.so`, `libzxc.dylib`, `zxc.dll`) |
| `ZXC_NATIVE_ARCH` | ON | OFF | Enable `-march=native` for maximum performance |
| `ZXC_ENABLE_LTO` | ON | OFF | Enable Link-Time Optimization (LTO) |
| `ZXC_PGO_MODE` | OFF | OFF | Profile-Guided Optimization mode (`OFF`, `GENERATE`, `USE`) |
| `ZXC_BUILD_CLI` | ON | OFF | Build command-line interface |
| `ZXC_BUILD_TESTS` | ON | OFF | Build unit tests |
| `ZXC_INSTALL` | ON | OFF | Generate install rules (headers, pkg-config, CMake package) |
| `ZXC_ENABLE_COVERAGE` | OFF | OFF | Enable code coverage generation (disables LTO/PGO) |
| `ZXC_DISABLE_SIMD` | OFF | OFF | Disable hand-written SIMD paths (AVX2/AVX512/NEON) |
| `ZXC_USE_SYSTEM_RAPIDHASH` | OFF | OFF | Use a system-installed `rapidhash.h` instead of the vendored copy |

"Vendored" is a build where zxc is not the top-level project (`add_subdirectory()`,
`FetchContent`): the embedding project then keeps control of its own compiler flags,
test registration and install set.

```bash
# Build shared library
cmake -B build -DBUILD_SHARED_LIBS=ON

# Portable build (without -march=native)
cmake -B build -DZXC_NATIVE_ARCH=OFF

# Library only (no CLI, no tests)
cmake -B build -DZXC_BUILD_CLI=OFF -DZXC_BUILD_TESTS=OFF

# Code coverage build
cmake -B build -DZXC_ENABLE_COVERAGE=ON

# Disable explicit SIMD code paths (compiler auto-vectorisation is unaffected)
cmake -B build -DZXC_DISABLE_SIMD=ON
```

### Profile-Guided Optimization (PGO — Clang and GCC only)

PGO uses runtime profiling data to optimize branch layout, inlining decisions, and code placement.

> **Not available with MSVC.** `ZXC_PGO_MODE` is accepted there but emits no instrumentation and no
> profile-use flags: `cmake/zxcCompilerFlags.cmake` gates the whole PGO block on `NOT MSVC`, and so
> does the missing-profile check. `GENERATE` and `USE` therefore produce an ordinary build, without
> a warning. The steps below assume Clang or GCC.

**Step 1 - Build with instrumentation:**
```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release -DZXC_PGO_MODE=GENERATE
cmake --build build --parallel
```

**Step 2 - Run a representative workload to collect profile data:**
```bash
# Run the test suite (exercises all block types and compression levels)
./build/zxc_test

# Or compress/decompress representative data
./build/zxc -b your_data_file
```

**Step 3 - (Clang only) Merge raw profiles:**
```bash
# Clang generates .profraw files that must be merged before use
llvm-profdata merge -output=build/pgo/default.profdata build/pgo/*.profraw
```
> GCC uses a directory-based format and does not require this step.

**Step 4 - Rebuild with profile data:**
```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release -DZXC_PGO_MODE=USE
cmake --build build --parallel
```

## CMake subproject (vendored)

zxc can be vendored directly into a CMake build, either as a git submodule with
`add_subdirectory()` or through `FetchContent`:

```cmake
include(FetchContent)
FetchContent_Declare(zxc
    GIT_REPOSITORY https://github.com/hellobertrand/zxc.git
    GIT_TAG        v0.14.0
)
FetchContent_MakeAvailable(zxc)

target_link_libraries(myapp PRIVATE zxc::zxc_lib)
```

`zxc::zxc_lib` is the same target name the installed package exports, so
switching between a vendored copy and `find_package(zxc)` needs no other
change.

When zxc is not the top-level project it builds the library only: the CLI, the
tests, `-march=native`, LTO and the install rules all default to off, so the
embedding project keeps full control of its own CTest registration and install
set. Any of them can still be turned back on explicitly (`-DZXC_BUILD_CLI=ON`,
`-DZXC_NATIVE_ARCH=ON`, `-DZXC_INSTALL=ON`, ...). `-march=native` is also
ignored whenever CMake is cross-compiling, since it would encode the build
host's ISA.

Compiler flags follow the same rule. Vendored, zxc adds nothing to what it
inherits from the parent: the optimisation level comes from the build type, and
the warning level (`-Wall -Wextra`, `/W3`) and code generation policy
(`-fomit-frame-pointer`, `-fstrict-aliasing`, `-ffunction-sections`,
`-fdata-sections` and the matching dead-strip link options) are the embedding
project's to set. An embedder that builds with frame pointers for its profiler,
its own aliasing rules or a quiet build log keeps them. A configure-time warning
fires if neither a build type nor an optimisation flag is set, since zxc would
then be built unoptimised.

Third-party code is vendored, never probed: `rapidhash.h` comes from the copy in
the tree unless `-DZXC_USE_SYSTEM_RAPIDHASH=ON` asks for a system one, so a
build cannot silently pick up a header from the host.

## Meson subproject (or WrapDB)

zxc ships a native `meson.build`, so any Meson project can pull it in as a
subproject or via [WrapDB](https://mesonbuild.com/Wrapdb-projects.html).

**1. Create `subprojects/zxc.wrap`:**
```ini
[wrap-git]
url = https://github.com/hellobertrand/zxc.git
revision = head
depth = 1

[provide]
libzxc = libzxc_dep
```

**2. Use the dependency in your `meson.build`:**
```meson
project('my_project', 'c', default_options : ['c_std=c17'])

zxc_dep = dependency('libzxc', fallback : ['zxc', 'libzxc_dep'])
executable('my_app', 'main.c', dependencies : zxc_dep)
```

**3. Build and run:**
```bash
meson setup build
meson compile -C build
./build/my_app
```

When consumed as a subproject, only the library is built (CLI and tests are
skipped automatically).
