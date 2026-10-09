#!/bin/sh
# SPDX-License-Identifier: BSD-3-Clause
# Release archive docs: LICENSE.txt, MANUAL.md and README.md into $2.
# $1 = version, stdin = the archive's contents list; GITHUB_REPOSITORY names the repo.
set -eu
contents=$(cat)
cp LICENSE "$2/LICENSE.txt"
cp docs/man/zxc.1.md "$2/MANUAL.md"
cat > "$2/README.md" <<README
# ZXC $1

Asymmetric Lossless Compression Built for Ultra-Fast Decode

**Contents:**

$contents

**CLI usage:** See [MANUAL.md](MANUAL.md) for the full reference (synopsis, options, examples).

**Release history:** https://github.com/${GITHUB_REPOSITORY}/releases

**Documentation, benchmarks & source:** https://github.com/${GITHUB_REPOSITORY}

**Verify build provenance (SLSA):** every archive is signed via [GitHub Artifact Attestations](https://docs.github.com/en/actions/security-guides/using-artifact-attestations-to-establish-provenance-for-builds). Verify with:
\`\`\`
gh attestation verify <archive> --repo ${GITHUB_REPOSITORY}
\`\`\`

License: BSD-3-Clause (see LICENSE.txt)
README
