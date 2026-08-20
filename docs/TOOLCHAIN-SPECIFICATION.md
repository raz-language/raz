# Raz toolchain specification

The `raz` command is the project, package, and toolchain driver. `razc` is the direct compiler frontend used by low-level integrations and conformance workflows. Production distributions install the Raz-written compiler; native host components are build-time implementation details.

## Backend contract

Raz lowers every program through typed HIR and backend-neutral MIR. Forge is the default production backend. LLVM is a first-class alternate backend implemented through the Raz toolchain.

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

Supported x86-64 hosts can produce native artifacts through Forge or the LLVM/Clang path plus the platform linker/runtime boundary. The repository includes System V AMD64 and Windows x64 ABI and object-format qualification.

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
