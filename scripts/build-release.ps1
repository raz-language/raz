# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0

[CmdletBinding()]
param([switch]$Clean)
$ErrorActionPreference='Stop'
& (Join-Path $PSScriptRoot 'release.ps1') -Clean:$Clean
exit $LASTEXITCODE
