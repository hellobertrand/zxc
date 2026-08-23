# Security Policy

## Reporting a Vulnerability

**Please do not report security vulnerabilities through public GitHub issues.**

If you have discovered a security issue in ZXC, please email us privately at **zxc.codec@gmail.com**.

We will respond within 48 hours.

## Verifying a release

Four independent checks, from the strongest claim to the most convenient. The first is the
only one that attests a **human** decision; the others attest the build machinery.

### The tag was signed by the maintainer

```sh
git fetch --tags
git verify-tag v<version>
```

The key is in [`.github/release-key.asc`](release-key.asc); import it with `gpg --import`.
Fingerprint: `CFC1 8791 C7DB A33B A5B8  5506 C6A2 4372 A165 64D6` — the same key signs the
tags and, through a subkey held by CI, the release artifacts.

### The files match the signed checksums

```sh
gpg --verify SHA256SUMS.asc SHA256SUMS   # signed by the release subkey held by CI
sha256sum -c SHA256SUMS --ignore-missing   # only the assets you downloaded
```

The source tarballs also carry their own detached signature, for packaging tools that
expect one beside the file:

```sh
gpg --verify zxc-<version>.tar.gz.asc zxc-<version>.tar.gz
```

### Build provenance

```sh
gh attestation verify zxc-<version>-linux-x86_64.tar.gz --repo hellobertrand/zxc
```

Confirms the archive was produced by this repository's `Build & Release` workflow from the
commit the release points at. Requires `gh` 2.49 or newer. The source tarball, the `.tar.zxc`
and the SBOM are attested the same way.

### SLSA provenance

`multiple.intoto.jsonl` is a SLSA Build Level 3 attestation covering the archives, the
source tarballs and the SBOM. `SHA256SUMS` is not among them — it is covered by the PGP
signature above instead:

```sh
slsa-verifier verify-artifact zxc-<version>-linux-x86_64.tar.gz \
  --provenance-path multiple.intoto.jsonl \
  --source-uri github.com/hellobertrand/zxc \
  --source-tag v<version>
```

### Source tarball

`zxc-<version>.tar.gz` is `git archive` through `gzip -n`, so you can regenerate it and
compare rather than trust it:

```sh
git archive --format=tar --prefix="zxc-<version>/" v<version> | gzip -n -9 | sha256sum
```

The bytes are stable for a given git version; the version used is recorded in the release
run log.

If any of these checks fails, the file did not come from this release pipeline: please do
not use it, and report it to the address above.
