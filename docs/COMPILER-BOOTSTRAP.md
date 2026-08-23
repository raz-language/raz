# Compiler bootstrap

The production Raz compiler is written in Raz. A compact native host compiler is included solely to construct the production compiler from a clean source checkout.

## Build model

A release build has three roles:

- **Host compiler / stage-0** — x86-64 Windows/Linux source builds use the compatibility-pinned native compiler under `src/bootstrap/`. AArch64 and macOS source builds use a compatible prebuilt Raz compiler as stage-0 while Forge AArch64 remains an experimental backend without recursive native bootstrap qualification. Forge can already emit Linux AArch64 ELF and Darwin arm64 Mach-O objects; the remaining stage-0 requirement is a release/bootstrap qualification boundary, not missing object-format support.
- **Production compiler** — the compiler under `compiler/src/`. This is the compiler installed and used by Raz developers and applications.
- **Self-host build** — one Raz-owned compiler rebuild used by normal bootstrap to prove the production compiler can compile itself. A second independent generation is optional release/CI reproducibility verification.

Generation numbering is an implementation detail of the build driver and is not part of the Raz user-facing toolchain.

Raz repository build artifacts and Raz-compiler artifacts use separate roots. The CMake seed/host toolchain build lives under `build/<profile>/`. Compiler construction and self-host generations, because they are produced by the Raz compiler pipeline, live under `target/bootstrap/`. This keeps ordinary repository builds in `build/` while preserving the invariant that compiler-produced artifacts stay in `target/`.

The native seed is optimized for bootstrap throughput rather than treated as a second production compiler. Generated host inputs are only rewritten when their contents change. The normal `repro-1` self-host workspace preserves its Raz incremental cache between bootstraps and hard-links immutable compiler sources when the filesystem supports it. An explicitly requested verification generation remains independent. These choices reduce redundant native compilation and file copying without changing the canonical compiler source or release reproducibility evidence.

## Compiler source

The canonical compiler is split into semantic modules under `compiler/src/` and is built through the normal Raz project/module graph. No compiler source-order metadata is retained; Stage 0 and qualification discover the module set from the source tree and explicit imports.

## Native boundary

Language behavior belongs in Raz. Parsing, semantic analysis, HIR/MIR, optimization policy, project loading, package resolution, formatting, diagnostics, and CLI behavior are owned by the Raz compiler.

Native code is limited to permanent host and ABI boundaries such as raw memory operations, filesystem/process access, networking, cryptographic engines, object/linker integration, and backend bridges.

## Host-compiler compatibility contract

The native host compiler is compatibility-pinned so ordinary language evolution cannot create a second compiler implementation. `tests/python/check-host-compiler-contract.py` verifies its accepted source contract against `tests/data/host-compiler-contract.sha256`.

Changes to the host compiler are reserved for compatibility, platform, correctness, or security requirements needed to construct the canonical production compiler.

## Reproducibility

Normal bootstrap performs one Raz-owned self-host rebuild. Release/CI qualification that requires deterministic convergence runs `tools/bootstrap.py --verify-reproducibility`, which adds a second independent generation and compares the compiler objects byte-for-byte. This keeps the daily bootstrap fast without removing the stronger release check.

For x86-64 Windows/Linux the self-host generation uses the optimized Forge pipeline by default in release mode (`-O2`); debug uses `-O0`. AArch64 and macOS use LLVM object emission. When `--verify-reproducibility` is enabled, the independent verification generation uses the same selected optimization level.

### LLVM stage-0 bootstrap on AArch64/macOS

Until Forge AArch64 is recursively bootstrap-qualified, a clean AArch64 source checkout needs one compatible prebuilt Raz compiler to cross the initial compiler-construction boundary:

```sh
./bootstrap.sh --bootstrap-profile release --host-preset release \
  --stage0 /path/to/raz-compiler
```

`RAZ_STAGE0_COMPILER=/path/to/raz-compiler` is equivalent. The stage-0 image is used only to compile the first production-compiler object with `--backend=llvm --emit=obj`; that object is linked against the freshly built host-native runtime, Forge library, and bridge. The normal self-host generation is produced by the newly built compiler itself; an optional second verification generation is produced by that self-hosted compiler.

See [Compiler reproducibility](COMPILER-REPRODUCIBILITY.md) and [Windows build](WINDOWS-BUILD.md).
