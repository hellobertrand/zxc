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
git verify-tag v0.14.0
```

The release key is in [`.github/release-key.asc`](release-key.asc); import it with
`gpg --import`. Fingerprint: `E943 9DC2 5979 8AB2 984C 9B87 F756 101E B877 1268`. This is a dedicated
release key.

### The files match the signed checksums

```sh
gpg --verify SHA256SUMS.txt.asc SHA256SUMS.txt   # signed offline, key never in CI
sha256sum -c SHA256SUMS.txt                      # run where the assets are
```

### Build provenance

```sh
gh attestation verify zxc-0.14.0-linux-x86_64.tar.gz --repo hellobertrand/zxc
```

Confirms the archive was produced by this repository's `Build & Release` workflow from the
commit the release points at. Requires `gh` 2.49 or newer. The source tarball, the `.tar.zxc`
and the SBOM are attested the same way.

### SLSA provenance

`multiple.intoto.jsonl` is a SLSA Build Level 3 attestation covering every asset at once:

```sh
slsa-verifier verify-artifact zxc-0.14.0-linux-x86_64.tar.gz \
  --provenance-path multiple.intoto.jsonl \
  --source-uri github.com/hellobertrand/zxc \
  --source-tag v0.14.0
```

### Source tarball

`zxc-<version>.tar.gz` is `git archive` through `gzip -n`, so you can regenerate it and
compare rather than trust it:

```sh
git archive --format=tar --prefix="zxc-0.14.0/" v0.14.0 | gzip -n -9 | sha256sum
```

The bytes are stable for a given git version; the version used is recorded in the release
run log. See [docs/RELEASE.md](../docs/RELEASE.md).

If any of these checks fails, the file did not come from this release pipeline: please do
not use it, and report it to the address above.
