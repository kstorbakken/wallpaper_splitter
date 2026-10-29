# Installation

Wallpaper Splitter targets **KDE Plasma 6 on Wayland**. Prebuilt packages are for
**x86-64** Linux systems.

## Arch Linux (AUR)

Install [wallpaper_splitter from the AUR](https://aur.archlinux.org/packages/wallpaper_splitter)
using an AUR helper such as `yay`:

```sh
yay -S wallpaper_splitter
```

## Install a release package

Open the [latest GitHub release](https://github.com/kstorbakken/wallpaper_splitter/releases/latest)
and expand **Assets** to download the package for your distribution. The prebuilt
packages are for **x86-64** PCs (`amd64` in the Debian filename). The source code
archives are for building the application yourself.

In a terminal, change to the folder containing your download, then use the
appropriate command below. These examples use version **1.5.0**; replace the
filename with the one you downloaded.

| Distribution | Download | Install command |
| --- | --- | --- |
| Debian / compatible Ubuntu-based systems | `.deb` | `sudo apt install ./wallpaper-splitter_1.5.0_amd64.deb` |
| Fedora | `.rpm` | `sudo dnf install ./wallpaper-splitter-1.5.0-1.x86_64.rpm` |
| Arch Linux (prebuilt alternative to AUR) | `.pkg.tar.zst` | `sudo pacman -U ./wallpaper_splitter-1.5.0-1-x86_64.pkg.tar.zst` |

These package managers also install required dependencies from your configured
repositories. The `.deb` is built on **Debian 13 (Trixie)** and the `.rpm` on
Fedora; your system must provide compatible Qt 6, KDE Frameworks 6, and runtime
libraries. Older Debian/Ubuntu releases or other RPM-based distributions may not
satisfy those dependencies. If installation reports incompatible dependencies,
try the AppImage below or build from source using the
[development guide](../DEVELOPMENT.md#build-from-source).

After installing a native package, launch **Wallpaper Splitter** from your
application menu or run `wallpaper_splitter` in a terminal. Applying wallpapers
directly requires a KDE Plasma session.

## AppImage (other distributions)

Download the `.AppImage` from the same release, then make it executable and run
it from the download folder (replace the filename for newer releases):

```sh
chmod +x wallpaper-splitter-1.5.0-x86_64.AppImage
./wallpaper-splitter-1.5.0-x86_64.AppImage
```

The AppImage bundles Qt and KDE libraries and runs without a system-wide package
installation. It is built on an Ubuntu 24.04-based KDE neon environment, so it
still requires a compatible x86-64 Linux system; older distributions may not run
it. Keep the file somewhere permanent if you plan to use it regularly. To update,
download a newer AppImage and run that file instead.

If it reports that FUSE is unavailable, try running it with extraction enabled:

```sh
APPIMAGE_EXTRACT_AND_RUN=1 ./wallpaper-splitter-1.5.0-x86_64.AppImage
```

Continue with the [quick start](../README.md#quick-start) or [user guide](usage.md).
