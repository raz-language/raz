# Raz source tree

Raz implementation and native compiler support code lives under this directory.

- `forge/` — bundled production Forge 2.0 backend implemented in C++ and exposed through Forge's library/C APIs.
- `bootstrap/` — native bootstrap compiler and tools used to seed the self-hosted Raz frontend.
- `runtime/` — narrow native OS/ABI runtime boundary used where Raz must cross into the host platform.

Raz language semantics and standard-library behavior remain in Raz. Forge is a compiler backend dependency, analogous to LLVM: backend implementation code is native by design and must not absorb Raz frontend semantics.
