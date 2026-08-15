#!/usr/bin/env sh
# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0

set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$root"
cmake --preset asan-ubsan
cmake --build --preset asan-ubsan
ctest --preset asan-ubsan
printf '%s\n' 'FORGE  sanitizer gate passed'
