# Wallpaper Splitter

[![CI](https://github.com/kstorbakken/wallpaper_splitter/actions/workflows/ci.yml/badge.svg)](https://github.com/kstorbakken/wallpaper_splitter/actions/workflows/ci.yml)

Span one wallpaper across your monitors on **KDE Plasma 6 on Wayland**.
Wallpaper Splitter fits an image to your screen layout, cuts it into per-monitor
crops, and applies them together. You can also export the crops for your own use.

<p align="center">
  <img src="assets/screenshot.png" alt="Wallpaper Splitter editor showing one landscape across three monitor outlines, with Open, Library, Settings, Preview, Export, and Apply controls." width="900">
</p>

## What you can do

- Fit a continuous panorama, stretch to fill, or adjust the image by hand.
- Align displays with different pixel densities, heights, and bezel gaps.
- Preview on the desktop with automatic restoration of your previous wallpaper.
- Save wallpaper sets in a library and rotate them while the app is closed.
- Split a folder of images, or export and apply from the command line.

Supports still images, including JPEG, PNG, and WebP.

## Installation

Download a package from the [latest release](https://github.com/kstorbakken/wallpaper_splitter/releases/latest).
Prebuilt packages target **x86-64** Linux systems with Plasma 6 on Wayland.

| System | Install option |
| --- | --- |
| Arch Linux | [AUR](https://aur.archlinux.org/packages/wallpaper_splitter): `yay -S wallpaper_splitter`, or a prebuilt `.pkg.tar.zst` |
| Debian 13 / compatible Ubuntu-based systems | `.deb` |
| Fedora | `.rpm` |
| Other compatible distributions | `.AppImage` (Ubuntu 24.04-based build) |

See the [installation guide](docs/installation.md) for commands, compatibility,
AppImage setup, and updates. To build from source, use the
[development guide](DEVELOPMENT.md#build-from-source).

## Quick start

1. Launch **Wallpaper Splitter** and click **Open**, or drop an image into the window.
2. Use **Fill panorama** in the preview’s upper-right corner to fill the monitor
   layout while preserving proportions. Drag the image’s corner handles to resize
   it manually; hold **Shift** to preserve its aspect ratio.
3. Click **Preview** to try it on the desktop. The previous wallpaper returns
   when the countdown ends or you click **End Preview**.
4. Click **Apply** to save a library set and use it on the current Plasma activity,
   or **Export** to save crop files in a folder you choose.

Open **Library → Slideshow…** to rotate saved sets automatically. For displays with
different pixel densities or physical gaps, start with **Settings → Monitors**.

## Guides

| Guide | Covers |
| --- | --- |
| [Using the editor](docs/usage.md) | Image controls, batch splitting, previews, exports, and the library |
| [Slideshow](docs/slideshow.md) | Playlists, scheduling, AppImage updates, and troubleshooting |
| [Monitor alignment](docs/monitors.md) | Physical sizes, different resolutions, bezel gaps, and height offsets |
| [Command line](docs/command-line.md) | Exporting, applying, automatic layouts, and advancing a slideshow |
| [Development](DEVELOPMENT.md) | Building, tests, dependencies, and the devcontainer |

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md) for the development workflow and
[ROADMAP.md](ROADMAP.md) for planned work. See the
[editor controls](docs/usage.md#open-adjust-and-apply) for the known limitation
with diagonal resizing of the monitor overlay.

## About

This is an independently maintained fork of
[l0drex’s Wallpaper Splitter](https://github.com/l0drex/wallpaper_splitter).
It is not an official continuation of or endorsed by the original project.

Licensed under the [GNU GPL v3](LICENSE). See [AUTHORS.md](AUTHORS.md) for
authorship and maintenance history.
