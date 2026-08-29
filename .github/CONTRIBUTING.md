# Contributing to ZXC

Thank you for your interest in contributing to ZXC! This guide will help you get started.

## Developer Certificate of Origin (DCO)

ZXC uses the [Developer Certificate of Origin 1.1](https://developercertificate.org/).
The full text is reproduced in the [appendix](#appendix-developer-certificate-of-origin-11)
at the end of this document.

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

## Appendix: Developer Certificate of Origin 1.1

Reproduced verbatim from <https://developercertificate.org/>.

```text
Developer Certificate of Origin
Version 1.1

Copyright (C) 2004, 2006 The Linux Foundation and its contributors.

Everyone is permitted to copy and distribute verbatim copies of this
license document, but changing it is not allowed.


Developer's Certificate of Origin 1.1

By making a contribution to this project, I certify that:

(a) The contribution was created in whole or in part by me and I
    have the right to submit it under the open source license
    indicated in the file; or

(b) The contribution is based upon previous work that, to the best
    of my knowledge, is covered under an appropriate open source
    license and I have the right under that license to submit that
    work with modifications, whether created in whole or in part
    by me, under the same open source license (unless I am
    permitted to submit under a different license), as indicated
    in the file; or

(c) The contribution was provided directly to me by some other
    person who certified (a), (b) or (c) and I have not modified
    it.

(d) I understand and agree that this project and the contribution
    are public and that a record of the contribution (including all
    personal information I submit with it, including my sign-off) is
    maintained indefinitely and may be redistributed consistent with
    this project or the open source license(s) involved.
```
