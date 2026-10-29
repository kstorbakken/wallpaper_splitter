# Development environment

Wallpaper Splitter can be built in any environment that supplies a C++20
compiler, CMake 3.20 or newer, Qt 6, and KDE Frameworks 6 ConfigWidgets. The
committed devcontainer is a reproducible convenience environment, not a
requirement for contributing.

Tests also require Qt 6 Qml (`qt6-declarative` on Arch or
`qt6-declarative-dev` on Debian) to execute the Plasma wallpaper script in
QJSEngine with simulated desktops. The application itself does not require Qml.

The supported desktop target is KDE Plasma 6 on Wayland. For source installs,
include the runtime plugins that release packages already declare:

| Distribution | Wayland plugin | Additional image formats (including WebP) |
| --- | --- | --- |
| Debian / KDE neon | `qt6-wayland` | `qt6-image-formats-plugins` |
| Fedora | `qt6-qtwayland` | `qt6-qtimageformats` |
| Arch Linux | `qt6-wayland` | `qt6-imageformats` |

The shared workflow in `.github/workflows/build.yml` builds and tests Debian
Trixie, Fedora, Arch, and KDE neon for pull requests and tagged releases. Each
target validates the application metadata before packaging. KDE neon also checks
native Wayland startup with a headless Weston compositor; release builds run
that check against the AppImage and verify its WebP and Wayland shell plugins.
These checks do not replace integration testing in a real Plasma session.

Application sources compile into one internal static library shared by the GUI,
CLI, and regression suites. Building either test target also builds the CLI that
its tests execute. Only the application and desktop metadata are installed.

## Build from source

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

Run the built application directly from a terminal in your Plasma Wayland session:

```sh
./build/wallpaper_splitter
```

No package or installation is needed for local use. To install it system-wide:

```sh
sudo cmake --install build
```

Use `-DBUILD_TESTING=OFF` when you only need the application.
For end-user packages, see [Installation](docs/installation.md).

## Updating the screenshot

The README and AppStream metadata both use `assets/screenshot.png`. Keep that
path when replacing the image so both stay up to date. Capture the current
editor with an image loaded, readable monitor outlines, and all footer controls
visible. Use wallpaper artwork you have permission to redistribute and avoid
personal paths or other private information.

The current image is a capture of the application’s Qt widgets using the
offscreen platform, Fusion style, a dark color scheme, an example three-monitor
layout, and an original illustrated landscape. It shows the editor; it does not
demonstrate a live Plasma wallpaper application. A capture from a Plasma Wayland
session can replace it at the same path.

## Arch Linux devcontainer

The included `.devcontainer/` definition uses `archlinux:base-devel` with the
application's build, test, and metadata-validation tools. It reflects the
maintainer's Arch Linux/KDE workstation.

Open the repository in a Dev Containers-compatible editor and choose
**Reopen in Container**. The repository is mounted into the container by the
Dev Containers tooling, so build products written below the checkout remain
visible on the host.

Configure, build, and test with:

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

The container is intended for compilation, automated tests, and static
validation. It does not forward the host's Wayland display or KDE session D-Bus
connection. Launch `./build/wallpaper_splitter` from the mounted checkout on the
KDE host for GUI checks and applying wallpapers.

## Preparing a release

Create a `release/<version>` branch from `main`. Update the version in
`CMakeLists.txt` and `packaging/arch/metadata.sh`, reset `pkgrel` to `1`, add the
AppStream release entry in `assets/metainfo.xml`, and update versioned package
examples in [Installation](docs/installation.md). Release notes
should cover all changes since the previous tag.

Open a pull request from that branch. In addition to the normal build and test
matrix, CI creates Debian, RPM, Arch, and AppImage packages on `release/*` pull
requests. These artifacts are available from the CI run for any additional
package checks. Use the binary directly in a Plasma 6 Wayland session for normal
application testing.

After merging the release PR, tag its commit as `v<version>` and push the tag
when ready to publish. The tag triggers the Release packages workflow, which
rebuilds the packages and publishes them to a GitHub release after validation.
Release PR CI only uploads workflow artifacts. The Release packages workflow
can also be run manually on a branch to build artifacts without publishing.

## Matching another environment

A container does not need to use the same distribution as its host. The Arch
definition can be used from Linux, macOS, or Windows hosts supported by the
Dev Containers runtime. Keeping Arch is useful when the target is an Arch/KDE
installation.

To reproduce a different deployment environment, add another Dockerfile under
`.devcontainer/` (or replace the existing one), change the `build.dockerfile`
entry in `devcontainer.json`, and install that distribution's equivalents of:

- a C++20 compiler and standard build tools;
- CMake and Ninja;
- Qt 6 Core, D-Bus, GUI, Widgets, Test, and Qml development files;
- KDE Frameworks 6 ConfigWidgets and Extra CMake Modules.

Keep the CMake build and test commands the same. Release packages are assembled
by the shared GitHub Actions workflow, using CPack for Debian/RPM and separate
steps for Arch and AppImage.

If the committed editor settings, user name, or other ergonomic choices do not
suit your setup, adjust your local devcontainer copy or use a native build.
None of those choices affect the source or test suite.
