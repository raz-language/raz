# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0

[CmdletBinding()]
param(
    [ValidateSet('debug', 'release')][string]$BootstrapProfile = 'debug',
    [ValidateSet('dev', 'release')][string]$HostPreset = 'release',
    [int]$Jobs = [Math]::Max(1, [Environment]::ProcessorCount),
    [switch]$Clean,
    [switch]$RunTests
)
$ErrorActionPreference = 'Stop'
& (Join-Path $PSScriptRoot 'bootstrap.ps1') -BootstrapProfile $BootstrapProfile -HostPreset $HostPreset -Jobs $Jobs -Clean:$Clean -RunTests:$RunTests
exit $LASTEXITCODE
