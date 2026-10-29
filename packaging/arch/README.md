# Arch release package

`metadata.sh` supplies the name, version, license, and runtime dependencies for
the pacman archive assembled by the shared GitHub Actions build workflow.

The workflow stages the installed application, writes `.PKGINFO` using this
metadata, and creates the release `.pkg.tar.zst`. Keep `pkgver` aligned with
`CMakeLists.txt` and reset `pkgrel` to `1` for a new release.

The [AUR package](https://aur.archlinux.org/packages/wallpaper_splitter) has a
separate recipe maintained in its AUR repository.
