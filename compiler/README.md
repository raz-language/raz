# Raz compiler

This directory contains the production Raz compiler, written in Raz.

The layout follows the same broad architectural idea as rustc: one compiler tree is composed of focused compiler packages with explicit dependency boundaries. The goal is not to copy rustc's crate count mechanically; Raz splits only where the boundary improves ownership, incremental rebuilds, or backend isolation.

`compiler/src/main.rz` is intentionally tiny. It delegates to `raz_driver`, while the compiler implementation lives in sibling `raz_*` packages.

## Package layout

```text
compiler/
  src/
    main.rz                 # raz-compiler executable entry
    raz_lexer/              # tokenization and source primitives
    raz_parser/             # syntax parser
    raz_query/              # shared query database/context infrastructure
    raz_hir/                # HIR + semantic operations + traits
    raz_mir/                # HIR -> MIR + ownership facts + analysis/verification
    raz_mir_opt/            # canonical MIR optimization transforms + pass policy
    raz_borrowck/           # move/loan/reborrow/drop legality over canonical MIR
    raz_codegen_forge/      # Forge native backend
    raz_codegen_llvm/       # LLVM IR/native integration
    raz_codegen_wasm/       # WebAssembly backend
    raz_codegen_rxe/        # Raz executable bytecode backend
    raz_codegen_web/        # static/browser web lowering
    raz_driver/             # CLI, projects, packages, registry, LSP, tooling
```

The split follows real ownership/rebuild boundaries rather than crate-count mimicry. `raz_query` owns the query database/context, while HIR owns semantic operations that still require `HirBuilder`. `raz_mir` owns executable MIR plus the ownership-event facts recorded during lowering, its reusable CFG/dataflow/liveness analysis APIs, and structural verification. `raz_mir_opt` consumes that API through a one-way dependency and owns canonical optimization transforms/pass policy. `raz_borrowck` independently consumes MIR and owns move, partial-move, loan-region, reborrow, and drop legality. This keeps MIR independent of borrow checking while preserving ownership metadata as part of the canonical IR contract.

## Dependency direction

```text
raz_lexer
    ↓
raz_parser
    ↓
raz_query ──→ raz_hir
                 ↓
              raz_mir ──→ raz_borrowck
                  │
                  └────→ raz_mir_opt
                 ↓             ↓
                 └──────→ raz_driver
                 ↓
       raz_codegen_* packages
                 ↓
             raz_driver
                 ↓
            raz-compiler
```

Backends consume parser/HIR/MIR APIs directly where needed. Compiler packages must not introduce reverse semantic dependencies. In particular, HIR cannot depend on MIR, MIR cannot depend on `raz_borrowck` or `raz_mir_opt`, and backends do not perform ownership legality checks themselves. `raz_mir_opt` depends only on MIR and does not own language legality. The driver runs `raz_borrowck` before and after `raz_mir_opt` so transforms cannot silently invalidate ownership semantics. Query infrastructure remains data-oriented so `raz_query` does not depend back on HIR.

## Bootstrap boundary

The C++ Stage-0 compiler is frozen compatibility machinery. It predates some of the production compiler's cross-package interface behavior, so bootstrap presents HIR + MIR + borrow checking as a disposable `raz-middle` compatibility package only while Stage-0 constructs the first Raz seed compiler.

That compatibility package is never canonical source. The first Raz-owned self-host generation compiles the real `raz_hir`, `raz_mir`, `raz_mir_opt`, and `raz_borrowck` packages independently.

## Native build artifacts

Package/module objects under:

```text
target/<profile>/packages/
```

are the canonical native object layout. `target/<profile>/obj/<package>.o` / `.obj` is only a whole-program linker scratch path and is removed after a successful executable link.

## Source ordering

Compiler packages use semantic module discovery and explicit imports. No compiler `source-order.txt` is retained. Bootstrap discovers the canonical package manifests and `.rz` sources directly, and `src/main.rz` remains the executable entry point.
