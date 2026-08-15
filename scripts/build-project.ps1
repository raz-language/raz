# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0

[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][string]$Project,
    [string]$Output = '',
    [switch]$Clean
)
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$production = Join-Path $root 'build\production'
$stage2 = Join-Path $production 'razc.exe'
$runtime = Join-Path $production 'raz_runtime.lib'
if (-not (Test-Path -LiteralPath $stage2 -PathType Leaf)) { $stage2 = Join-Path $root 'build\windows-all-stages\stage2\raz-stage2.exe' }
if (-not (Test-Path -LiteralPath $runtime -PathType Leaf)) { $runtime = Join-Path $root 'build\release\src\runtime\native\raz_runtime.lib' }
foreach ($item in @($stage2,$runtime)) { if (-not (Test-Path -LiteralPath $item -PathType Leaf)) { throw "Required self-host artifact missing: $item. Run bootstrap.bat first." } }
$projectPath = (Resolve-Path -LiteralPath $Project).Path
if (Test-Path -LiteralPath $projectPath -PathType Container) { $manifest = Join-Path $projectPath 'raz.toml' } else { $manifest = $projectPath; $projectPath = Split-Path -Parent $manifest }
if (-not (Test-Path -LiteralPath $manifest -PathType Leaf)) { throw "raz.toml not found: $manifest" }
if (-not $Output) { $Output = Join-Path $projectPath 'build\selfhost\app.exe' }
$Output = [IO.Path]::GetFullPath($Output)
$work = Join-Path $projectPath 'build\selfhost'
if ($Clean -and (Test-Path $work)) { Remove-Item $work -Recurse -Force }
New-Item -ItemType Directory -Path $work -Force | Out-Null
$fir = Join-Path $work 'package.fir'; $obj = Join-Path $work 'package.obj'
Write-Host "[RAZ] compiler -> Forge IR + native object (in process)" -ForegroundColor Cyan
& $stage2 --forge-native $manifest $fir | Out-Host
if ($LASTEXITCODE -ne 0) { throw "Raz compiler/Forge backend failed with exit code $LASTEXITCODE" }
if (-not (Test-Path -LiteralPath $obj -PathType Leaf)) { throw "In-process Forge backend did not emit object: $obj" }
$clang = 'C:\Program Files\LLVM\bin\clang-cl.exe'
if (-not (Test-Path $clang)) { $cmd = Get-Command clang-cl.exe -ErrorAction SilentlyContinue; if ($cmd) { $clang = $cmd.Source } }
if (-not (Test-Path $clang)) { throw 'clang-cl.exe was not found.' }
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$devcmd = ''
if (Test-Path $vswhere) { $install = (& $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath | Select-Object -First 1); if ($install) { $devcmd = Join-Path $install 'Common7\Tools\VsDevCmd.bat' } }
if (-not $devcmd -or -not (Test-Path $devcmd)) { throw 'Visual Studio C++ Build Tools were not found.' }
New-Item -ItemType Directory -Path (Split-Path -Parent $Output) -Force | Out-Null
$command = '"' + $devcmd + '" -no_logo -arch=x64 -host_arch=x64 >nul && "' + $clang + '" /nologo "' + $obj + '" "' + $runtime + '" ws2_32.lib bcrypt.lib /Fe:"' + $Output + '"'
& $env:ComSpec /d /s /c $command | Out-Host
if ($LASTEXITCODE -ne 0) { throw "Native link failed with exit code $LASTEXITCODE" }
Write-Host "Built with Raz: $Output" -ForegroundColor Green
