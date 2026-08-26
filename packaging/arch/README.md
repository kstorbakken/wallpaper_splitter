# Local Arch test package

This directory contains the `makepkg` definition and helper used to build an
installable Arch Linux package from the current working tree. It is intended
for local integration testing, not as devcontainer configuration, and it
includes uncommitted source changes.

Run `./packaging/arch/build-package.sh` from anywhere in the checkout. The
helper creates an isolated temporary build, runs the test suite, and writes the
unsigned `.pkg.tar.zst` artifact to the ignored `packages/` directory.

The helper requires an Arch environment with the dependencies declared in
`PKGBUILD`; the repository's devcontainer supplies them. Official tagged
release packages are built separately by GitHub Actions.
