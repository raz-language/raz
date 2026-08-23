# Raz toolchain specification

The `raz` command is the project, package, and toolchain driver. `razc` is the direct compiler frontend used by low-level integrations and conformance workflows. Production distributions install the Raz-written compiler; native host components are build-time implementation details.

## Backend contract

Raz lowers every program through typed HIR and backend-neutral MIR. Forge is the default production backend on x86-64 Windows/Linux. LLVM is a first-class production backend and remains the automatic native default on AArch64 and macOS. Forge now provides an experimental AArch64 ELF machine/object path, but it is not yet the qualified default.

Backend selection must not change language semantics. See [Backends](BACKENDS.md) for target, optimization, linkage, and backend qualification details.

## Project contract

Projects are defined by `raz.toml`. `[package].source` defaults to `src`; `[package].entry` defaults to `src/main.rz`. Dependencies are resolved deterministically, cycles are rejected, duplicate graph identities are deduplicated, and conflicting versions or canonical roots are errors rather than order-dependent choices.
Packages may additionally declare executable tool entries with `[[bin]]` records containing `name` and `entry`. The normal project build continues to use `[package].entry`; an explicit tool/binary selection may override the entry for that build without mutating the manifest.

Each physical source module has deterministic package/module ownership. Imports support aliases and public re-exports. Visibility is enforced at semantic composition boundaries: `public` crosses packages, default visibility is package-internal, and `private` is module-only.

Dependency interfaces preserve the public API plus the hidden semantic closure required to instantiate exported generics. Unrelated implementation details do not invalidate downstream packages.

## Reproducibility

Equivalent source, dependency, configuration, and toolchain inputs must produce deterministic project ordering, metadata, lockfiles, generated interfaces, and compiler output. Release qualification also verifies that the production compiler reproduces itself deterministically.

See [Compiler reproducibility](COMPILER-REPRODUCIBILITY.md).

## Native artifacts

Supported x86-64 hosts can produce native artifacts through Forge or LLVM/Clang. Linux AArch64 and macOS arm64 use LLVM/Clang with the same backend-neutral MIR and runtime contract. Qualification covers Windows x64/COFF, System V AMD64/ELF, AAPCS64/ELF, and Darwin AArch64/Mach-O object production. Forge production-parity native code generation remains x86-64; experimental AArch64 ELF64 and Darwin Mach-O arm64 encoders are available for qualification work.

## Installed layout

Release archives and installers use a shared redistributable layout:

```text
bin/                 raz, razc (language server is `raz lsp`)
lib/                 native runtime components
share/raz/library/   Raz standard library
manifest.sha256      redistributable integrity manifest
```

Windows MSI installation supports optional PATH registration. Portable installation supports user PATH, machine PATH, or no PATH modification.

## Language stability

The supported language surface is defined by [Language stability](LANGUAGE-STABILITY.md) and the conformance suite. Platform-specific facilities are documented explicitly and do not imply portability to unsupported targets.
