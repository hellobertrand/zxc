# Security Policy

## Reporting a Vulnerability

**Please do not report security vulnerabilities through public GitHub issues.**

If you have discovered a security issue in ZXC, please email us privately at **zxc.codec@gmail.com**.

We will respond within 48 hours.

## Verifying a release

Every asset is signed with [Sigstore](https://www.sigstore.dev/) — keyless, so there is no
key to fetch or trust: the signature is bound to the workflow identity that produced it, and
recorded in a public transparency log.

### The signature on any asset

```sh
cosign verify-blob --bundle zxc-0.14.0.tar.gz.sigstore.json \
  --certificate-identity-regexp '^https://github.com/hellobertrand/zxc/' \
  --certificate-oidc-issuer https://token.actions.githubusercontent.com \
  zxc-0.14.0.tar.gz
```

Each asset has a `.sigstore.json` bundle beside it, `SHA256SUMS` included. The bundle is
self-contained, so this works without querying GitHub.

### The files match the checksums

```sh
sha256sum -c SHA256SUMS         # run where the assets are;
```

Verify `SHA256SUMS` itself with the command above before trusting it.

### Build provenance

```sh
gh attestation verify zxc-0.14.0-linux-x86_64.tar.gz --repo hellobertrand/zxc
```

Confirms the archive was produced by this repository's `Build & Release` workflow from the
commit the release points at. Requires `gh` 2.49 or newer.

### SLSA provenance

`multiple.intoto.jsonl` is a SLSA Build Level 3 attestation covering the archives, the
source tarballs and the SBOM:

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
