#!/usr/bin/env bash

set -euo pipefail

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
project_dir="$(cd -- "${script_dir}/../.." && pwd)"
package_dir="${1:-${project_dir}/packages}"

project_version="$(sed -nE \
    's/^project\(wallpaper_splitter VERSION ([^ ]+) .*/\1/p' \
    "${project_dir}/CMakeLists.txt")"
package_version="$(sed -nE 's/^pkgver=(.*)$/\1/p' "${script_dir}/PKGBUILD")"

if [[ -z "${project_version}" || "${project_version}" != "${package_version}" ]]; then
    printf 'CMake version (%s) does not match PKGBUILD version (%s).\n' \
        "${project_version:-missing}" "${package_version:-missing}" >&2
    exit 1
fi

build_dir="$(mktemp -d --tmpdir wallpaper-splitter-package.XXXXXXXX)"

cleanup() {
    rm -rf -- "${build_dir}"
}
trap cleanup EXIT

mkdir -p -- "${package_dir}"
package_dir="$(cd -- "${package_dir}" && pwd)"

export BUILDDIR="${build_dir}/build"
export PKGDEST="${package_dir}"
export SRCDEST="${build_dir}/sources"
export SRCPKGDEST="${package_dir}"
export LOGDEST="${build_dir}/logs"

cd -- "${script_dir}"
mapfile -t built_packages < <(makepkg --packagelist)
makepkg --clean --cleanbuild --force --check --noconfirm

printf 'Arch package written to:\n'
printf '%s\n' "${built_packages[@]}"
