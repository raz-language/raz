# Building Raz on Windows

Raz supports native Windows builds with the MSVC SDK and Clang/Clang-CL toolchain.

## Recommended build

From a Developer PowerShell or a normal PowerShell with Visual Studio Build Tools installed:

```powershell
./bootstrap.bat
```

The bootstrap driver configures the native host components, constructs the production Raz compiler, and performs reproducibility qualification required for a release toolchain.

Useful options include:

```text
-Clean
-RunTests
-Jobs <n>
-HostPreset release
-BootstrapProfile release
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

Release packaging installs the production `raz`/`razc` executables rather than the native host compiler. See [Installation](INSTALLATION.md) for the redistributable layout.
