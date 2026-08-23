# Raz Stage-0 bootstrap compiler

This directory contains the native C++ Stage-0 compiler used only to construct the Raz-written compiler from a clean source checkout.

The host compiler is not the installed Raz compiler and does not define the evolving language surface. Language semantics, package behavior, diagnostics, optimization policy, and developer tooling belong to the Raz-written compiler under `compiler/`.

Native Stage-0 code is compatibility-pinned and limited to the source contract required to construct the production compiler plus permanent runtime/backend integration boundaries. Its public CLI is intentionally restricted to `build`, `check`, minimal help/version, and the private module worker used for parallel bootstrap compilation. Package management, running/tests, formatting, linting, docs, language-server features, benchmarking, profiling, publishing, installation, and other production commands exist only in the Raz-written toolchain.

## Stage-0 cache

The native Stage-0 toolchain is built once into the selected CMake build directory and then reused verbatim by later bootstrap runs. A normal `bootstrap.bat` / `bootstrap.sh` does not reconfigure or rebuild C++ Stage 0 when the complete cached artifact set is present. This keeps the common self-host cycle focused on the Raz-written compiler.

Use `--rebuild-stage0` when intentionally changing Stage-0 C++ sources. `-Clean` removes both the cached Stage-0 build and bootstrap qualification state, so the next run constructs Stage 0 again.

Release qualification verifies this boundary with `tests/data/host-compiler-contract.sha256`.
