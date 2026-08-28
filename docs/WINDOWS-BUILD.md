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
--verify-reproducibility   # optional release fixed-point check
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
├─ release/
│  ├─ bin/
│  │  ├─ raz.exe
│  │  └─ oblink.exe
│  ├─ lib/            # runtime + Forge support archives
│  └─ packages/       # canonical compiler package/module objects
└─ BUILD-SUMMARY.txt
```

Bootstrap uses temporary seed, candidate, self-host, web, and regression workspaces while qualification is running. They are intentionally preserved if bootstrap fails, but after a successful build they are removed and only the canonical modular production target above is retained. The final Windows compiler/CLI is `target/bootstrap/release/bin/raz.exe`. The internal compiler package remains named `raz-compiler`, but successful bootstrap retains the public `raz.exe` product name. Release packaging may also provide `razc.exe` as a compatibility alias and `razup.exe` as the toolchain-manager entry point. See [Installation](INSTALLATION.md) for the redistributable layout.
