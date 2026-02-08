# Contributing to ZXC

Thank you for your interest in contributing to ZXC! This guide will help you get started.

## Developer Certificate of Origin (DCO)

By contributing, you certify that:
- You have the right to submit the contribution
- You agree to license your contribution under the BSD-3-Clause license

Add this to your commits:
```bash
git commit -s -m "Your commit message"
```

## Quick Start

### Build and Test

```bash
git clone https://github.com/hellobertrand/zxc.git
cd zxc
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Debug
make -j
ctest --output-on-failure
```

### Format Code

```bash
clang-format -i src/lib/*.c include/*.h
```

## Requirements

- **C17** compiler (GCC, Clang, or MSVC)
- **CMake** 3.10+
- Follow `.clang-format` style (Google)
- All code must be ASCII-only
- Pass `ctest` and static analysis

## Submitting Changes

1. Fork and create a feature branch
2. Add tests for new functionality
3. Ensure CI passes (build, tests, benchmarks)
4. Sign your commits with `-s`
5. Open a PR to `main`

## Reporting Issues

Include:
- ZXC version (`zxc --version`)
- OS and architecture
- Minimal reproduction steps

Thank you for making ZXC better!
