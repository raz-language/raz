# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0

[CmdletBinding()]
param([switch]$Clean)
$ErrorActionPreference='Stop'
$root=(Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$args=@(); if($Clean){$args += '-Clean'}
& (Join-Path $root 'scripts\bootstrap.ps1') @args
if($LASTEXITCODE -ne 0){throw "all-stage build failed: $LASTEXITCODE"}
& python (Join-Path $root 'scripts\check-native-boundary.py') | Out-Host
& python (Join-Path $root 'scripts\check-selfhost-runtime-declarations.py') | Out-Host
if($LASTEXITCODE -ne 0){throw 'native boundary audit failed'}

# C3 project graph qualification: the production Stage-2 frontend must own
# manifest parsing, recursive source discovery, dependencies, and entry ordering.
Write-Host "C3 project graph qualification" -ForegroundColor Cyan
$c3 = Join-Path $root 'build\c3-release-project'
if (Test-Path -LiteralPath $c3) { Remove-Item -LiteralPath $c3 -Recurse -Force }
$dep = Join-Path $c3 'dep'; $app = Join-Path $c3 'app'
New-Item -ItemType Directory -Path (Join-Path $dep 'src\nested') -Force | Out-Null
New-Item -ItemType Directory -Path (Join-Path $app 'src\sub') -Force | Out-Null
@'
[package]
name = "dep"
source = "src"
[dependencies]
'@ | Set-Content -LiteralPath (Join-Path $dep 'raz.toml') -Encoding ASCII
'fn dependency_value() -> i64 { return 40; }' | Set-Content -LiteralPath (Join-Path $dep 'src\nested\math.rz') -Encoding ASCII
@'
[package]
name = "app"
source = "src"
entry = "src/main.rz"
[dependencies]
dep = "../dep"
'@ | Set-Content -LiteralPath (Join-Path $app 'raz.toml') -Encoding ASCII
'fn local_value() -> i64 { return 2; }' | Set-Content -LiteralPath (Join-Path $app 'src\sub\helper.rz') -Encoding ASCII
'fn main() -> i64 { return dependency_value() + local_value(); }' | Set-Content -LiteralPath (Join-Path $app 'src\main.rz') -Encoding ASCII
$c3exe = Join-Path $c3 'app.exe'
& (Join-Path $root 'scripts\build-project.ps1') -Project $app -Output $c3exe -Clean
if($LASTEXITCODE -ne 0){throw "C3 self-host project build failed: $LASTEXITCODE"}
& $c3exe
if($LASTEXITCODE -ne 42){throw "C3 self-host project returned $LASTEXITCODE instead of 42"}
Write-Host "C3 project graph qualification: PASS" -ForegroundColor Green
$prod=Join-Path $root 'build\production'; New-Item -ItemType Directory -Path $prod -Force | Out-Null
Copy-Item (Join-Path $root 'build\windows-all-stages\stage2\raz-stage2.exe') (Join-Path $prod 'razc.exe') -Force
Copy-Item (Join-Path $root 'build\release\src\runtime\native\raz_runtime.lib') (Join-Path $prod 'raz_runtime.lib') -Force
$summary=@"
Raz production compiler
========================
Frontend: self-hosted Stage 2 (razc.exe)
Backend : C++ Forge 2.0 (linked in-process)
Runtime : raz_runtime.lib
External Forge codegen executable: none
Native ABI boundary: audited by scripts/check-native-boundary.py
Stage 3/4 are verification artifacts only.
"@
Set-Content -LiteralPath (Join-Path $prod 'README.txt') -Value $summary -Encoding ASCII
Write-Host "Production self-hosted toolchain: $prod" -ForegroundColor Green
