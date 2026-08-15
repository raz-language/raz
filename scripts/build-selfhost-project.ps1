# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0

[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][string]$Project,
    [string]$Output = '',
    [switch]$Clean
)
$ErrorActionPreference = 'Stop'
& (Join-Path $PSScriptRoot 'build-project.ps1') -Project $Project -Output $Output -Clean:$Clean
exit $LASTEXITCODE
