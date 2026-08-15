# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0

[CmdletBinding()]
param(
    [ValidateSet('debug', 'release')]
    [string]$BootstrapProfile = 'debug',

    [ValidateSet('dev', 'release')]
    [string]$HostPreset = 'release',

    [int]$Jobs = [Math]::Max(1, [Environment]::ProcessorCount),

    [ValidateRange(1, 3600)]
    [int]$StageStatusIntervalSeconds = 15,

    [switch]$Clean,
    [switch]$RunTests
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

function Repair-FutureSourceTimestamps {
    param([Parameter(Mandatory = $true)][string]$Root)

    # ZIP's legacy timestamp format has no timezone.  Archives created on a UTC
    # host can therefore extract on Windows with source timestamps several hours
    # ahead of the local clock.  Ninja then sees CMake inputs as perpetually newer
    # than build.ninja and loops forever through "Re-running CMake...".
    #
    # Repair only genuinely future-dated source files and never touch build output.
    $now = Get-Date
    $futureThreshold = $now.AddSeconds(30)
    $replacementTime = $now.AddMinutes(-2)
    $fixed = 0

    Get-ChildItem -LiteralPath $Root -Recurse -File -Force -ErrorAction SilentlyContinue |
        Where-Object {
            $_.FullName -notmatch '[\\/]build[\\/]' -and
            $_.FullName -notmatch '[\\/]\.git[\\/]' -and
            $_.LastWriteTime -gt $futureThreshold
        } |
        ForEach-Object {
            $_.LastWriteTime = $replacementTime
            $fixed++
        }

    if ($fixed -gt 0) {
        Write-Host "Repaired $fixed future-dated source file timestamp(s)." -ForegroundColor Yellow
        Write-Host "This prevents Ninja/CMake regeneration loops caused by ZIP timezone skew." -ForegroundColor DarkGray
    }
}

function Write-Step {
    param([string]$Message)
    Write-Host ''
    Write-Host ('=' * 78) -ForegroundColor DarkGray
    Write-Host "  $Message" -ForegroundColor Cyan
    Write-Host ('=' * 78) -ForegroundColor DarkGray
}

function Invoke-Checked {
    param(
        [Parameter(Mandatory = $true)][string]$Label,
        [Parameter(Mandatory = $true)][string]$FilePath,
        [string[]]$Arguments = @(),
        [string]$WorkingDirectory = ''
    )

    Write-Host "[RUN] $Label" -ForegroundColor Yellow
    Write-Host "      $FilePath $($Arguments -join ' ')" -ForegroundColor DarkGray

    $oldLocation = Get-Location
    try {
        if ($WorkingDirectory) {
            Set-Location -LiteralPath $WorkingDirectory
        }
        # Native tools such as Forge codegen write useful status lines to stdout.
        # Send those lines directly to the host instead of allowing them to become
        # PowerShell function return values. Otherwise a caller that captures the
        # result of Emit-NativeCompiler also captures lines such as
        # "OBJECT  COFF AMD64 ...", corrupting the executable path used by the
        # next bootstrap stage.
        & $FilePath @Arguments | Out-Host
        $exitCode = $LASTEXITCODE
    }
    finally {
        Set-Location -LiteralPath $oldLocation
    }

    if ($exitCode -ne 0) {
        throw "$Label failed with exit code $exitCode."
    }
}

function Require-Command {
    param([string]$Name)
    $command = Get-Command $Name -ErrorAction SilentlyContinue
    if (-not $command) {
        throw "Required tool '$Name' was not found in PATH."
    }
    return $command.Source
}

function Require-File {
    param([string]$Path, [string]$Description)
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw "$Description was not produced: $Path"
    }
    return (Resolve-Path -LiteralPath $Path).Path
}

function Find-File {
    param(
        [string]$Root,
        [string[]]$Names,
        [string]$Description
    )

    foreach ($name in $Names) {
        $match = Get-ChildItem -LiteralPath $Root -Recurse -File -Filter $name -ErrorAction SilentlyContinue |
            Select-Object -First 1
        if ($match) {
            return $match.FullName
        }
    }
    throw "$Description was not found below $Root."
}

function Read-CMakeCacheValue {
    param([string]$CachePath, [string]$Name)

    $pattern = '^' + [Regex]::Escape($Name) + '(?::[^=]+)?=(.*)$'
    foreach ($line in Get-Content -LiteralPath $CachePath) {
        if ($line -match $pattern) {
            return $Matches[1].Trim()
        }
    }
    throw "$Name was not found in $CachePath."
}

function Copy-BootstrapSource {
    param([string]$SourceRoot, [string]$Destination)

    if (Test-Path -LiteralPath $Destination) {
        Remove-Item -LiteralPath $Destination -Recurse -Force
    }
    New-Item -ItemType Directory -Path $Destination | Out-Null
    Copy-Item -Path (Join-Path $SourceRoot '*') -Destination $Destination -Recurse -Force
    foreach ($generated in @('.raz', 'target')) {
        $generatedPath = Join-Path $Destination $generated
        if (Test-Path -LiteralPath $generatedPath) {
            Remove-Item -LiteralPath $generatedPath -Recurse -Force
        }
    }

}

