# Forge 2.0 stabilization

Forge 2.0 closes the initial production roadmap around the x86-64 compiler core.

## Unified driver workflows

- `forge inspect <file.fir> --stage=source|optimized|machine|all` prints the verified, optimized, and lowered representations.
- `forge explain <file.fir> -O2` reports only passes that changed each function and summarizes rewritten operations, removals, and eliminated blocks.
- `forge doctor` checks the host, CMake, C++ compiler, native linker, and workspace writability.

## Validation model

The strict production matrix executes all tests, including JIT and installed consumers. The ASan/UBSan preset executes sanitizer-safe compiler-core tests and excludes tests whose purpose requires entering uninstrumented generated machine code or launching separately linked installed consumers. This keeps sanitizer findings actionable without weakening the authoritative production matrix.

## Supported production scope

Forge 2.0 provides the frontend development kit, verified IR, interpreter, x86-64 JIT and object generation, System V and Windows aggregate ABI lowering, deterministic ELF/COFF objects and archives, native static/shared library workflows, incremental artifacts, optimization pipelines, and CFG-aware register allocation.
