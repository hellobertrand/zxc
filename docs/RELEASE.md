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

4. Sign the checksums offline — the private key must never reach CI:

   ```sh
   gh release download v0.14.0 -p SHA256SUMS.txt
   gpg --detach-sign --armor SHA256SUMS.txt
   gh release upload v0.14.0 SHA256SUMS.txt.asc
   gh release edit v0.14.0 --draft=false
   ```

Publishing also fires `wrapper-go.yml`, whose `tag-module` job tags the Go submodule
`wrappers/go/v0.14.0` on the release commit once the Go tests pass — Go resolves a
subdirectory module only through that form. Nothing to do by hand; check it with:

```sh
go list -m github.com/hellobertrand/zxc/wrappers/go@v0.14.0
```

Before publishing, `gh release delete v0.14.0 --cleanup-tag --yes` undoes everything and the
release was never public, which makes a real tag a safe rehearsal. Publishing is the point
of no return: the Go module version is then out, and the proxy caches it permanently.

If the provenance job cannot attach `multiple.intoto.jsonl` to a draft, set `draft: false`
on the `release` job and upload the signature right after publishing instead.

## Creating or rotating the release key

Needed once at the start, and again whenever the key expires or is replaced. It is a
dedicated key, separate from the commit-signing key: it stays offline and is unlocked a few
times a year.

```sh
gpg --quick-generate-key "ZXC Release Signing Key <zxc.codec@gmail.com>" rsa4096 sign 2y
gpg --output zxc-release-revoke.asc --gen-revoke <fingerprint>   # before first use
gpg --keyserver keys.openpgp.org --send-keys <fingerprint>
gpg --armor --export <fingerprint> > .github/release-key.asc
```

Then update the fingerprint in [SECURITY.md](../.github/SECURITY.md). Back the key up, keep
the revocation certificate somewhere else, and use a passphrase of six random words — anyone
holding the key file attacks it offline, with no rate limit.
