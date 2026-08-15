#!/usr/bin/env sh
# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0

set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$root"

build_dir="$root/build/release-strict"
package_dir="$root/_packages"
rm -rf "$package_dir"
mkdir -p "$package_dir"

cmake --preset release-strict
cmake --build --preset release-strict
ctest --preset release-strict

version=$("$build_dir/forge" version | awk '{print $2}')
if [ "$version" != "2.0.0" ]; then
  printf '%s
' "FORGE  expected version 2.0.0, got $version" >&2
  exit 1
fi

cpack --config "$build_dir/CPackConfig.cmake" -B "$package_dir"
cpack --config "$build_dir/CPackSourceConfig.cmake" -B "$package_dir"

if command -v sha256sum >/dev/null 2>&1; then
  (cd "$package_dir" && sha256sum forge-2.0.0-* > SHA256SUMS)
elif command -v shasum >/dev/null 2>&1; then
  (cd "$package_dir" && shasum -a 256 forge-2.0.0-* > SHA256SUMS)
else
  printf '%s
' 'FORGE  no SHA-256 tool found' >&2
  exit 1
fi

printf '%s
' 'FORGE  release gate passed'
printf '%s
' "FORGE  packages: $package_dir"
