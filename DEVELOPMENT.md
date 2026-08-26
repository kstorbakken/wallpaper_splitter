# Development environment

Wallpaper Splitter can be built in any environment that supplies a C++20
compiler, CMake 3.20 or newer, Qt 6, and KDE Frameworks 6 ConfigWidgets. The
committed devcontainer is a reproducible convenience environment, not a
requirement for contributing.

## Arch Linux devcontainer

The included `.devcontainer/` definition uses `archlinux:base-devel` with the
application's build, test, metadata-validation, and pacman-packaging tools. It
reflects the maintainer's Arch Linux/KDE workstation and makes it possible to
build a package that can be installed on that host for integration testing.

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
validation. It does not forward the host's Wayland/X11 display or KDE session
D-Bus connection, so GUI behavior and applying a wallpaper should be tested on
a KDE host.

## Building an Arch test package

The files in `packaging/arch/` build the current checkout, including
uncommitted changes, with `makepkg`. Tests run before an unsigned package is
written to the ignored `packages/` directory:

```sh
./packaging/arch/build-package.sh
```

On an Arch Linux host, install or upgrade the printed artifact with:

```sh
sudo pacman -U packages/wallpaper_splitter-1.3.0-2-x86_64.pkg.tar.zst
```

This local test-package workflow is separate from the packages created for
tagged releases by GitHub Actions.

## Matching another environment

A container does not need to use the same distribution as its host. The Arch
definition can be used from Linux, macOS, or Windows hosts supported by the
Dev Containers runtime. Keeping Arch is useful when the target is an Arch
package or an Arch/KDE installation.

To reproduce a different deployment environment, add another Dockerfile under
`.devcontainer/` (or replace the existing one), change the `build.dockerfile`
entry in `devcontainer.json`, and install that distribution's equivalents of:

- a C++20 compiler and standard build tools;
- CMake and Ninja;
- Qt 6 Core, D-Bus, GUI, Widgets, and Test development files;
- KDE Frameworks 6 ConfigWidgets and Extra CMake Modules.

Keep the CMake build and test commands the same. Package creation is
distribution-specific: `packaging/arch/build-package.sh` requires Arch's
`makepkg`, while the CPack configuration and release workflow provide the
starting points for Debian, RPM, and AppImage builds.

If the committed editor settings, user name, or other ergonomic choices do not
suit your setup, adjust your local devcontainer copy or use a native build.
None of those choices affect the source or test suite.
