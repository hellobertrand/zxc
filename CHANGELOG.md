# Changelog

## [0.11.0] - 2026-05-13
api: Increases default block size to 512 KB (#216)
api: Introduces Level 6 compression with Huffman literals (#208)
api: Feat: Add WHATWG TransformStream adapters and `detectZxc` utility (#211)
api: Add Go io.Reader/Writer adapters for zxc streams (#209)
api: Introduce push-based streaming API for non-blocking integrations (#204)
api: Harden block API: overflow fix, memory shrink, wrapper coverage (#198)
api: Integrates seekable API fuzzer (#196)
fix: Fix TSan data race between async writer and shutdown sentinel (#201)
perf: Introduce compression level 6 and tune default block size
perf: Update whitepaper with Decompression Bandwidth Frontier graph
perf: Refresh Apple M2 benchmark data and clarify effective throughput formula
perf: Update whitepaper and README with AMD EPYC 7763 benchmarks
perf: Update documentation with zxc v0.11.0 effective throughput benchmark
perf: Optimize LZ77 match finding for fast levels with tag-first filter (#215)
cli: Customize release artifacts with tailored README and manual
build: Adds LEVEL_DENSITY support to wrappers (#214)
build: Feat: Add `std::io` streaming adapters to Rust wrapper (#213)
build: Fix: allow direct Emscripten module factory injection in WASM wrapper
build: Reverts LZbench source to upstream repository
build: Updates CI for Go ARM64 and Node.js 22 (#195)
doc: Remove Python scripts for benchmark chart generation
doc: Migrate benchmark figures to SVG format and streamline documentation
doc: Refine benchmark presentation and standardize documentation terminology
doc: Refresh v0.11.0 benchmark graphs and update documentation references
doc: Update README with current fuzzing iteration count
doc: Update whitepaper: streamline memory usage and remove ratio benchmarks
doc: Update documentation with zxc v0.11.0 benchmarks for Apple M2
doc: Update documentation with zxc v0.11.0 benchmarks for Apple M2
doc: Update whitepaper memory usage statistics
doc: Update documentation with zxc v0.11.0 benchmarks for Apple M2
doc: Update documentation with zxc v0.11.0 benchmarks for AMD EPYC 9B45
doc: Update documentation with zxc v0.11.0 benchmarks for Google Axion
doc: Update README memory usage statistics to reflect recent optimizations
doc: Add Node.js stream.Transform adapters and zxc frame detection (#212)
doc: Add Free Pascal community binding to README + EXAMPLES
doc: Adds Nim bindings to README and EXAMPLES
doc: Updates README with TurboBench verification details
doc: Updates README with architecture support and ARM performance details
doc: Updates formatting in README performance table
doc: Updates benchmark decompression cycles image for v0.10.0
doc: Updates benchmark performance metrics in documentation
misc: Bump version to v0.11.0
misc: Bump version to v0.10.1
misc: Feat: add Python io.RawIOBase adapters and zxc magic detection (#210)
misc: Refine: Use named constants for compression level checks
misc: Bump mymindstorm/setup-emsdk from 14 to 16 (#206)
misc: Bump softprops/action-gh-release from 2 to 3 (#205)
misc: Refine: remove PPA requirement for GCC on Ubuntu 22.04 in CI (#207)
misc: Harden decompression logic against integer overflows on 32-bits platforms (#202)
misc: Split monolithic tests (#200)

## [0.10.0] - 2026-04-16
api: Bumps version to 0.10.0 and updates benchmarks and documentation
api: Adds Seekable API documentation and bumps SOVERSION to 3
api: Bumps version to 0.10.0
api: Simplifies checksum selection and standardizes on RapidHash
api: Adds seekable archives for random access (#188)
api: Adds WebAssembly (WASM) support (#189)
api: Adds runtime library information API (#163)
api: Introduces enum for future pluggable hashing (#153)
api: Add block-level API for compression without file framing (#148)
api: Add clang-format integration and top-level Makefile (#147)
fix: Fixes CLI error handling and edge cases (#170)
perf: Refreshes benchmark performance metrics for competitor libraries
perf: Refreshes benchmark performance metrics in whitepaper
perf: Refreshes benchmark performance metrics in whitepaper
perf: Refreshes benchmark performance metrics in README
perf: Adds block size tuning guidelines and benchmarks to documentation
perf: Optimizes literal copying with a unified 32-byte fast path (#181)
perf: Optimizes LZ77 match finding with split hash table (#179)
perf: Optimizes core compression and decompression logic (#177)
perf: Optimizes block compression and decompression (#164)
perf: Improves workflows (#166)
perf: Enhances I/O buffering (#162)
perf: Optimizes `log2` and enables BMI/LZCNT extensions (#159)
cli: Standardizes PGO and documents usage (#185)
cli: Refines stream engine for robustness (#146)
cli: Change default file extension to .zxc (#143)
cli: Updates CLI default checksum settings (#142)
build: Bumps WASM package version to 0.10.0
build: Updates workflow documentation in README
build: Updates README badge layout
build: Adds Windows ARM64 Build and Refines CI (#194)
build: Updates triggers for the WASM build workflow
build: Updates GitHub Actions documentation for WASM and security workflows
build: Update documentation for ZXC_DISABLE_SIMD
build: Add option to disable explicit SIMD code paths (#174)
build: Updates Xcode 26.4 path in CI workflows
build: Upgrade CI to macOS 26 and latest compilers (#169)
build: Update rand requirement from 0.9 to 0.10 in /wrappers/rust (#168)
build: Adds fuzzing coverage report workflow (#160)
build: Update benchmark suite
build: Expands security analysis and updates Go CI (#152)
build: Expands security analysis and updates Go CI (#151)
build: Adds official Go wrapper (#149)
build: Update security badges in README
build: Update lzbench source in benchmark workflow
doc: Updates status badges in README
doc: Updates benchmark methodology and performance claims in documentation
doc: Updates decompression efficiency benchmark graph to v0.10.0
doc: Refreshes benchmark data for v0.10.0 in documentation
doc: Updates benchmark data and hardware targets in documentation
doc: Updates version to 0.10.0 and refreshes benchmark data in documentation
doc: Updates benchmark environment details in documentation
doc: Refines varint decoding and match length (#186)
doc: Streamlines internal encoding and block size definitions (#175)
doc: Parameterizes lazy matching length threshold (#158)
doc: Add compression ratio benchmarks to the whitepaper
doc: Update benchmark graphs for version 0.9.0
misc: Updates Python publish runner to ubuntu-latest
misc: Defines ZXC_STATIC_DEFINE for Rust sys-crate build
misc: Defines versioning policy and error handling requirements
misc: Defines versioning policy and error handling requirements
misc: Refactors varint encoding and hash variables for clarity (#183)
misc: Hardens decompression overflow checks with padding margin (#146) (#178)
misc: Bump codecov/codecov-action from 5 to 6 (#167)
misc: Factorize decompression macros to consolidate copy logic (#150)
misc: Exclude hard-to-test error paths from code coverage
misc: Rename pkg-config module from zxc to libzxc (#144)
misc: Cache context variables locally in encoder blocks (#141)

## [0.9.1] - 2026-03-16
fix: Fix CLI parsing and adds block size option (#140)
doc: Add tar integration instructions to README
misc: Bumps version to 0.9.1

## [0.9.0] - 2026-03-15
api: Add flexible block sizes options and reusable contexts (#138)
cli: Update license headers and add SPDX identifier
build: Bump version to 0.9.0
build: Use upstream lzbench and shallow clone in benchmark workflow
doc: Update documentation for sticky options in reusable contexts
doc: Add Homebrew installation instructions to README
doc: Add Homebrew packaging status badge to README
doc: Update README with installation instructions and packaging status
doc: Add Conan installation section in README (#136)
doc: Update documentation
misc: Update benchmark images for 0.9.0
misc: Configures corpus storage for fuzzing CI (#139)
misc: Implement direct decompression fast path (#135)

## [0.8.3] - 2026-03-06
build: Add compiler compatibility CI workflow (#134)
misc: Bumps version to 0.8.3

## [0.8.2] - 2026-03-05
api: Optimizes fuzzers for buffer API and memory reuse (#126)
fix: Fix file handle leak in CLI benchmark (#132)
cli: Resource management and enforces thread limits (#131)
cli: CLI: enhances robustness and resource handling (#130)
misc: Bumps version to 0.8.2
misc: Refine output file closing logic to avoid closing stdout (#133)
misc: Prevents out-of-bounds memory access (#129)

## [0.8.1] - 2026-03-01
build: Updates wrapper versions and refactors ABI versioning
build: Adds Node.js wrapper badge

## [0.8.0] - 2026-03-01
api: Adds Node.js wrapper for zxc compression (#107)
api: Refactors error handling with errors codes, improves docs and benchmark CLI (#103)
fix: Fixes CLI benchmark deadline calculation (#116)
fix: Fix potential buffer overflows, decoder underflows, and path handling issues (#105)
fix: Fixes typos in documentation
perf: Optimizes Node.js wrapper builds and CI
perf: Optimize LZ77 hashing strategy and standardize hash table configuration (#106)
cli: Adds recursive directory mode (#112)
cli: Enforces const correctness for local variables (#110)
build: Adds CI support for Alpha architecture (#122)
build: Wrapper Python: closes file handles in stream tests (#115)
build: Bumps version to 0.8.0
build: Updates benchmark branch
build: Wrapper Python: exposes C error constants and enhances error reporting (#111)
build: Adds vcpkg installation instructions
build: Adds multi-arch support for more architectures
doc: Refresh benchmark results for zxc 0.8.0
doc: Adopts dual-scale encoding for chunk size (#118)
doc: Standardizes block unit and chunk size logic (#114)
doc: Replaces benchmark PNG images with WebP
doc: Adds zxc command man page
doc: Format: Implement LZ offset bias (+1) to eliminate zero-offset attack vectors (#104)
doc: Update README with additional package badges
doc: Updates README with TL;DR
doc: Adds vcpkg badge to README
misc: Bump actions/download-artifact from 7 to 8 (#121)
misc: Bump actions/upload-artifact from 6 to 7 (#120)
misc: Refactors path handling for robustness (#119)
misc: Revert "Standardizes block unit and chunk size logic (#114)" (#117)
misc: Clarifies rapidhash component license
misc: Adds file format specification
misc: Refine introduction wording.
misc: Add comprehensive ZXC file format spec with byte-level structures and worked hexdump example

## [0.7.3] - 2026-02-18
fix: Fix workfows builds
build: Bumps version to 0.7.3
build: Moves rapidhash.h to vendor directory
misc: Finds rapidhash via system or vendored fallback
misc: Use lzbench master branch

## [0.7.2] - 2026-02-16
fix: Fix warning for AVX2/AVX512 macro redefinition in native mode build (#94)
build: Bumps version to 0.7.2

## [0.7.1] - 2026-02-14
fix: Fixes potential out-of-bounds read in decompression (#92)
doc: Updates documentation
misc: Specifies Release config for /O2 flag (#93)
misc: Bump version to 0.7.1
misc: Uses zxc-add-0.7.x branch of lzbench fork
misc: Bumps version to 0.7.1

## [0.7.0] - 2026-02-13
fix: Fix potential out-of-bounds read (static analysis) (#89)
cli: Adds JSON output option for CLI (#88)
build: Refactors multiarch CI to use Debian containers (#91)
build: Enables big-endian architecture support (#86)
misc: Bump version to 0.7.0

## [0.6.3] - 2026-02-11
api: Add shared library support with BUILD_SHARED_LIBS option (#81)
fix: Fix rust version test
fix: Fix whitepaper links to graphs
build: Improves workflows and wrapper builds (#85)
build: Bump version to 0.6.3
build: Updates binding package names (#84)
build: Add Python wrapper (#75)
doc: Update bindings docs
misc: Add edge case tests (#83)
misc: Add Rust support to CodeQL security analysis and fix config path filtering. (#82)

## [0.6.2] - 2026-02-09
fix: Fixes Rust wrapper build for cargo publish (#69)
cli: Fixes: addresses vulnerabilities and input validation into CLI (#77)
cli: Enables code coverage and configures thresholds
cli: Adds test for decompressed size function (#73)
cli: Enables code coverage reporting (#72)
build: Reverts to official LZbench repository
build: Wrapper Rust: improves compression/decompression handling (#70)
doc: Move WHITEPAPER to docs folder
misc: Bump version to 0.6.2
misc: Add snyk config file
misc: Updates license headers and contributing guidelines (#76)
misc: Updates contribution guidelines
misc: Adds TODO list for future development

## [0.6.1] - 2026-02-06
api: Adds Rust wrapper for ZXC compression (#68)
misc: Bump version to 0.6.1
misc: Corrects bit reader initialization (#67)

## [0.6.0] - 2026-02-05
api: Improves compression, ratio and data integrity (#65)
build: Update vendors workflow
build: Automatic update of rapidhash.h (#59)
build: Adds workflow to update rapidhash.h
build: Refactors CI workflows and removes SDE testing (#56)
doc: Adds brotli to lzbench benchmark (#58)
misc: Bump peter-evans/create-pull-request from 7 to 8 (#64)
misc: Adds funding file

## [0.5.1] - 2026-01-22
fix: Fixes ARM32 compilation (#55)
cli: Enhances CLI input/output security (#52)
cli: I/O security, Bit Packing improving and code cleanup (#51)
misc: Enables NEON32 support (#54)
misc: Bump softprops/action-gh-release from 1 to 2 (#53)

## [0.5.0] - 2026-01-20
fix: Fix benchmark graph to reflect v0.5.0
perf: Improves compression with rapidhash  integration and new GHI block format (#43)
build: Implements runtime CPU feature dispatch (#50)
build: Enhances code quality and security checks
build: Updates benchmark workflow
doc: Improves RLE and vbyte encoding (#45)
doc: Updates copyright year
misc: Adds bounds checks to decode block (#49)
misc: Applies size mask to section sizes
misc: Refactors RLE token handling for clarity
misc: Removes release template file
misc: Updates copyright year

## [0.4.0] - 2026-01-07
perf: v0.4.0 - Variable offsets, optimized parsing & layout (#23)

## [0.3.3] - 2026-01-06
fix: Fixes buffering issues with stdin and stdout (#37)
portability: set binary mode for standard streams (#39)
misc: Increments patch version to 0.3.3 (#38)

## [0.3.2] - 2026-01-05
cli: Updates build system and CI configuration (#24)
build: Add CI with multi-arch builds + dependabot (#30)
build: Improves file handling and code robustness (#28)
build: Configures CodeQL with config file (#27)
build: Adds CodeQL security analysis (#26)
misc: Bump actions/checkout from 4 to 6 (#31)
misc: Bump actions/upload-artifact from 4 to 6 (#32)
misc: Bump actions/download-artifact from 4 to 7 (#35)
misc: Bump actions/cache from 4 to 5 (#33)
misc: Bump github/codeql-action from 3 to 4 (#34)
misc: Replaces not operator for branchless evaluation. (#29)

## [0.3.1] - 2025-12-27
doc: Add community bindings to readme (#21)
doc: Update documentation
misc: Increments patch version to 0.3.1
misc: Fix: flush stdout buffer before exit when using -c flag (#22)

## [0.3.0] - 2025-12-26
perf: Improves compression and decompression (#18)
build: Configures fuzzing workflows for multiple targets
misc: Use -1..-5 as compression levels (#16)
misc: add short options to help, version (#15)

## [0.2.0] - 2025-12-19
api: Updates include path in fuzz test
api: Restructure public headers to provide a "sans-IO" API (#9)
fix: Fixes potential buffer overflow in decompression
fix: Fix fuzzers names
perf: Optimize hot path
cli: Updates includes for buffer and constants
build: Removes 'candidate' branch from CI workflows
build: Improves decompression robustness and fixes bugs
misc: Initializes memory block after allocation
misc: Suppresses cppcheck false positive
misc: Refactors compression context buffer management
misc: Reduces memory usage of chain table
misc: Reduces thread contention in stream engine
misc: Refactors file closing and adds benchmark mode
misc: Updates atomic type definition
misc: Improves fuzzing and stream handling
misc: Adds checks to prevent buffer overflows
misc: Remove duplicate docs from *.c files
misc: Updates fuzzing schedule
misc: Adds size check for raw blocks
misc: Adds capacity check to avoid buffer overflow
misc: Adds bounds check for bits value
misc: Prevents buffer overflows during decompression
misc: Moves buffer allocation for I/O performance
misc: Removes unused memory freeing
misc: Signals reader on I/O error in async writer
misc: Updates I/O error flag to atomic type
misc: Refactors fuzzing tests for better coverage
misc: Format code

## [0.1.2] - 2025-12-17
fix: Fixes potential null pointer dereference
doc: Adds parameter documentation
misc: Adds more unit tests
misc: Removes redundant file pointer check
misc: Adds input validation to prevent crashes

## [0.1.1] - 2025-12-17
build: Adds name to fuzzing job for clarity
build: Enhances fuzzing workflow with manual control
build: Updates fuzzing workflow concurrency group
build: Adds fuzzing infrastructure with ClusterFuzzLite
doc: Update README
misc: Increments patch version to 0.1.1
misc: Quotes cron expression for fuzzing schedule
misc: Prevents potential buffer overruns
misc: Adds zstd_fast to benchmark
misc: Use official lzbench repo for benchmarks

## [0.1.0] - 2025-12-15
api: Update API documentation
api: Enhances C/C++ integration with examples
fix: Fixes CRC32 detection for SSE4.2 (#6)
fix: Fixes compilation on Windows
perf: Optimizes decompression for ARM64
build: Removes path restrictions on workflows
build: Updates concurrency group key for workflows
build: Excludes Markdown files from CI triggers
build: Updates benchmark branch
build: Adds CI workflows for code quality and benchmarks
doc: Initializes the ZXC compression library
misc: Adds NEON 32-bit support (#3)
misc: Adds code ownership using CODEOWNERS
misc: Documenting SIMD Instructions
misc: Improves code readability
misc: Adds initial contribution guidelines
misc: Adds initial implementation of ZXC library
misc: Adds the project license
misc: Initial commit