function New-CompilerInput {
    param([string]$Directory, [string]$Frontend)

    # Recursive fixed-point stages use the compiler's committed source-order
    # contract.  The Raz compiler is physically modular, but today those
    # modules are still combined into one logical translation unit.  Keeping
    # one explicit order makes Stage 0 project builds, recursive source-list
    # builds, and production raz.toml builds deterministic.
    $sourceDirectory = Split-Path -Parent $Frontend
    $compilerDirectory = Split-Path -Parent $sourceDirectory
    $sourceOrderPath = Join-Path $compilerDirectory 'bootstrap-source-order.txt'
    if (-not (Test-Path -LiteralPath $sourceDirectory -PathType Container)) {
        throw "Compiler source directory was not found: $sourceDirectory"
    }
    if (-not (Test-Path -LiteralPath $sourceOrderPath -PathType Leaf)) {
        throw "Compiler source order was not found: $sourceOrderPath"
    }

    if (Test-Path -LiteralPath $Directory) {
        Remove-Item -LiteralPath $Directory -Recurse -Force
    }
    $stageSourceDirectory = Join-Path $Directory 'src'
    New-Item -ItemType Directory -Path $stageSourceDirectory -Force | Out-Null
    Copy-Item -Path (Join-Path $sourceDirectory '*') -Destination $stageSourceDirectory -Recurse -Force

    $ordered = New-Object System.Collections.Generic.List[string]
    foreach ($raw in [IO.File]::ReadAllLines($sourceOrderPath)) {
        $relative = $raw.Trim().Replace('\', '/')
        if ([string]::IsNullOrWhiteSpace($relative) -or $relative.StartsWith('#')) {
            continue
        }
        if (-not $relative.StartsWith('src/')) {
            throw "Compiler source-order entry must be relative to compiler/: $relative"
        }
        $stagePath = Join-Path $Directory ($relative.Replace('/', '\'))
        if (-not (Test-Path -LiteralPath $stagePath -PathType Leaf)) {
            throw "Compiler source-order entry was not copied into recursive stage: $relative"
        }
        $ordered.Add($relative)
    }
    if ($ordered.Count -eq 0) {
        throw 'Compiler bootstrap-source-order.txt contains no source modules.'
    }
    if ($ordered[$ordered.Count - 1] -ne 'src/main.rz') {
        throw 'Compiler bootstrap-source-order.txt must keep src/main.rz as the final module.'
    }

    $inputPath = Join-Path $Directory 'stage-input.txt'
    [IO.File]::WriteAllText($inputPath, (($ordered -join "`n") + "`n"), [Text.Encoding]::ASCII)
}

function Link-StageCompiler {
    param(
        [string]$Compiler,
        [string]$Object,
        [string]$RuntimeLibrary,
        [string]$ForgeBridgeLibrary,
        [string]$ForgeLibrary,
        [string]$Executable,
        [string]$WorkingDirectory
    )

    $compilerName = [IO.Path]::GetFileName($Compiler).ToLowerInvariant()
    if ($compilerName -eq 'cl.exe' -or $compilerName -eq 'cl' -or
        $compilerName -eq 'clang-cl.exe' -or $compilerName -eq 'clang-cl') {
        $args = @('/nologo', $Object, $RuntimeLibrary, $ForgeBridgeLibrary, $ForgeLibrary, 'ws2_32.lib', 'bcrypt.lib', "/Fe:$Executable", '/link', '/STACK:8388608')
    }
    else {
        $args = @($Object, $RuntimeLibrary, $ForgeBridgeLibrary, $ForgeLibrary, '-o', $Executable, '-lws2_32', '-lbcrypt')
    }

    Invoke-Checked -Label "Link $([IO.Path]::GetFileName($Executable))" `
        -FilePath $Compiler -Arguments $args -WorkingDirectory $WorkingDirectory
}

function Emit-NativeCompiler {
    param(
        [int]$Stage,
        [string]$CxxCompiler,
        [string]$RuntimeLibrary,
        [string]$ForgeBridgeLibrary,
        [string]$ForgeLibrary,
        [string]$StageDirectory
    )

    $object = Join-Path $StageDirectory "stage$Stage.obj"
    $executable = Join-Path $StageDirectory "raz-stage$Stage.exe"

    Require-File -Path $object -Description "Stage $Stage in-process Forge object" | Out-Null
    Link-StageCompiler -Compiler $CxxCompiler -Object $object -RuntimeLibrary $RuntimeLibrary `
        -ForgeBridgeLibrary $ForgeBridgeLibrary -ForgeLibrary $ForgeLibrary `
        -Executable $executable -WorkingDirectory $StageDirectory
    Require-File -Path $executable -Description "Stage $Stage executable" | Out-Null

    return $executable
}

function Show-StageCompilerDiagnostic {
    param(
        [Parameter(Mandatory = $true)][string]$StageDirectory,
        [Parameter(Mandatory = $true)][string]$Frontend
    )

    $diagnosticPath = Join-Path $StageDirectory 'stage1-diagnostic.txt'
    if (-not (Test-Path -LiteralPath $diagnosticPath -PathType Leaf)) {
        return
    }

    $raw = (Get-Content -LiteralPath $diagnosticPath -Raw).Trim()
    if (-not $raw) {
        return
    }

    Write-Host "Self-host diagnostic: $raw" -ForegroundColor Yellow
    $parts = $raw -split '\s+'
    if ($parts.Count -lt 3) {
        return
    }

    $phaseNames = @{
        1 = 'syntax parser failure'
        2 = 'HIR construction failure'
        3 = 'MIR lowering failure'
        80 = 'compiler entry'
        81 = 'runtime/qualification startup complete'
        82 = 'project manifest path selected'
        83 = 'project loading started'
        84 = 'project loading completed'
        90 = 'source package loaded'
        91 = 'parse started'
        92 = 'syntax parser completed'
        93 = 'HIR completed'
        94 = 'MIR completed'
        95 = 'Forge IR emission completed'
    }
    $codeNames = @{
        0 = 'none'
        1 = 'InvalidByte'
        2 = 'ExpectedToken'
        3 = 'ExpectedIdentifier'
        4 = 'TooManyItems'
        5 = 'UnknownName'
        6 = 'DuplicateName'
        7 = 'UnknownType'
        8 = 'WrongArgumentCount'
        9 = 'MissingReturn'
    }

    [int64]$phase = 0
    [int64]$code = 0
    [int64]$offset = 0
    if (-not [int64]::TryParse($parts[0], [ref]$phase) -or
        -not [int64]::TryParse($parts[1], [ref]$code) -or
        -not [int64]::TryParse($parts[2], [ref]$offset)) {
        return
    }

    $phaseText = if ($phaseNames.ContainsKey([int]$phase)) { $phaseNames[[int]$phase] } else { "phase $phase" }
    $codeText = if ($codeNames.ContainsKey([int]$code)) { $codeNames[[int]$code] } else { "code $code" }
    Write-Host "  $phaseText; $codeText; source offset $offset" -ForegroundColor Yellow

    if ($offset -lt 0 -or -not (Test-Path -LiteralPath $Frontend -PathType Leaf)) {
        return
    }

    $source = [IO.File]::ReadAllText($Frontend)
    if ($offset -gt $source.Length) {
        return
    }
    $before = $source.Substring(0, [int]$offset)
    $line = 1 + ([regex]::Matches($before, "`n")).Count
    $lastNewline = $before.LastIndexOf("`n")
    $column = [int]$offset - $lastNewline
    $lineStart = $lastNewline + 1
    $lineEnd = $source.IndexOf("`n", [int]$offset)
    if ($lineEnd -lt 0) { $lineEnd = $source.Length }
    $sourceLine = $source.Substring($lineStart, $lineEnd - $lineStart).TrimEnd("`r")
    Write-Host "  compiler.rz:$line`:$column" -ForegroundColor Yellow
    Write-Host "  $sourceLine" -ForegroundColor DarkYellow
}

function Invoke-SelfHostCompiler {
    param(
        [Parameter(Mandatory = $true)][string]$Label,
        [Parameter(Mandatory = $true)][string]$FilePath,
        [Parameter(Mandatory = $true)][string]$WorkingDirectory,
        [string[]]$Arguments = @()
    )

    Write-Host "[RUN] $Label" -ForegroundColor Yellow
    Write-Host "      $FilePath $($Arguments -join ' ')" -ForegroundColor DarkGray

    $diagnosticPath = Join-Path $WorkingDirectory 'stage1-diagnostic.txt'
    Remove-Item -LiteralPath $diagnosticPath -Force -ErrorAction SilentlyContinue

    # Do not use Start-Process here. On some Windows/PowerShell combinations its
    # PassThru Process wrapper can expose HasExited while ExitCode remains null.
    # Launch through System.Diagnostics.Process so WaitForExit() and ExitCode are
    # obtained from the same native process handle.
    $startInfo = New-Object System.Diagnostics.ProcessStartInfo
    $startInfo.FileName = $FilePath
    $startInfo.WorkingDirectory = $WorkingDirectory
    $startInfo.UseShellExecute = $false
    $startInfo.CreateNoWindow = $false

    # Self-host stage arguments are filenames/flags today, but quote defensively
    # so this helper also remains correct if a future path contains spaces.
    $escapedArguments = foreach ($argument in $Arguments) {
        if ($argument -match '[\s"]') {
            '"' + ($argument -replace '"', '\\"') + '"'
        }
        else {
            $argument
        }
    }
    $startInfo.Arguments = ($escapedArguments -join ' ')

    $process = New-Object System.Diagnostics.Process
    $process.StartInfo = $startInfo
    if (-not $process.Start()) {
        throw "$Label could not start '$FilePath'."
    }

    $watch = [Diagnostics.Stopwatch]::StartNew()
    $lastDiagnostic = ''
    $lastStatusSecond = -1
    try {
        while (-not $process.HasExited) {
            Start-Sleep -Seconds 2
            $process.Refresh()

            if (Test-Path -LiteralPath $diagnosticPath -PathType Leaf) {
                $raw = (Get-Content -LiteralPath $diagnosticPath -Raw -ErrorAction SilentlyContinue).Trim()
                if ($raw -and $raw -ne $lastDiagnostic) {
                    $lastDiagnostic = $raw
                    $parts = $raw -split '\s+'
                    $phase = if ($parts.Count -gt 0) { $parts[0] } else { '?' }
                    $phaseNames = @{
                        '80' = 'compiler entry'
                        '81' = 'runtime/qualification startup complete'
                        '82' = 'project manifest path selected'
                        '83' = 'project loading started'
                        '84' = 'project loading completed'
                        '90' = 'source package loaded'
                        '91' = 'parse started'
                        '92' = 'syntax parser completed'
                        '93' = 'HIR completed'
                        '94' = 'MIR completed'
                        '95' = 'Forge IR emission completed'
                    }
                    $phaseText = if ($phaseNames.ContainsKey($phase)) { $phaseNames[$phase] } else { "phase $phase" }
                    Write-Host ("      [{0:mm\:ss}] self-host: {1} ({2})" -f $watch.Elapsed, $phaseText, $raw) -ForegroundColor DarkCyan
                }
            }

            $seconds = [int]$watch.Elapsed.TotalSeconds
            if ($seconds -ge $StageStatusIntervalSeconds -and [int]($seconds / $StageStatusIntervalSeconds) -ne $lastStatusSecond) {
                $lastStatusSecond = [int]($seconds / $StageStatusIntervalSeconds)
                $where = if ($lastDiagnostic) { "last diagnostic: $lastDiagnostic" } else { 'waiting for first compiler diagnostic' }
                Write-Host ("      [{0:mm\:ss}] still compiling; {1}" -f $watch.Elapsed, $where) -ForegroundColor DarkGray
            }
        }
    }
    finally {
        # WaitForExit is required before reading ExitCode from Process. Unlike
        # Start-Process -PassThru, this Process instance owns the native handle
        # directly and Windows will expose a concrete exit code here.
        $process.WaitForExit()
        $watch.Stop()
    }

    $exitCode = [int]$process.ExitCode
    if ($exitCode -ne 0) {
        throw "$Label failed with exit code $exitCode."
    }
    Write-Host ("      completed in {0}" -f $watch.Elapsed.ToString('hh\:mm\:ss\.fff')) -ForegroundColor Green
}

function Compile-NextStage {
    param(
        [int]$Stage,
        [string]$PreviousCompiler,
        [string]$Frontend,
        [string]$StageDirectory,
        [string]$CxxCompiler,
        [string]$RuntimeLibrary,
        [string]$ForgeBridgeLibrary,
        [string]$ForgeLibrary,
        [string]$ForgeOptimization
    )

    New-CompilerInput -Directory $StageDirectory -Frontend $Frontend
    # The structured Forge backend writes the native object beside this nominal
    # output path. Keeping the .fir stem preserves the existing bridge contract
    # without forcing the compiler-sized textual FIR compatibility transport.
    $outputStem = Join-Path $StageDirectory "stage$Stage.fir"
    $object = Join-Path $StageDirectory "stage$Stage.obj"

    try {
        Invoke-SelfHostCompiler -Label "Stage $($Stage - 1) -> Stage $Stage" -FilePath $PreviousCompiler `
            -Arguments @('--forge-native', '--forge-structured-only', $ForgeOptimization, 'stage-input.txt', "stage$Stage.fir") `
            -WorkingDirectory $StageDirectory
    }
    catch {
        Show-StageCompilerDiagnostic -StageDirectory $StageDirectory -Frontend $Frontend
        throw
    }

    Require-File -Path $object -Description "Stage $Stage structured Forge object" | Out-Null
    $objectSize = (Get-Item -LiteralPath $object).Length
    if ($objectSize -lt 500000) {
        throw "Stage $Stage structured Forge object is unexpectedly small: $objectSize bytes."
    }

    $exe = Emit-NativeCompiler -Stage $Stage -CxxCompiler $CxxCompiler `
        -RuntimeLibrary $RuntimeLibrary -ForgeBridgeLibrary $ForgeBridgeLibrary `
        -ForgeLibrary $ForgeLibrary -StageDirectory $StageDirectory

    return [PSCustomObject]@{
        Stage = $Stage
        Artifact = $object
        ArtifactSize = $objectSize
        Hash = (Get-FileHash -Algorithm SHA256 -LiteralPath $object).Hash.ToLowerInvariant()
        Executable = $exe
        Object = $object
        OutputStem = $outputStem
    }
}

function Assert-ByteIdentical {
    param([string]$Left, [string]$Right, [string]$Label)

    $leftInfo = Get-Item -LiteralPath $Left
    $rightInfo = Get-Item -LiteralPath $Right
    if ($leftInfo.Length -ne $rightInfo.Length) {
        throw "$Label failed: file sizes differ ($($leftInfo.Length) vs $($rightInfo.Length))."
    }

    $leftHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $Left).Hash
    $rightHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $Right).Hash
    if ($leftHash -ne $rightHash) {
        throw "$Label failed: SHA-256 differs ($leftHash vs $rightHash)."
    }
}


function Import-BatchEnvironment {
    param(
        [Parameter(Mandatory = $true)][string]$BatchFile,
        [string[]]$Arguments = @()
    )

    if (-not $env:ComSpec) {
        throw 'ComSpec is not set; cannot initialize the Visual Studio build environment.'
    }

    $quotedBatch = '"' + $BatchFile + '"'
    $commandLine = "$quotedBatch $($Arguments -join ' ') >nul && set"
    $lines = & $env:ComSpec /d /s /c $commandLine
    if ($LASTEXITCODE -ne 0) {
        throw "Failed to import environment from $BatchFile."
    }

    foreach ($line in $lines) {
        $separator = $line.IndexOf('=')
        if ($separator -le 0) {
            continue
        }

        $name = $line.Substring(0, $separator)
        $value = $line.Substring($separator + 1)
        [Environment]::SetEnvironmentVariable($name, $value, 'Process')
    }
}

function Find-VisualStudioDevCmd {
    $vswhereCandidates = @()
    if (${env:ProgramFiles(x86)}) {
        $vswhereCandidates += (Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe')
    }
    if ($env:ProgramFiles) {
        $vswhereCandidates += (Join-Path $env:ProgramFiles 'Microsoft Visual Studio\Installer\vswhere.exe')
    }

    foreach ($vswhere in $vswhereCandidates | Select-Object -Unique) {
        if (-not (Test-Path -LiteralPath $vswhere -PathType Leaf)) {
            continue
        }

        $installation = (& $vswhere -latest -products * `
            -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
            -property installationPath 2>$null | Select-Object -First 1)
        if ($installation) {
            $devCmd = Join-Path $installation.Trim() 'Common7\Tools\VsDevCmd.bat'
            if (Test-Path -LiteralPath $devCmd -PathType Leaf) {
                return $devCmd
            }
        }
    }

    $roots = @()
    if ($env:ProgramFiles) {
        $roots += (Join-Path $env:ProgramFiles 'Microsoft Visual Studio')
    }
    if (${env:ProgramFiles(x86)}) {
        $roots += (Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio')
    }

    foreach ($visualStudioRoot in $roots | Select-Object -Unique) {
        if (-not (Test-Path -LiteralPath $visualStudioRoot -PathType Container)) {
            continue
        }

        $match = Get-ChildItem -LiteralPath $visualStudioRoot -Recurse -File `
            -Filter 'VsDevCmd.bat' -ErrorAction SilentlyContinue |
            Where-Object { $_.FullName -match '\\Common7\\Tools\\VsDevCmd\.bat$' } |
            Sort-Object FullName -Descending |
            Select-Object -First 1
        if ($match) {
            return $match.FullName
        }
    }

    return ''
}

function Initialize-WindowsBuildEnvironment {
    if ($env:VSCMD_VER -and $env:VCToolsInstallDir -and $env:WindowsSdkDir) {
        Write-Host "Visual Studio environment already active ($($env:VSCMD_VER))." -ForegroundColor Green
        return
    }

    $devCmd = Find-VisualStudioDevCmd
    if (-not $devCmd) {
        throw @'
A usable Visual Studio C++ build environment could not be found.

Raz/Forge needs the MSVC C++ standard library and Windows SDK even when LLVM
clang/clang-cl is the selected compiler. Install Visual Studio 2022 Build Tools
with "Desktop development with C++", including the MSVC toolset and Windows SDK,
then run bootstrap.bat again.
'@
    }

    Write-Host "Initializing Visual Studio toolchain: $devCmd" -ForegroundColor DarkGray
    Import-BatchEnvironment -BatchFile $devCmd -Arguments @('-no_logo', '-arch=x64', '-host_arch=x64')

    if (-not $env:VCToolsInstallDir) {
        throw 'Visual Studio initialized, but VCToolsInstallDir is missing.'
    }
    if (-not $env:WindowsSdkDir) {
        throw 'Visual Studio initialized, but WindowsSdkDir is missing.'
    }
}

function Resolve-ExecutablePath {
    param([string]$Candidate)

    if (-not $Candidate) {
        return ''
    }

    if (Test-Path -LiteralPath $Candidate -PathType Leaf) {
        return (Resolve-Path -LiteralPath $Candidate).Path
    }

    $command = Get-Command $Candidate -ErrorAction SilentlyContinue
    if ($command) {
        return $command.Source
    }
    return ''
}

function Test-Cxx20Toolchain {
    param(
        [Parameter(Mandatory = $true)][string]$Compiler,
        [Parameter(Mandatory = $true)][string]$ScratchRoot
    )

    New-Item -ItemType Directory -Path $ScratchRoot -Force | Out-Null
    $source = Join-Path $ScratchRoot 'raz-cxx20-preflight.cpp'
    $object = Join-Path $ScratchRoot 'raz-cxx20-preflight.obj'

    @'
#include <optional>
#include <string_view>
#include <vector>
int raz_cxx20_preflight() {
    std::optional<std::string_view> value{"raz"};
    std::vector<int> values{1, 2, 3};
    return value ? static_cast<int>(value->size() + values.size()) : 0;
}
'@ | Set-Content -LiteralPath $source -Encoding ASCII

    Remove-Item -LiteralPath $object -Force -ErrorAction SilentlyContinue

    $compilerName = [IO.Path]::GetFileName($Compiler).ToLowerInvariant()
    if ($compilerName -eq 'cl.exe' -or $compilerName -eq 'cl' -or
        $compilerName -eq 'clang-cl.exe' -or $compilerName -eq 'clang-cl') {
        $arguments = @('/nologo', '/std:c++20', '/EHsc', '/c', $source, "/Fo$object")
    }
    else {
        $arguments = @('-std=c++20', '-c', $source, '-o', $object)
    }

    $output = & $Compiler @arguments 2>&1
    $exitCode = $LASTEXITCODE
    $ok = ($exitCode -eq 0 -and (Test-Path -LiteralPath $object -PathType Leaf))

    if (-not $ok) {
        $message = ($output | Select-Object -Last 8) -join [Environment]::NewLine
        return [PSCustomObject]@{
            Ok = $false
            Diagnostic = $message
        }
    }

    return [PSCustomObject]@{
        Ok = $true
        Diagnostic = ''
    }
}

function Select-WindowsCxxCompiler {
    param(
        [string]$CachedCompiler,
        [string]$ScratchRoot
    )

    $candidateNames = @()
    if ($CachedCompiler) {
        $candidateNames += $CachedCompiler
    }
    if ($env:CXX) {
        $candidateNames += $env:CXX
    }

    # Prefer the MSVC-compatible Clang driver when LLVM is installed, then MSVC.
    # Keep clang++ as a final option because it is also valid once VS/SDK discovery
    # is correctly initialized.
    $candidateNames += @('clang-cl.exe', 'cl.exe', 'clang++.exe')

    $seen = @{}
    $diagnostics = @()
    foreach ($candidate in $candidateNames) {
        $compiler = Resolve-ExecutablePath -Candidate $candidate
        if (-not $compiler) {
            continue
        }

        $key = $compiler.ToLowerInvariant()
        if ($seen.ContainsKey($key)) {
            continue
        }
        $seen[$key] = $true

        Write-Host "Checking C++20 toolchain: $compiler" -ForegroundColor DarkGray
        $probe = Test-Cxx20Toolchain -Compiler $compiler -ScratchRoot $ScratchRoot
        if ($probe.Ok) {
            return $compiler
        }

        $diagnostics += "$compiler`n$($probe.Diagnostic)"
    }

    $details = $diagnostics -join "`n`n"
    throw "No working Windows C++20 compiler was found after Visual Studio initialization.`n`n$details"
}

try {
    if ($env:OS -ne 'Windows_NT') {
        throw 'This script is the Windows bootstrap driver. Run it from Windows.'
    }
    if ($Jobs -lt 1) {
        throw '-Jobs must be at least 1.'
    }

    $scriptRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
    $root = (Resolve-Path (Join-Path $scriptRoot '..')).Path

    Repair-FutureSourceTimestamps -Root $root

    Write-Step 'Initialize Windows C++20 build environment'
    Initialize-WindowsBuildEnvironment

    $cmake = Require-Command 'cmake.exe'
    $ninja = Require-Command 'ninja.exe'
    $ctest = ''
    if ($RunTests) {
        $ctest = Require-Command 'ctest.exe'
    }

    $hostBuild = Join-Path $root "build\$HostPreset"
    $allStagesRoot = Join-Path $root 'build\windows-all-stages'
    $cachePathCandidate = Join-Path $hostBuild 'CMakeCache.txt'
    $toolchainProbeRoot = Join-Path $root 'build\.toolchain-preflight'

    if ($Clean -and (Test-Path -LiteralPath $hostBuild -PathType Container)) {
        Write-Step 'Cleaning host build tree'
        Remove-Item -LiteralPath $hostBuild -Recurse -Force
    }

    # Toolchain selection is deliberately one-way.  An existing CMake cache owns
    # the compiler selection for that build tree; we validate it but never delete
    # the tree merely because PATH/CXX changed.  On a fresh tree we choose one
    # working compiler once and pass it explicitly to CMake.  This prevents the
    # Windows bootstrap from oscillating between clang++, clang-cl, and cl across
    # configure invocations.
    $selectedCompiler = ''
    $configureCompilerArgument = @()
    if (Test-Path -LiteralPath $cachePathCandidate -PathType Leaf) {
        $cachedCompiler = Read-CMakeCacheValue -CachePath $cachePathCandidate -Name 'CMAKE_CXX_COMPILER'
        $cachedResolved = Resolve-ExecutablePath -Candidate $cachedCompiler
        if (-not $cachedResolved) {
            throw "The existing CMake cache refers to a missing compiler: $cachedCompiler`nRun bootstrap.bat -Clean once to regenerate the host build."
        }

        Write-Host "Validating cached C++ compiler: $cachedResolved" -ForegroundColor DarkGray
        $probe = Test-Cxx20Toolchain -Compiler $cachedResolved -ScratchRoot $toolchainProbeRoot
        if (-not $probe.Ok) {
            throw "The compiler recorded in build\$HostPreset cannot compile the C++20 standard library.`nCompiler: $cachedResolved`n`n$($probe.Diagnostic)`n`nDelete build\$HostPreset (or run bootstrap.bat -Clean after updating the toolchain) and run again."
        }
        $selectedCompiler = $cachedResolved
    }
    else {
        $selectedCompiler = Select-WindowsCxxCompiler -CachedCompiler '' -ScratchRoot $toolchainProbeRoot
        $configureCompilerArgument = @("-DCMAKE_CXX_COMPILER=$selectedCompiler")
    }

    # Do not export CXX after configuration has been chosen.  CMake owns compiler
    # identity inside its build tree and changing CXX later can trigger needless
    # reconfiguration or compiler switching.
    Remove-Item Env:CXX -ErrorAction SilentlyContinue
    Write-Host "Selected C++ compiler: $selectedCompiler" -ForegroundColor Green

    if ($Clean -and (Test-Path -LiteralPath $allStagesRoot)) {
        Write-Step 'Cleaning retained self-host stage artifacts'
        Remove-Item -LiteralPath $allStagesRoot -Recurse -Force
    }
    New-Item -ItemType Directory -Path $allStagesRoot -Force | Out-Null

    Write-Step 'Stage 0 - configure and build the native host toolchain'
    Write-Host "CMake : $cmake"
    Write-Host "Ninja : $ninja"
    Write-Host "Preset: $HostPreset"
    Write-Host "Jobs  : $Jobs"

    $configureArguments = @('--preset', $HostPreset) + $configureCompilerArgument
    Invoke-Checked -Label 'Configure host toolchain' -FilePath $cmake `
        -Arguments $configureArguments -WorkingDirectory $root
    Invoke-Checked -Label 'Build host compiler, runtime, and in-process Forge bridge' -FilePath $cmake `
        -Arguments @('--build', '--preset', $HostPreset, '--parallel', "$Jobs", '--target',
                     'raz0', 'razc0', 'raz_runtime', 'raz_forge_bridge', 'forge') `
        -WorkingDirectory $root

    $cachePath = Require-File -Path (Join-Path $hostBuild 'CMakeCache.txt') -Description 'CMake cache'
    $cxxCompiler = Read-CMakeCacheValue -CachePath $cachePath -Name 'CMAKE_CXX_COMPILER'
    $cxxCompiler = Require-File -Path $cxxCompiler -Description 'CMake-selected C++ compiler'

    # The Raz-written/native package link step must use the exact host C++
    # driver selected and validated by CMake.  Do not let raz0 fall back to
    # clang++ on Windows: Visual Studio-only installations legitimately have
    # cl.exe but no clang++.  Keep this in the process environment so Stage 0
    # and any recursively invoked Raz compiler use the same native linker.
    $env:RAZ_LINKER = $cxxCompiler

    $razExe = Find-File -Root $hostBuild -Names @('raz0.exe') -Description 'Stage 0 raz0.exe'
    $razcExe = Find-File -Root $hostBuild -Names @('razc0.exe') -Description 'Stage 0 razc0.exe'
    $runtimeLibrary = Find-File -Root $hostBuild -Names @('raz_runtime.lib', 'libraz_runtime.a') -Description 'Raz runtime library'
    $forgeBridgeLibrary = Find-File -Root $hostBuild -Names @('raz_forge_bridge.lib', 'libraz_forge_bridge.a') -Description 'Raz Forge bridge library'
    $forgeLibrary = Find-File -Root $hostBuild -Names @('forge.lib', 'libforge.a') -Description 'Forge library'

    Write-Host "Stage 0 raz0: $razExe" -ForegroundColor Green
    Write-Host "Stage 0 razc0: $razcExe" -ForegroundColor Green
    Write-Host "C++ linker   : $cxxCompiler" -ForegroundColor Green
    Write-Host "Runtime      : $runtimeLibrary" -ForegroundColor Green
    Write-Host "Forge bridge : $forgeBridgeLibrary" -ForegroundColor Green
    Write-Host "Forge library: $forgeLibrary" -ForegroundColor Green

    Write-Step 'Bootstrap source integrity'
    $sourceBootstrap = Join-Path $root 'compiler'
    $frontend = Join-Path $sourceBootstrap 'src\main.rz'
    Require-File -Path $frontend -Description 'Canonical self-host compiler source' | Out-Null
    $sourceOrder = Join-Path $sourceBootstrap 'bootstrap-source-order.txt'
    Require-File -Path $sourceOrder -Description 'Canonical self-host compiler source order' | Out-Null
    $moduleCount = @([IO.File]::ReadAllLines($sourceOrder) | Where-Object { -not [string]::IsNullOrWhiteSpace($_) -and -not $_.TrimStart().StartsWith('#') }).Count
    Write-Host "Canonical self-host compiler: $moduleCount Raz modules (entry compiler/src/main.rz)" -ForegroundColor Green

    Write-Step 'Stage 1 - build the Raz-written compiler with Stage 0'
    $stage1Project = Join-Path $allStagesRoot 'stage1-project'
    Copy-BootstrapSource -SourceRoot $sourceBootstrap -Destination $stage1Project
    # Stage 0 is frozen before semantic-module interfaces. Build Stage 1 from a
    # disposable legacy view: remove compiler-only semantic namespace/import edges
    # and materialize source-order.txt only in this temporary copy.
    Get-ChildItem -LiteralPath (Join-Path $stage1Project 'src') -Filter '*.rz' -File -Recurse | ForEach-Object {
        $lines = [IO.File]::ReadAllLines($_.FullName) | Where-Object {
            -not $_.StartsWith('namespace raz_compiler_') -and
            -not $_.StartsWith('public import raz_compiler_')
        }
        [IO.File]::WriteAllLines($_.FullName, $lines, [Text.UTF8Encoding]::new($false))
    }
    Copy-Item -LiteralPath (Join-Path $stage1Project 'bootstrap-source-order.txt') `
        -Destination (Join-Path $stage1Project 'source-order.txt') -Force
    Invoke-Checked -Label 'Stage 0 -> Stage 1 native compiler' -FilePath $razExe `
        -Arguments @('build', $stage1Project, '--target', 'host', '--profile', $BootstrapProfile, '--force') `
        -WorkingDirectory $root

    $stage1Built = Join-Path $stage1Project "target\host\$BootstrapProfile\raz-compiler.exe"
    $stage1Built = Require-File -Path $stage1Built -Description 'Stage 1 executable'
    $stage1Dir = Join-Path $allStagesRoot 'stage1'
    New-Item -ItemType Directory -Path $stage1Dir -Force | Out-Null
    $stage1Exe = Join-Path $stage1Dir 'raz-stage1.exe'
    Copy-Item -LiteralPath $stage1Built -Destination $stage1Exe -Force
    Write-Host "Stage 1: $stage1Exe" -ForegroundColor Green

    # Recursive fixed-point stages use the structured in-process Forge backend.
    # This avoids serializing and reparsing the compiler-sized legacy textual FIR
    # transport while preserving a deterministic native-object fixed point. O0
    # keeps bootstrap qualification focused on semantic convergence; Forge still
    # performs required native lowering, register allocation, and encoding.
    $forgeBootstrapOptimization = '--opt=0'
    Write-Host "Recursive Forge optimization: $forgeBootstrapOptimization" -ForegroundColor Green

    $stage2 = Compile-NextStage -Stage 2 -PreviousCompiler $stage1Exe -Frontend $frontend `
        -StageDirectory (Join-Path $allStagesRoot 'stage2') -CxxCompiler $cxxCompiler `
        -RuntimeLibrary $runtimeLibrary -ForgeBridgeLibrary $forgeBridgeLibrary -ForgeLibrary $forgeLibrary `
        -ForgeOptimization $forgeBootstrapOptimization

    $stage3 = Compile-NextStage -Stage 3 -PreviousCompiler $stage2.Executable -Frontend $frontend `
        -StageDirectory (Join-Path $allStagesRoot 'stage3') -CxxCompiler $cxxCompiler `
        -RuntimeLibrary $runtimeLibrary -ForgeBridgeLibrary $forgeBridgeLibrary -ForgeLibrary $forgeLibrary `
        -ForgeOptimization $forgeBootstrapOptimization

    $stage4 = Compile-NextStage -Stage 4 -PreviousCompiler $stage3.Executable -Frontend $frontend `
        -StageDirectory (Join-Path $allStagesRoot 'stage4') -CxxCompiler $cxxCompiler `
        -RuntimeLibrary $runtimeLibrary -ForgeBridgeLibrary $forgeBridgeLibrary -ForgeLibrary $forgeLibrary `
        -ForgeOptimization $forgeBootstrapOptimization

    Write-Step 'Verify deterministic recursive fixed point'
    Assert-ByteIdentical -Left $stage2.Artifact -Right $stage3.Artifact -Label 'Stage 2 vs Stage 3 fixed point'
    Assert-ByteIdentical -Left $stage3.Artifact -Right $stage4.Artifact -Label 'Stage 3 vs Stage 4 fixed point'
    Write-Host "Stage 2 == Stage 3 == Stage 4 ($($stage2.ArtifactSize) object bytes)" -ForegroundColor Green
    Write-Host "SHA-256: $($stage2.Hash)" -ForegroundColor Green

    if ($RunTests) {
        Write-Step 'Run self-host qualification tests'
        Invoke-Checked -Label 'Self-host CTest qualification' -FilePath $cmake `
            -Arguments @('--build', '--preset', $HostPreset, '--parallel', "$Jobs") `
            -WorkingDirectory $root
        Invoke-Checked -Label 'Stage 2 + recursive fixed-point tests' -FilePath $ctest `
            -Arguments @('--test-dir', $hostBuild, '--output-on-failure', '-R',
                         'raz-self-host-(pass-b-complete|recursive-fixed-point|bootstrap-source-sync)') `
            -WorkingDirectory $root
    }

    $summaryPath = Join-Path $allStagesRoot 'BUILD-SUMMARY.txt'
    $summary = @(
        'Raz Windows all-stage bootstrap succeeded.',
        '',
        "Root: $root",
        "Host preset: $HostPreset",
        "Bootstrap profile: $BootstrapProfile",
        "C++ compiler: $cxxCompiler",
        '',
        "Stage 0 raz0: $razExe",
        "Stage 0 razc0: $razcExe",
        "Stage 1 compiler: $stage1Exe",
        "Stage 2 compiler: $($stage2.Executable)",
        "Stage 3 compiler: $($stage3.Executable)",
        "Stage 4 compiler: $($stage4.Executable)",
        '',
        "Fixed-point object bytes: $($stage2.ArtifactSize)",
        "Fixed-point SHA-256: $($stage2.Hash)",
        'Stage 2 == Stage 3 == Stage 4: yes'
    )
    Set-Content -LiteralPath $summaryPath -Value $summary -Encoding UTF8

    Write-Step 'ALL STAGES BUILT SUCCESSFULLY'
    Write-Host "Artifacts: $allStagesRoot" -ForegroundColor Green
    Write-Host "Summary  : $summaryPath" -ForegroundColor Green
    Write-Host ''
    Write-Host 'Usable compilers:' -ForegroundColor White
    Write-Host "  Stage 1  $stage1Exe"
    Write-Host "  Stage 2  $($stage2.Executable)"
    Write-Host "  Stage 3  $($stage3.Executable)"
    Write-Host "  Stage 4  $($stage4.Executable)"
    exit 0
}
catch {
    Write-Host ''
    Write-Host 'BUILD FAILED' -ForegroundColor Red
    Write-Host $_.Exception.Message -ForegroundColor Red
    exit 1
}
