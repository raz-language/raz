# Compiler reproducibility

Normal Raz bootstrap performs one self-hosted compiler rebuild from the canonical `compiler/src/` source tree. Release/CI jobs can additionally verify deterministic fixed-point output with a second independent generation.

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

Normal bootstrap constructs the production compiler and rebuilds it once with itself. This final self-hosted compiler lives under `target/bootstrap/repro-1/`. For deterministic release verification, run `tools/bootstrap.py --verify-reproducibility`; that creates an independent `repro-2` generation and requires the two generated compiler objects to be identical.

Qualification workspaces and generated compilers are stored under `target/bootstrap/`. The Raz repository's native CMake host/runtime/Forge build lives separately under `build/<profile>/`. Each self-host/verification workspace keeps its native object and compiler executable under its own `target/<profile>/` subtree, so compiler-produced artifacts never appear beside `raz.toml` or `src/`. The normal `repro-1` workspace retains its incremental cache across bootstraps; `repro-2` is always constructed independently when verification is requested.

Release self-host compilation uses Forge `-O2` by default so the generated compiler is representative of the optimized production toolchain. Debug bootstrap defaults to `-O0`. The level can be overridden with `tools/bootstrap.py --repro-opt` or `bootstrap.repro-opt`; an optional verification generation always uses the same level as `repro-1`.

The check is intentionally broader than a compiler unit test: it exercises project loading, parsing, semantic analysis, HIR/MIR, Forge lowering, native object emission, linking, filesystem behavior, and deterministic metadata together.

## Performance-sensitive implementation

The compiler uses reusable arenas and stable metadata, avoids redundant full-source scans on the normal parse path, integrates with Forge through structured in-process APIs, fingerprints native link inputs, and avoids replacing unchanged native objects. Bootstrap also seeds `repro-1` with only the safe assembled-project input cache and preserves `repro-1/target/cache` between runs, so unchanged bootstraps can restore the already-qualified artifact without pretending a Stage-0 artifact is self-hosted.

## Measuring compiler throughput

When comparing compiler performance, record the host CPU, memory, operating system, native toolchain, optimization profile, source module count, frontend/backend elapsed time, output size, and peak memory where available. Use the same source tree and configuration for comparisons.
