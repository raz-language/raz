# Compiler reproducibility

Normal Raz bootstrap performs one self-hosted compiler rebuild from the canonical `compiler/src/` source tree. Release tooling can additionally verify deterministic fixed-point output with a second independent generation.

## Deterministic inputs

Reproducibility requires stable:

- project/module discovery;
- dependency ordering;
- semantic metadata;
- HIR and MIR construction;
- backend lowering;
- object generation; and
- native link inputs.

The compiler has no source-order metadata. Host-side qualification discovers `compiler/src/**/*.rz` directly, and normal compiler builds use semantic module imports and the project graph.

## Qualification

Normal bootstrap constructs the production compiler and rebuilds it once with itself. The self-host generation is a qualification proof; after every gate passes, bootstrap promotes the canonical modular compiler to `target/bootstrap/release/bin/raz` (or `raz.exe` on Windows) and removes the disposable generation workspaces. For deterministic release verification, run `tools/bootstrap.py --verify-reproducibility`; bootstrap creates an independent second generation, compares the compiler objects byte-for-byte, records the fixed-point digest in `BUILD-SUMMARY.txt`, and removes that verification workspace after success.

The Raz repository's native CMake host/runtime/Forge build lives separately under `build/<profile>/`. Temporary seed/self-host workspaces use the same canonical profile layout as ordinary Raz projects while qualification runs, but successful bootstrap output is intentionally compact: `target/bootstrap/release/` contains only the production compiler, canonical package/module objects, and relocatable runtime/Forge/linker support, plus `target/bootstrap/BUILD-SUMMARY.txt`. Failed bootstraps retain their workspaces for diagnosis.

Release self-host compilation runs the Raz project in its `release` profile and uses Forge `-O2` by default. The profile selection and optimization level are explicit, so bootstrap cache identity matches the artifact that will be packaged. Debug bootstrap defaults to the debug profile and `-O0`. The level can be overridden with `tools/bootstrap.py --repro-opt` or `bootstrap.repro-opt`; an optional verification generation always uses the same level as `repro-1`.

The check is intentionally broader than a compiler unit test: it exercises project loading, parsing, semantic analysis, HIR/MIR, Forge lowering, native object emission, linking, filesystem behavior, and deterministic metadata together.

## Performance-sensitive implementation

The compiler uses reusable arenas and stable metadata, avoids redundant full-source scans on the normal parse path, integrates with Forge through structured in-process APIs, fingerprints native link inputs, and avoids replacing unchanged native objects. Bootstrap keeps the expensive native Stage-0 toolchain cache under `build/<profile>/`. Raz-owned qualification workspaces are disposable after a successful run so `target/bootstrap` remains a product artifact tree rather than a historical test/cache dump.

## Measuring compiler throughput

When comparing compiler performance, record the host CPU, memory, operating system, native toolchain, optimization profile, source module count, frontend/backend elapsed time, output size, and peak memory where available. Use the same source tree and configuration for comparisons.
