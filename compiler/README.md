# Raz compiler

This directory contains the production Raz compiler, written in Raz.

The layout follows the same broad architectural idea as rustc: one compiler tree is composed of focused compiler packages with explicit dependency boundaries. The goal is not to copy rustc's crate count mechanically; Raz splits only where the boundary improves ownership, incremental rebuilds, or backend isolation.

`compiler/src/main.rz` is intentionally tiny. It delegates to `raz_driver`, while the compiler implementation lives in sibling `raz_*` packages.

Compiler package directories are **source-only**. Generated `target/` trees must never live under `compiler/src/raz_*`; package and bootstrap caches belong in the repository-level `target/` or `build/` trees so source discovery and archives cannot depend on stale generated state.

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
    raz_codegen_common/     # backend-neutral writer, ABI symbols, and MIR capability queries
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

## Driver internal ownership

`raz_driver` remains one compiler package, but its project-build implementation is split by responsibility rather than accumulated in one monolithic module:

```text
raz_driver/src/
  project.rz               # manifests, dependency graph, deterministic source assembly
  project_native.rz        # native paths, package-unit cache/object emission, final link
  project_web_manifest.rz  # target=web/package web fields and output/public paths
```

The split intentionally keeps package/API boundaries stable. Native artifact policy and web-manifest interpretation must not drift back into `project.rz`; `check-driver-project-layout.py` pins this ownership contract. Further driver decomposition should follow the same rule: extract cohesive subsystems behind narrow APIs before considering additional compiler packages.

Registry internals follow the same ownership rule. Semantic-version parsing/constraints live in `registry_semver.rz`, shared record/token helpers in `registry_support.rz`, index/version-selection helpers in `registry_index.rz`, project registry-state paths in `registry_state.rz`, manifest tracking/spec persistence in `registry_tracking.rz`, and verified constraint/lock hydration plus cache writes in `registry_resolver.rz`; `registry.rz` remains the orchestration layer for update, fetch, vendor, publish, and CLI workflows. `check-driver-registry-layout.py` prevents those extracted responsibilities from drifting back into the orchestration module.

CLI internals are also split by responsibility. `cli_support.rz` owns shared stdout/argv primitives, `cli_help.rz` owns command recognition, suggestions, help topics, and CLI-facing argument errors, while `cli.rz` retains build/run execution, diagnostics, project creation, and completion/status policy. `check-driver-cli-layout.py` pins that boundary so help/command-table growth does not turn the execution module back into a monolith.

## Native build artifacts

Package/module objects under:

```text
target/<profile>/packages/
```

are the canonical native object layout. `target/<profile>/obj/<package>.o` / `.obj` is only a whole-program linker scratch path and is removed after a successful executable link.

## Source ordering

Compiler packages use semantic module discovery and explicit imports. No compiler `source-order.txt` is retained. Bootstrap discovers the canonical package manifests and `.rz` sources directly, and `src/main.rz` remains the executable entry point.

### Backend dependency rule

Backends are siblings: Forge, LLVM, Wasm, and RXE may depend on `raz_codegen_common`, frontend/HIR/MIR packages, and runtime ABI declarations, but never on one another. `raz_codegen_web` is an application-target orchestrator and may explicitly consume Wasm. Native symbol identity, main discovery, async-MIR capability queries, and generic text buffering belong to `raz_codegen_common` rather than to Forge or LLVM.

HIR statement lowering is split by semantic responsibility. `semantic/statement_support.rz` owns generic block/statement construction and lexical block capture, `semantic/match_statements.rz` owns match-pattern parsing, ownership checks, exhaustiveness, and match HIR emission, while `semantic/statements.rz` remains the dispatcher for locals, assignments, defer/unsafe, loops, and block statement control flow. `check-hir-statement-layout.py` pins this ownership boundary.
Compile-time HIR semantics are split by responsibility. `semantic/comptime_eval.rz` owns deterministic constexpr/comptime evaluation, mutable comptime value materialization, block execution, and constant folding; `semantic/comptime.rz` owns parsing/orchestration, attributes, predeclaration, generic materialization, closure finalization, and incremental HIR build coordination. `check-hir-comptime-layout.py` pins this ownership boundary.
Ownership HIR semantics are split into three focused layers. `semantic/ownership_paths.rz` owns ownership-path projection, partial-move identity, predrop bookkeeping, and move-state mutation; `semantic/ownership_flow.rz` owns path-sensitive move-state propagation across branches, loops, blocks, and root statements; `semantic/ownership.rz` retains name/borrow/reference validation, coercions, trait/inherent calls, closure capture semantics, and reference-return validation. `check-hir-ownership-layout.py` pins this boundary.
Expression HIR lowering is layered as well. `semantic/expression_support.rz` owns primitive associated constants and immediate temporary member/method chaining; `semantic/expression_operators.rz` owns casts and the binary-operator precedence ladder; `semantic/expression_calls.rz` owns call signature validation, argument coercion, closure capture materialization, effect/unsafe checks, and direct/indirect call HIR emission; `semantic/expression_postfix.rz` owns member/method projection, enum postfix properties/payloads, propagation, and typed index-result/effect validation; `semantic/expressions.rz` remains the core primary-expression, recursive argument/index parsing, literal, and final expression dispatcher. `check-hir-expression-layout.py` pins this boundary.


### WASM host-layer ownership

The WASM backend keeps platform host adapters separated by responsibility. `wasm/wasi.rz` owns WASI preview1/runtime adaptation, `wasm/browser_host.rz` owns the `raz_web` browser import ABI and wrapper lowering, and `wasm/host_support.rz` owns host-neutral runtime-symbol matching shared by browser, WASI, and future lowering. Browser imports must not be added directly to `wasi.rz`.

### WASM WASI filesystem ownership

The WASM backend keeps host concerns layered. `wasm/wasi.rz` owns Preview1 process, stdio, environment, clock/random, sleep, and higher-level filesystem orchestration. `wasm/wasi_filesystem.rz` owns preopen discovery/path routing and the core file descriptor/path adapters (open/close/seek/tell/flush/eof/stat/exists). `wasm/wasi_support.rz` owns the tiny shared errno/memory helpers used across WASI adapters. Browser ABI lowering remains isolated in `wasm/browser_host.rz`.
