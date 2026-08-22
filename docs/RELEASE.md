# Release process

The workflow refuses a tag it cannot attribute to the release key, and publishes as a draft
so nothing is public before the checksums are signed.

## Cutting a release

1. Bump `ZXC_VERSION_*` in `include/zxc_constants.h`, update `CHANGELOG.md`, merge to
   `main`. CI compares the tag against this header and refuses a mismatch.

2. Tag and push.

   ```sh
   FPR=$(gpg --show-keys --with-colons .github/release-key.asc | awk -F: '/^fpr:/{print $10; exit}')
   git tag -s -u "$FPR" v0.14.0 -m "zxc 0.14.0"
   git push origin v0.14.0
   ```

3. Wait for the run. The result is a draft release.

4. Check, then sign. Signing without looking turns the one human attestation into a rubber
   stamp over whatever CI produced — a compromised runner or action would get your signature
   on its digests:

   ```sh
   gh release download v0.14.0     # every asset, into the current directory
   sha256sum -c SHA256SUMS         # macOS: shasum -a 256 -c
   gh attestation verify zxc-0.14.0-linux-x86_64.tar.gz --repo hellobertrand/zxc

   # The source tarball you can rebuild yourself, so do:
   git archive --format=tar --prefix="zxc-0.14.0/" v0.14.0 | gzip -n -9 | sha256sum
   grep 'zxc-0.14.0.tar.gz$' SHA256SUMS
   ```

   Only then sign and publish — the private key never reaches CI:

   ```sh
   gpg --detach-sign --armor SHA256SUMS          # covers every asset
   gpg --detach-sign --armor zxc-0.14.0.tar.gz   # what distro tooling looks for
   gpg --detach-sign --armor zxc-0.14.0.tar.zxc
   gh release upload v0.14.0 SHA256SUMS.asc \
     zxc-0.14.0.tar.gz.asc zxc-0.14.0.tar.zxc.asc
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

So publishing is the point of no return, and not only for Go. Before it,
`gh release delete v0.14.0 --cleanup-tag --yes` undoes everything and the release was never
public — which makes a real tag a safe rehearsal of the whole pipeline. After it, a mistake
costs a version number on four registries.

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

## Creating or rotating the release key

Needed once at the start, and again whenever the key expires or is replaced. It is a
dedicated key, separate from the commit-signing key: it stays offline and is unlocked a few
times a year.

```sh
gpg --quick-generate-key "ZXC Release Signing Key <zxc.codec@gmail.com>" rsa4096 sign 2y
gpg --output zxc-release-revoke.asc --gen-revoke <fingerprint>   # before first use
gpg --keyserver keys.openpgp.org --send-keys <fingerprint>
gpg --armor --export <fingerprint> >> .github/release-key.asc   # append, never overwrite
```

Appending matters: the file is the only key source users are pointed at, so dropping the old
key would make every tag already signed with it unverifiable. Retire a key by revoking it,
not by deleting it from here.

Then update the fingerprint in [SECURITY.md](../.github/SECURITY.md) and the repository
variable `RELEASE_KEY_FPR`, which is what CI pins the tag signature against — it lives in
the repo settings rather than the tree, since the tree comes from the tag being verified.

Back the key up, keep the revocation certificate somewhere else, and use a passphrase of six
random words — anyone holding the key file attacks it offline, with no rate limit.
