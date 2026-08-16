# Authors and credits

## Raz

**Mario Vinciguerra** - language design and implementation.

Raz 1.0 grew from the goal of building a modern native systems language with explicit ownership, deterministic resource management, strong compile-time semantics, and a backend interface small enough to understand end-to-end.

## Forge

Raz uses [Forge](https://github.com/Ascension-Digital-Technologies/Forge) as its production native backend. Raz also served as a substantial real-world frontend for proving Forge's design as a compact alternative to integrating LLVM into a language toolchain.

The C++ Forge backend is bundled under `src/forge/` and retains its nested Apache-2.0 license for independent redistribution. Raz integrates Forge through an audited backend ABI boundary, while the LLVM backend is implemented in Raz.

## Contributors

Contributions are recorded in the repository history. See `CONTRIBUTING.md` for contribution and review guidelines.
