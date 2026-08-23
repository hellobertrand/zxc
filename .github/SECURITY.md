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
  --certificate-identity-regexp '^https://github\.com/hellobertrand/zxc/\.github/workflows/build\.yml@refs/tags/' \
  --certificate-oidc-issuer https://token.actions.githubusercontent.com \
  zxc-0.14.0.tar.gz
```

Every asset built by the release job has a `.sigstore.json` bundle beside it, `SHA256SUMS`
included; the bundle is self-contained, so this works without querying GitHub.
`multiple.intoto.jsonl` is the exception — it is attached afterwards and carries its own
proof, see below.

### The files match the checksums

```sh
sha256sum -c SHA256SUMS         # macOS: shasum -a 256 -c
```

`SHA256SUMS` is only worth as much as its own signature: verify it with the cosign command
in the previous section before trusting anything it lists.

### Build provenance

The signature above says the file came from this workflow; provenance additionally binds it
to the commit it was built from. The quick way, if you have `gh` 2.49 or newer:

```sh
gh attestation verify zxc-0.14.0-linux-x86_64.tar.gz --repo hellobertrand/zxc
```

Or from `multiple.intoto.jsonl`, the SLSA Build Level 3 attestation shipped with the
release, which covers the archives, the source tarballs and the SBOM:

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
