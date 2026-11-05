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
To maintain legal clarity and recognize all contributors, every new source file must start with an SPDX line followed by the copyright notice. The SPDX line is the first line, or the second after a shebang, and uses the file's own comment syntax:

| Files | SPDX line |
|---|---|
| `.c`, `.cc`, `.rs`, `.go`, `.js`, `.mjs`, `.ts` | `// SPDX-License-Identifier: BSD-3-Clause` |
| `.h` | `/* SPDX-License-Identifier: BSD-3-Clause */` |
| `.py`, `.pyi`, `.sh`, `.cmake`, `.cmake.in`, `CMakeLists.txt`, `Makefile`, `Kbuild`, `meson.build`, `meson_options.txt`, `Dockerfile` | `# SPDX-License-Identifier: BSD-3-Clause` |

The copyright line carries no year. A file you write from scratch may name you instead: `Copyright (c) <Name> and contributors.`

For example, in a `.c` file:

```C
// SPDX-License-Identifier: BSD-3-Clause
/*
 * ZXC - High-performance lossless compression
 *
 * Copyright (c) Bertrand Lebonnois and contributors.
 */
```

and in a `.h` file:

```C
/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * ZXC - High-performance lossless compression
 *
 * Copyright (c) Bertrand Lebonnois and contributors.
 */
```

The `SPDX Header Check` step of the quality workflow enforces the SPDX line. The vendored `src/lib/vendors/rapidhash.h` carries `MIT` instead, added by the vendors workflow on each update.

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
- **CMake** 3.21+
- Follow `.clang-format` style (Google)
- All code must be ASCII-only
- Pass `ctest` and static analysis

## Submitting Changes

1. Fork and create a short-lived topic branch from `main` (`feat/...`, `fix/...`, `perf/...`, `docs/...`)
2. Add tests for new functionality
3. Ensure CI passes (build, tests, benchmarks)
4. Sign your commits with `-s`
5. Open a PR to `main`

Always target `main`, including for bug fixes. Backporting a fix to a maintenance
branch is done by the maintainers after it has landed on `main` (see below).
Only open a PR against a `release/*` branch when a maintainer asks for it.

## Branches and Releases

### Branches

| Branch | Purpose |
|---|---|
| `main` | Default branch. All development lands here; it becomes the next minor release. |
| `release/X.Y` | Maintenance branch for the `X.Y` line, created from the `vX.Y.0` tag. Receives fixes only, never features. |
| `bump/vX.Y.Z` | Short-lived branch preparing a release (version bump, CHANGELOG). Merged by PR, then deleted. |
| `feat/*`, `fix/*`, `perf/*`, `docs/*`... | Short-lived topic branches, merged into `main` by PR. |

`main` and `release/*` are protected: CI must pass, history is never rewritten,
and tags are never moved or deleted. Downstream projects (ClickHouse, distribution
packages, forks) pin exact commits and tags, so every published commit must stay
reachable.

### Versioning and compatibility

ZXC follows [Semantic Versioning](https://semver.org/) for its public API and
CLI. From 1.0.0 onwards, the compressed format is stable within a major version:
data written by any `1.x` release must decompress with every later `1.x` release.

- **Patch** (`1.0.1`): bug fixes and security fixes only. No new API, no change
  to the compressed output that would affect existing readers.
- **Minor** (`1.1.0`): new features and API additions, backward compatible.
- **Major** (`2.0.0`): breaking changes to the API or the format. These are not
  merged into `main` until a 2.0 is planned.

### Where changes go

| Change | Lands on | Then |
|---|---|---|
| Feature | `main` | Released in the next minor version |
| Bug fix | `main` | Backported to the supported `release/X.Y` branch if it affects that line |
| Security fix | `main` (or privately, see [SECURITY.md](SECURITY.md)) | Backported, then released as a patch |
| Breaking change | Not merged until a 2.0 is planned | |

Fixes always land on `main` first, so that no fix is lost in a later release.
In the rare case where a fix cannot apply to `main` (the affected code no longer
exists there), it is made directly on the `release/X.Y` branch and the PR says why.

### Supported versions

Only the latest minor release line receives bug fixes. The previous line may
receive security fixes at the maintainers' discretion. See
[SECURITY.md](SECURITY.md) for the current list.

### Release process (maintainers)

**Minor or major release (`vX.Y.0`), from `main`:**

```bash
git switch -c bump/vX.Y.0 main
# bump versions, regenerate CHANGELOG, open a PR to main and merge it
git switch main && git pull
git tag -s vX.Y.0 -m "vX.Y.0"
git push origin vX.Y.0
git branch release/X.Y vX.Y.0
git push origin release/X.Y
```

Then publish the GitHub release from the tag.

**Patch release (`vX.Y.Z`), from `release/X.Y`:**

```bash
git switch -c bump/vX.Y.Z origin/release/X.Y
git cherry-pick -x -s <sha-on-main>...   # one per fix, in order
# bump versions, regenerate CHANGELOG, open a PR to release/X.Y and merge it
git switch release/X.Y && git pull
git tag -s vX.Y.Z -m "vX.Y.Z"
git push origin vX.Y.Z
```

`-x` records the original commit (`(cherry picked from commit ...)`), which keeps
backports traceable. When publishing the GitHub release for a patch on an older
line (that is, when a newer minor already exists), untick **Set as the latest
release**.

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
