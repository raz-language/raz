# Building Raz on Windows

Raz supports native Windows builds with the MSVC SDK and Clang/Clang-CL toolchain.

## Recommended build

From a Developer PowerShell or a normal PowerShell with Visual Studio Build Tools installed:

```powershell
./bootstrap.bat
```

The bootstrap driver configures/reuses the native Stage-0 components, constructs the production Raz compiler, and performs one Raz-owned self-host build. Deterministic release verification is optional via `--verify-reproducibility`.

Useful options include:

```text
-Clean
-RunTests
-Jobs <n>
-HostPreset release
-BootstrapProfile release
--verify-reproducibility   # optional release/CI fixed-point check
```

## Native components only

The CMake build can be invoked directly when only the native runtime, Forge, or host tools are required:

```powershell
cmake --preset release
cmake --build --preset release
```

## Toolchain requirements

- Visual Studio Build Tools with the Windows SDK
- Clang/Clang-CL
- CMake
- Ninja or the generator configured by the selected preset
- Python 3 for repository qualification tooling

The build driver discovers the Visual Studio environment and preserves include/library paths when invoking Clang-CL and the Windows linker.

## Output

Repository build output and compiler output are intentionally split:

```text
build/
├─ debug/
└─ release/

target/bootstrap/
├─ candidate/
├─ repro-1/        # normal final self-hosted compiler
└─ repro-2/        # only with --verify-reproducibility
```

On Windows the normal final compiler is `target/bootstrap/repro-1/target/<profile>/bin/raz-compiler.exe`. When `--verify-reproducibility` is requested, `repro-2` is the independent comparison generation rather than the everyday bootstrap artifact. Release packaging installs the production `raz`/`razc` executables rather than the native host compiler. See [Installation](INSTALLATION.md) for the redistributable layout.
