# Security Policy

## Supported Versions

Only the latest minor release line receives bug fixes. The previous line may
receive security fixes at the maintainers' discretion. Fixes reach older lines
as patch releases from their `release/X.Y` branch (see
[CONTRIBUTING.md](CONTRIBUTING.md#branches-and-releases)).

| Version | Status |
|---|---|
| 1.0.x | Bug and security fixes |
| 0.15.x | Security fixes at the maintainers' discretion |
| < 0.15 | Not supported |

This table is updated with each minor release.

## Reporting a Vulnerability

**Please do not report security vulnerabilities through public GitHub issues.**

If you have discovered a security issue in ZXC, please email us privately at **zxc.codec@gmail.com**.

We will respond within 48 hours.

## Verifying a release

Every release ships a `checksums.sha256` manifest signed with minisign, and each archive carries a
GitHub build attestation. See [docs/INSTALL.md](../docs/INSTALL.md#release-archives) for the commands.

Release tags are signed with the maintainer's PGP key
`CFC1 8791 C7DB A33B A5B8  5506 C6A2 4372 A165 64D6`, published at
<https://github.com/hellobertrand.gpg>.

If a check fails, please do not use the file and report it to the address above.
