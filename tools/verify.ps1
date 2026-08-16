# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0

[CmdletBinding()]
param(
    [ValidateSet('debug', 'release')][string]$Preset = 'debug',
    [int]$Jobs = [Math]::Max(1, [Environment]::ProcessorCount),
    [switch]$FullCompilerQualification
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path

function Invoke-Checked {
    param([string]$Label, [string]$FilePath, [string[]]$Arguments)
    Write-Host "[RUN] $Label" -ForegroundColor Cyan
    & $FilePath @Arguments
    if ($LASTEXITCODE -ne 0) { throw "$Label failed with exit code $LASTEXITCODE" }
}

Push-Location $root
try {
    Invoke-Checked 'Source contracts' 'python' @('tests/python/check-source.py')
    Invoke-Checked 'Raz formatter regression' 'python' @('tests/python/check-raz-formatter-layout.py')
    Invoke-Checked 'C++ spacing' 'python' @(
        'tools/format-cpp-spacing.py', '--check', 'src', 'tests')

    Invoke-Checked 'Configure' 'cmake' @('--preset', $Preset)
    Invoke-Checked 'Build' 'cmake' @('--build', '--preset', $Preset, '--parallel', "$Jobs")

    if ($FullCompilerQualification) {
        if ($env:OS -ne 'Windows_NT') {
            throw '-FullCompilerQualification requires Windows.'
        }
        $args = @('-BootstrapProfile', 'release', '-HostPreset', $Preset, '-Jobs', "$Jobs", '-RunTests')
        & (Join-Path $root 'bootstrap.bat') @args
        if ($LASTEXITCODE -ne 0) { throw "Full compiler qualification failed with exit code $LASTEXITCODE" }
    }
    else {
        Invoke-Checked 'CTest' 'ctest' @(
            '--test-dir', "build/$Preset", '--output-on-failure', '-j', "$Jobs", '-E',
            '^raz-compiler-(frontend-qualification|host-build|ir-qualification|production-qualification|reproducibility)$')
    }

    Write-Host 'Raz verification: PASS' -ForegroundColor Green
}
finally {
    Pop-Location
}
