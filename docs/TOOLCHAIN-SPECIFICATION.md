# Raz toolchain specification

The `raz` command is the package/toolchain driver. `razc` is the direct compiler frontend used by low-level and conformance workflows. Raz 1.0 production releases install the Raz-written compiler as `razc`; bootstrap generation numbers are release-qualification details rather than part of the user-facing tool model.

## Backend contract

Raz lowers every program through the same typed HIR and backend-neutral MIR. Forge is the default production backend; LLVM is a first-class alternate backend implemented in Raz.

`razc --backend=forge` emits Forge IR for the normal Forge native pipeline. `razc --backend=llvm` emits LLVM IR, and `--emit=obj` / `--emit=exe` let the Raz-owned LLVM toolchain layer drive LLVM/Clang native production. Backend selection must not change language semantics. See `docs/backends.md` for target, optimization, linkage, and differential-qualification details.

## Project contract

The compiler accepts a `raz.toml` project and owns deterministic project graph construction. `[package].source` defaults to `src`; `[package].entry` defaults to `src/main.rz`. Path dependencies are resolved recursively, dependency sources are ordered before dependents, cycles are rejected, and duplicate diamond dependencies are loaded once. A package name is one graph identity: resolving the same package name to different versions or canonical roots in one graph is an error rather than an order-dependent choice.

Each physical source module has deterministic package/module ownership even when source files use natural namespace names. Imports may use `as` aliases and `public import` re-exports. Import alias collisions, duplicate source namespace ownership within a package, unresolved imports, and multiple executable `main` owners are rejected during graph discovery. Only the configured executable entry module may define the package entrypoint.

Visibility is enforced at semantic composition boundaries: `public` crosses packages, the default visibility is package-internal, and `private` is module-only. Dependency interface files use `raz-interface-v5`. Public names remain the only dependency API, while encoded hidden semantic entries supply package-internal signatures required to specialize exported generic implementation bodies. Visibility checks still reject direct consumer access to those hidden declarations. The interface fingerprint includes the public surface plus only the referenced hidden generic closure, so unrelated internal declarations remain downstream-fresh while a helper signature/layout required by generic specialization invalidates consumers. Same-package module checking receives a package semantic surface that excludes `private` declarations.

Normal packages use deterministic source discovery. A package that requires a specific physical concatenation order may provide `source-order.txt` at the package root; each non-comment line is a package-relative `.rz` path. Both the native bootstrap loader and Raz project loader honor the same contract.

Direct `.rz` and deterministic manifest inputs remain supported by bootstrap/low-level compiler workflows where qualification requires them.

## Reproducibility

Unchanged inputs must produce deterministic project ordering and compiler output. The release bootstrap requires Stage 2, Stage 3, and Stage 4 to emit byte-identical Forge modules.

Formatter output must be idempotent, and generated lockfiles/metadata produced by supported toolchain commands should be reproducible for identical inputs.

## Native artifacts

On supported x86-64 hosts the toolchain can produce native artifacts through Forge or through the LLVM/Clang path plus the platform linker/runtime boundary. The repository includes System V x86-64 and Windows x64 ABI/object qualification.

## Release bootstrap generations

- Stage 0: native bootstrap/reference compiler.
- Stage 1: Raz-written compiler built by Stage 0.
- Stage 2: production self-hosted frontend.
- Stage 3/4: deterministic verification generations.

The production release requires a successful recursive fixed-point check and native-boundary audit before Stage 2 is installed.

## Stable language scope

The accepted language surface is defined by `docs/STABLE-LANGUAGE-SCOPE.md` and the repository conformance suite. Experimental library modules and target-specific extensions do not gain stable portability guarantees merely by existing in the tree.
