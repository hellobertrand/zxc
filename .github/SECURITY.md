# Security Policy

## Reporting a Vulnerability

**Please do not report security vulnerabilities through public GitHub issues.**

If you have discovered a security issue in ZXC, please email us privately at **zxc.codec@gmail.com**.

We will respond within 48 hours.

## Verifying a release

Every release ships a `sha256sums` manifest signed with minisign, and each archive carries a
GitHub build attestation. See [the README](../README.md#installation) for the two commands.

Release tags are signed with the maintainer's PGP key
`CFC1 8791 C7DB A33B A5B8  5506 C6A2 4372 A165 64D6`, published at
<https://github.com/hellobertrand.gpg>.

If a check fails, please do not use the file and report it to the address above.
