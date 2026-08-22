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
curl -sS https://github.com/hellobertrand.gpg | gpg --import
git fetch --tags
git verify-tag v<version>
```

Releases up to 0.13.3 carry a lightweight tag, which holds no signature of its own. Verify
the commit it points at instead:

```sh
git verify-commit v<version>
```

The keys come straight from GitHub — the same ones behind the "Verified" badge on the
commits. Signing key: `CFC1 8791 C7DB A33B A5B8  5506 C6A2 4372 A165 64D6`.

The tag is the one thing a person signs; everything else is attested by the build itself,
below.

### The files match the checksums

```sh
sha256sum -c sha256sums --ignore-missing   # only the assets you downloaded
```

`sha256sums` is a convenience for checking a download in one pass; what actually attests
the files is the build provenance below.

### Build provenance

```sh
gh attestation verify zxc-<version>-linux-x86_64.tar.gz --repo hellobertrand/zxc
```

Confirms the archive was produced by this repository's `Build & Release` workflow from the
commit the release points at. Requires `gh` 2.49 or newer. The source tarball, the `.tar.zxc`
and the SBOM are attested the same way.

### SLSA provenance

`multiple.intoto.jsonl` is a SLSA Build Level 3 attestation covering the archives, the
source tarballs and the SBOM:

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
