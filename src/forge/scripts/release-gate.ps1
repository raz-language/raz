# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent $PSScriptRoot
$BuildDir = Join-Path $Root "build/release-strict"
$PackageDir = Join-Path $Root "_packages"

Push-Location $Root
try {
    if (Test-Path $PackageDir) { Remove-Item -Recurse -Force $PackageDir }
    New-Item -ItemType Directory -Path $PackageDir | Out-Null

    cmake --preset release-strict
    cmake --build --preset release-strict
    ctest --preset release-strict

    $ForgeExe = Join-Path $BuildDir "forge.exe"
    $Version = (& $ForgeExe version).Trim()
    if ($Version -ne "forge 2.0.0") {
        throw "Expected forge 2.0.0, got '$Version'"
    }

    cpack --config (Join-Path $BuildDir "CPackConfig.cmake") -B $PackageDir
    cpack --config (Join-Path $BuildDir "CPackSourceConfig.cmake") -B $PackageDir

    $ChecksumPath = Join-Path $PackageDir "SHA256SUMS"
    Get-ChildItem $PackageDir -File |
        Where-Object { $_.Name -like "forge-2.0.0-*" } |
        Sort-Object Name |
        ForEach-Object {
            $Hash = (Get-FileHash $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
            "$Hash  $($_.Name)"
        } | Set-Content -Encoding ascii $ChecksumPath

    Write-Host "FORGE  release gate passed"
    Write-Host "FORGE  packages: $PackageDir"
} finally {
    Pop-Location
}
