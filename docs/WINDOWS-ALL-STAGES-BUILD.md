# Windows all-stage bootstrap

Raz includes a Windows bootstrap driver that builds and retains every compiler generation used to prove recursive self-hosting.

## Requirements

- Windows 10/11
- CMake 3.24+
- Ninja
- Visual Studio 2022 Build Tools or Visual Studio 2022 with the **C++ desktop workload** (MSVC toolset + Windows SDK)
- Optional LLVM/Clang installation; the script can use `clang-cl`, `cl`, or `clang++`
- PowerShell 5.1+ or PowerShell 7+

The bootstrap initializes the Visual Studio C++ environment itself, so it can be launched from a normal Command Prompt or PowerShell window. It validates the selected compiler with a real C++20 standard-library compile before configuring Raz and discards a stale host CMake build when its cached compiler does not match the validated compiler.

## One-command build

From the repository root:

```bat
bootstrap.bat
```

The script performs the complete chain:

1. **Stage 0** — configures the Release CMake preset and builds the native C++ Raz compiler, runtime, and Forge codegen helper.
2. **Stage 1** — Stage 0 builds the Raz-written bootstrap compiler package.
3. **Stage 2** — Stage 1 lowers the complete Raz compiler through the structured in-process Forge API; Forge emits COFF directly and the runtime links it into a native compiler.
4. **Stage 3** — Stage 2 recompiles the complete compiler through the same structured Forge path and is linked natively.
5. **Stage 4** — Stage 3 repeats the structured compilation once more.
6. The script requires the Stage 2, Stage 3, and Stage 4 native Forge objects to be byte-identical and to have the same SHA-256 hash.

Retained artifacts are written under:

```text
build\windows-all-stages\
  stage1\raz-stage1.exe
  stage2\raz-stage2.exe
  stage2\stage2.obj
  stage3\raz-stage3.exe
  stage3\stage3.obj
  stage4\raz-stage4.exe
  stage4\stage4.obj
  BUILD-SUMMARY.txt
```

## Useful options

Clean only the retained stage artifacts first:

```bat
bootstrap.bat -Clean
```

Build Stage 1 using its Release profile instead of the qualification-default Debug profile:

```bat
bootstrap.bat -BootstrapProfile release
```

Choose the CMake host preset and parallel job count:

```bat
bootstrap.bat -HostPreset release -Jobs 16
```

Also run the existing self-host CTest qualification after building all retained stages:

```bat
bootstrap.bat -RunTests
```

For CI/non-interactive use, invoke PowerShell directly or disable the batch-file pause:

```bat
set RAZ_NO_PAUSE=1
bootstrap.bat -Clean -RunTests
```

or:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\build-all-stages.ps1 -Clean -RunTests
```

Any failed configure, compile, Forge codegen, native link, stage generation, or fixed-point comparison stops the script immediately with a nonzero exit code.
