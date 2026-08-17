# Compiler reproducibility

Raz release builds verify that the production compiler can reproduce itself deterministically from the canonical `compiler/src/` source tree.

## Deterministic inputs

Reproducibility requires stable:

- project/module discovery;
- dependency ordering;
- semantic metadata;
- HIR and MIR construction;
- backend lowering;
- object generation; and
- native link inputs.

The host-only `compiler/host-source-order.txt` file supplies deterministic physical ordering where the native host compiler requires it. Normal compiler builds use module imports and the project graph.

## Qualification

Release qualification constructs the production compiler, rebuilds it with itself, and compares the resulting compiler artifacts. Equivalent inputs must converge to identical output before distribution artifacts are accepted.

Release self-host generations use Forge `-O2` by default so each generated compiler is representative of the optimized production toolchain instead of forcing later generations to run an artificial `-O0` compiler. Debug qualification defaults to `-O0`. The level can be overridden with `tools/bootstrap.py --repro-opt` or the `bootstrap.repro-opt` setting; deterministic comparison is always performed on objects produced with the selected level.

The check is intentionally broader than a compiler unit test: it exercises project loading, parsing, semantic analysis, HIR/MIR, Forge lowering, native object emission, linking, filesystem behavior, and deterministic metadata together.

## Performance-sensitive implementation

The compiler uses reusable arenas and stable metadata, avoids redundant full-source scans on the normal parse path, integrates with Forge through structured in-process APIs, fingerprints native link inputs, and avoids replacing unchanged native objects. These properties reduce rebuild cost without weakening determinism.

## Measuring compiler throughput

When comparing compiler performance, record the host CPU, memory, operating system, native toolchain, optimization profile, source module count, frontend/backend elapsed time, output size, and peak memory where available. Use the same source tree and configuration for comparisons.
