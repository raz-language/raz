# Raz host compiler

This directory contains the native host compiler and host-side tool entry points used to construct the production Raz compiler from a clean source checkout.

The host compiler is not the installed Raz compiler and does not define the evolving language surface. Language semantics, package behavior, diagnostics, optimization policy, and developer tooling belong to the Raz-written compiler under `compiler/`.

Native host code is compatibility-pinned and limited to the source contract required to construct the production compiler plus permanent runtime/backend integration boundaries.

Release qualification verifies this boundary with `tests/data/host-compiler-contract.sha256`.
