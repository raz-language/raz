# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent $PSScriptRoot
Push-Location $Root
try {
    cmake --preset asan-ubsan
    cmake --build --preset asan-ubsan
    ctest --preset asan-ubsan
    Write-Host "FORGE  sanitizer gate passed"
} finally {
    Pop-Location
}
