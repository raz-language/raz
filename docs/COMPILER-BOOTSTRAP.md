# Compiler bootstrap

The production Raz compiler is written in Raz. A compact native host compiler is included solely to construct the production compiler from a clean source checkout.

## Build model

A release build has three roles:

- **Host compiler** — the compatibility-pinned native compiler under `src/bootstrap/`. Its responsibility is limited to constructing the first Raz compiler from the canonical source tree.
- **Production compiler** — the compiler under `compiler/src/`. This is the compiler installed and used by Raz developers and applications.
- **Reproducibility builds** — production-compiler rebuilds used to verify deterministic output before release artifacts are accepted.

Generation numbering is an implementation detail of the build driver and is not part of the Raz user-facing toolchain.

Raz repository build artifacts and Raz-compiler artifacts use separate roots. The CMake seed/host toolchain build lives under `build/<profile>/`. Compiler construction and reproducibility generations, because they are produced by the Raz compiler pipeline, live under `target/bootstrap/`. This keeps ordinary repository builds in `build/` while preserving the invariant that compiler-produced artifacts stay in `target/`.

The native seed is optimized for bootstrap throughput rather than treated as a second production compiler. Generated host inputs are only rewritten when their contents change, and reproducibility workspaces hard-link immutable compiler sources when the filesystem supports it. These choices reduce redundant native compilation and file copying without changing the canonical compiler source, generated objects, or deterministic-convergence checks.

## Compiler source

The canonical compiler is split into semantic modules under `compiler/src/` and is built through the normal Raz project/module graph. `compiler/host-source-order.txt` exists only to provide deterministic source ordering to the native host compiler where required; it is not the production compiler's module model.

## Native boundary

Language behavior belongs in Raz. Parsing, semantic analysis, HIR/MIR, optimization policy, project loading, package resolution, formatting, diagnostics, and CLI behavior are owned by the Raz compiler.

Native code is limited to permanent host and ABI boundaries such as raw memory operations, filesystem/process access, networking, cryptographic engines, object/linker integration, and backend bridges.

## Host-compiler compatibility contract

The native host compiler is compatibility-pinned so ordinary language evolution cannot create a second compiler implementation. `tests/python/check-host-compiler-contract.py` verifies its accepted source contract against `tests/data/host-compiler-contract.sha256`.

Changes to the host compiler are reserved for compatibility, platform, correctness, or security requirements needed to construct the canonical production compiler.

## Reproducibility

Release qualification rebuilds the production compiler recursively and requires deterministic convergence for equivalent source and toolchain inputs. This verifies that the installed compiler is reproducible from the repository source rather than depending on an opaque generated artifact.

For release qualification the self-host generations use the optimized Forge pipeline by default (`-O2`); debug qualification uses `-O0`. This keeps the compilers that execute later generations aligned with production performance while preserving byte-for-byte reproducibility checks at the selected optimization level.

See [Compiler reproducibility](COMPILER-REPRODUCIBILITY.md) and [Windows build](WINDOWS-BUILD.md).
