# Contributing to ZXC

Thank you for your interest in contributing to ZXC! This guide will help you get started.

## Developer Certificate of Origin (DCO)

ZXC uses the [Developer Certificate of Origin 1.1](https://developercertificate.org/).
The full text is in the [`DCO`](../DCO) file at the root of this repository.

By adding a `Signed-off-by` line to your commits, you certify the four clauses
of the DCO — in short: that you wrote the contribution or otherwise have the
right to submit it under the project's BSD-3-Clause license, and that you
understand the contribution is public and will be kept in the project history
indefinitely, together with the name and email address in your sign-off.

Sign off each commit as you write it:

```bash
git commit -s -m "feat: your commit message"
```

This appends the trailer automatically, using your configured `user.name` and
`user.email`:

```
Signed-off-by: Your Name <you@example.com>
```

The sign-off records your own certification, taken from your `user.name` and
`user.email`. Pseudonyms and GitHub `noreply` addresses are accepted; anonymous
contributions are not.

It does not have to match the commit author. If you are submitting someone
else's unmodified work under clause (c) of the DCO, keep their `Signed-off-by`
line and add yours below it, so that the chain of sign-offs stays complete.

A DCO check runs on every pull request and must pass before it can be merged.
If you forgot a sign-off, the check output explains how to fix it.

## License Headers
To maintain legal clarity and recognize all contributors, every new source file (.c, .h, .rs, .py, etc.) must include the following header at the very top:

```C
/*
 * ZXC - High-performance lossless compression
 *
 * Copyright (c) 2025-2026 Bertrand Lebonnois and contributors.
 * SPDX-License-Identifier: BSD-3-Clause
 */
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
