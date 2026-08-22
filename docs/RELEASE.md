# Release process

Releases are built, signed with Sigstore, and published as a draft — so nothing reaches
PyPI, npm or crates.io before you have looked at it.

## Cutting a release

1. Bump `ZXC_VERSION_*` in `include/zxc_constants.h`, update `CHANGELOG.md`, merge to
   `main`. CI compares the tag against this header and refuses a mismatch.

2. Tag and push:

   ```sh
   git tag -a v0.14.0 -m "zxc 0.14.0"
   git push origin v0.14.0
   ```

3. Wait for the run. Every asset gets a `.sigstore.json` bundle, and the result is a draft
   release.

4. Check it, then publish:

   ```sh
   gh release download v0.14.0     # every asset, into the current directory
   sha256sum -c SHA256SUMS         # macOS: shasum -a 256 -c

   cosign verify-blob --bundle zxc-0.14.0.tar.gz.sigstore.json \
     --certificate-identity-regexp '^https://github.com/hellobertrand/zxc/' \
     --certificate-oidc-issuer https://token.actions.githubusercontent.com \
     zxc-0.14.0.tar.gz

   # The source tarball you can rebuild yourself, so do:
   git archive --format=tar --prefix="zxc-0.14.0/" v0.14.0 | gzip -n -9 | sha256sum
   grep 'zxc-0.14.0.tar.gz$' SHA256SUMS

   gh release edit v0.14.0 --draft=false
   ```

### What publishing sets off

That last command fires five workflows, four of which push to registries where a version
number can never be reused:

| Workflow | Effect |
|---|---|
| `wrapper-python.yml` | publishes to PyPI |
| `wrapper-nodejs.yml` | publishes to npm |
| `wrapper-wasm.yml` | publishes to npm |
| `wrapper-rust.yml` | publishes to crates.io |
| `wrapper-go.yml` | tags `wrappers/go/v0.14.0`, which the Go proxy caches permanently |

So publishing is the point of no return. Before it,
`gh release delete v0.14.0 --cleanup-tag --yes` undoes everything and the release was never
public — which makes a real tag a safe rehearsal of the whole pipeline.

Check the Go module landed:

```sh
go list -m github.com/hellobertrand/zxc/wrappers/go@v0.14.0
```

If `attach-provenance` fails to find the draft, the release is still a draft and nothing is
public — attach the file by hand before step 4:

```sh
gh run download <run-id>            # the attestation is among the run artifacts
gh release upload v0.14.0 multiple.intoto.jsonl --clobber
```
