# Raz compiler architecture

Raz uses a multi-backend compiler architecture with one language-semantic pipeline. Forge is the default native backend; LLVM is a first-class alternate backend. RXE and WebAssembly are additional code-generation targets.

## Compilation pipeline

1. load `raz.toml`, dependencies, and the deterministic module graph;
2. lex and parse source modules;
3. perform semantic analysis, type checking, ownership and lifetime validation, trait resolution, and generic resolution;
4. construct typed HIR;
5. lower to backend-neutral MIR and elaborate cleanup/drop behavior;
6. verify MIR ownership and control-flow invariants;
7. run MIR transformations;
8. dispatch to the selected backend; and
9. emit or link the requested artifact.

Backends do not define Raz source-language semantics. Forge, LLVM, RXE, WebAssembly, and the interpreter consume the same semantic and MIR decisions.

## Production compiler

The canonical compiler is implemented in Raz under `compiler/src/`. Modules are organized by responsibility and compiled through the normal package/module graph. `compiler/src/main.rz` is the process entry point; parsing, HIR, MIR, backend emission, project loading, package management, formatting, diagnostics, and tooling remain independent modules.

A compact native host compiler under `src/bootstrap/compiler/` exists only to construct the production compiler from source. It is compatibility-pinned by `tests/data/host-compiler-contract.sha256`; language evolution belongs in `compiler/src/` rather than being duplicated in the host compiler.

See [Compiler bootstrap](COMPILER-BOOTSTRAP.md) and [Compiler reproducibility](COMPILER-REPRODUCIBILITY.md).

## HIR and semantic queries

HIR carries typed source semantics after parsing and resolution. Compiler semantic services share a Raz-owned query database with canonical request identities, memoization, dependency tracking, cycle detection, fingerprints, and targeted reverse-dependent invalidation.

Physical modules retain source and exported-interface fingerprints so downstream packages are invalidated only when their relevant semantic inputs change. See [Semantic queries](SEMANTIC-QUERIES.md).

## MIR

MIR is the verified semantic boundary before execution or backend emission. It owns explicit basic blocks, values, places, storage lifetimes, moves, borrows, drops, and control-flow facts.

HIR remains authoritative for source-level borrow legality. MIR verification ensures backend consumers cannot observe structurally invalid ownership or lifetime state. Projection-aware metadata distinguishes fields, array elements, enum payloads, and dynamic-index aliases. Stores reinitialize the affected projection; CFG joins conservatively preserve ownership state.

Transformation passes preserve or explicitly remap instruction identities where required. See [MIR](MIR.md).

## Forge backend

Forge lives under `src/forge/` and is the default native backend. It owns:

- typed SSA verification;
- optimization;
- machine IR;
- ABI lowering;
- register allocation;
- instruction encoding;
- JIT infrastructure; and
- deterministic ELF/COFF object generation.

The Raz compiler integrates Forge in-process through the audited `raz_forge_bridge` boundary. Structured Forge APIs carry scalar and aggregate values, globals, functions, blocks, target features, source ranges, alignment, attributes, and optimization settings without requiring a compiler-sized textual intermediate file.

Textual Forge IR remains available for diagnostics, inspection, compatibility, and testing.

## LLVM backend

The LLVM backend is implemented under `compiler/src/backend/llvm/` and consumes the same MIR as Forge. It emits LLVM IR and uses the configured LLVM/Clang toolchain for native object or executable production.

Target triples, CPU/features, optimization, LTO, relocation/code models, visibility, libraries, and linker options are compiler configuration rather than language semantics. LLVM does not silently fall back to Forge.

See [Backends](BACKENDS.md).

## Runtime and standard library

`src/runtime/` is the narrow native boundary for operations that inherently cross the host ABI or operating system. It provides primitives for allocation, raw memory, atomics, threading, clocks, file/process handles, sockets/DNS, TLS, platform queries, and cryptographic engines.

Higher-level policy lives in Raz under:

```text
library/core/     language and low-level foundations
library/alloc/    allocation-backed data structures
library/std/      operating-system and application APIs
```

The standard library owns channels, task policy, recursive filesystem operations, buffered I/O, HTTP behavior, connection reuse, DNS caching, logging, compression, and other compositional services above the primitive native boundary.

The native boundary is audited by `tests/python/check-native-boundary.py`.

## Incremental compilation

The project driver and language server share a persistent module graph. Module fingerprints incorporate source, imports, target/profile configuration, and relevant dependency interfaces. Unrelated package changes therefore do not force broad recompilation.

Incremental metadata tracks semantic, HIR, MIR, backend IR, and final build fingerprints. Native builds emit cached module objects where safe, canonicalize equivalent generated specializations to deterministic owners, and content-address final link inputs so unchanged artifacts are not rewritten unnecessarily.

## Determinism

Project discovery, dependency traversal, package interfaces, lockfiles, module ordering, compiler metadata, backend output, and native link inputs are deterministic for equivalent inputs. Release qualification rebuilds the production compiler recursively and requires deterministic convergence.

## Native source organization

Native code is split by responsibility rather than file-size targets:

```text
src/bootstrap/   host compiler and host-side tool entry points
src/runtime/     permanent host/ABI runtime primitives
src/forge/       Forge backend
```

Private implementation headers under `detail/` are used when multiple routines intentionally share one translation-unit state model. `.inc` implementation files are not used.
