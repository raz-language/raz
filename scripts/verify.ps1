# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0

[CmdletBinding()]
param(
    [ValidateSet('dev', 'release')]
    [string]$Preset = 'dev',
    [int]$Jobs = [Math]::Max(1, [Environment]::ProcessorCount),
    [switch]$FullSelfHost
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
    Invoke-Checked 'Repository layout' 'python' @('scripts/check-layout.py')
    Invoke-Checked 'Self-host source set' 'python' @('scripts/check-selfhost-source-set.py')
    Invoke-Checked 'Compiler semantic modules' 'python' @('scripts/check-compiler-semantic-modules.py')
    Invoke-Checked 'Semantic query engine' 'python' @('scripts/check-query-engine.py')
    Invoke-Checked 'Query generic migration' 'python' @('scripts/check-query-generics.py')
    Invoke-Checked 'Canonical query identities' 'python' @('scripts/check-query-identities.py')
    Invoke-Checked 'Incremental query invalidation' 'python' @('scripts/check-query-invalidation.py')
    Invoke-Checked 'Persistent incremental cache' 'python' @('scripts/check-persistent-incremental.py')
    Invoke-Checked 'Module-granular HIR/MIR ownership' 'python' @('scripts/check-module-granular-incremental.py')
    Invoke-Checked 'Raz module graph/invalidation' 'python' @('scripts/check-phase4a-raz-modules.py')
    Invoke-Checked 'Raz module HIR/MIR cache artifacts' 'python' @('scripts/check-phase4a-cache-artifacts.py')
    Invoke-Checked 'MIR 2 architecture' 'python' @('scripts/check-mir2-architecture.py')
    Invoke-Checked 'MIR 2B analysis/ownership' 'python' @('scripts/check-mir2b-analysis.py')
    Invoke-Checked 'MIR 2C remap/compaction' 'python' @('scripts/check-mir2c-remap.py')
    Invoke-Checked 'MIR 2D CFG/scalar optimization' 'python' @('scripts/check-mir2d-cfg-scalar.py')
    Invoke-Checked 'MIR 2E ownership dataflow' 'python' @('scripts/check-mir2e-ownership-dataflow.py')
    Invoke-Checked 'MIR 2F partial moves' 'python' @('scripts/check-mir2f-partial-moves.py')
    Invoke-Checked 'MIR 2G borrow regions' 'python' @('scripts/check-mir2g-borrow-regions.py')
    Invoke-Checked 'MIR Phase 2 final contract' 'python' @('scripts/check-mir2-final.py')
    Invoke-Checked 'Stage 0 semantic freeze' 'python' @('scripts/check-stage0-semantic-freeze.py')
    Invoke-Checked 'Native boundary audit' 'python' @('scripts/check-native-boundary.py')
    Invoke-Checked 'Self-host runtime declaration audit' 'python' @('scripts/check-selfhost-runtime-declarations.py')
    Invoke-Checked 'Forge package audit' 'python' @('scripts/check-forge-package.py')
    Invoke-Checked 'Raz source formatting' 'python' @(
        'scripts/format-raz.py', '--check',
        'compiler', 'library', 'examples', 'tests', 'src')
    Invoke-Checked 'C++ spacing formatting' 'python' @(
        'scripts/format-cpp-spacing.py', '--check',
        'src', 'tests')

    Invoke-Checked 'Configure' 'cmake' @('--preset', $Preset)
    Invoke-Checked 'Build' 'cmake' @('--build', '--preset', $Preset, '--parallel', "$Jobs")

    if ($FullSelfHost) {
        if ($env:OS -ne 'Windows_NT') {
            throw '-FullSelfHost requires Windows because the production retained-stage bootstrap is a Windows workflow.'
        }
        & (Join-Path $root 'scripts/build-all-stages.ps1') -HostPreset $Preset -Jobs $Jobs -RunTests
        if ($LASTEXITCODE -ne 0) { throw "Full self-host qualification failed with exit code $LASTEXITCODE" }
    }
    else {
        Invoke-Checked 'Fast CTest qualification' 'ctest' @(
            '--test-dir', "build/$Preset", '--output-on-failure', '-j', "$Jobs", '-E',
            '^raz-self-host-(stage1-frontend|pass-a-complete|pass-b-probe|pass-b-complete|recursive-fixed-point)$')
    }

    Write-Host 'Raz verification: PASS' -ForegroundColor Green
}
finally {
    Pop-Location
}
