# Raz compiler architecture

Raz uses a **multi-backend native compiler architecture**. Forge is the default production backend and LLVM is a first-class alternate backend implemented in Raz.

## Production pipeline

1. `raz.toml` project/dependency loading and deterministic source ordering;
2. lexing and parsing;
3. semantic analysis, type checking, ownership/lifetime validation, trait resolution, and generic resolution;
4. typed HIR construction;
5. backend-neutral MIR lowering and cleanup/drop elaboration;
6. backend dispatch through `compiler/src/driver/backend.rz`;
7. either Forge IR/native lowering or Raz-written LLVM IR generation and LLVM/Clang native orchestration;
8. platform-native linking with the Raz runtime where required.

No backend owns Raz source-language semantics. Forge and LLVM consume the same HIR/MIR decisions, including ownership cleanup, async state, call signatures, module storage, globals, and references.

### Ownership and loan analysis

Ownership is checked before backend selection in both the native bootstrap/reference frontend and the Raz-written production frontend. The loan model is field/path-sensitive and non-lexical: shared/mutable loans are retained only while their reference binding has a future use. Reborrow parents are suspended only for the live child-loan region, dynamic indices conservatively alias their containing storage, and move state is joined across control-flow paths.

Reference provenance is also validated through aggregate returns. Direct borrows and reference-bearing struct/tuple/array constructors are traced back to their storage origin, so safe code cannot hide a reference to stack-local storage inside an owned-looking return value. `tests/soundness/` is the release corpus for these invariants and is executed by both Stage 0 and the self-host compiler.

## Self-hosted MIR 2.0 layout

The Raz-written compiler owns MIR as a dedicated subsystem rather than as an extension of HIR. `compiler/src/mir/core/model.rz` owns `MirModule`, `core/builder.rz` owns construction primitives, `lowering.rz` performs semantic HIR-to-MIR lowering, `analysis/cfg.rz` exposes backend-neutral control-flow queries, `verify/verifier.rz` provides the structural verification boundary, `transform/pipeline.rz` owns the ordered MIR transformation pipeline, and `interpreter.rz` consumes MIR for execution and qualification. Normal compilation calls `run_mir_pipeline` before execution or backend emission.

See `docs/MIR.md` for the MIR verification, ownership, and optimization contract.

Expression lowering, lexical cleanup, and statement lowering share one lowering unit because they form a recursive semantic cluster. Explicit lowering-context APIs keep that cluster acyclic at the module boundary, while Forge and LLVM remain consumers of the same verified MIR.

## Verified MIR ownership firewall

MIR is a hard semantic boundary before Forge or LLVM. HIR-to-MIR lowering emits verifier-only ownership metadata alongside executable operations: `storage.live`, `storage.dead`, `move`, `borrow.shared`, and `borrow.exclusive`. These markers have no machine-code effect; they exist so backend lowering can assume ownership invariants have already been checked.

The MIR verifier propagates ownership state through the control-flow graph at projection granularity. `place.path` metadata binds physical MIR pointers to logical places such as `value.field`, `items[3]`, `items[*]`, enum payloads, and discriminants. A move/drop of one field therefore invalidates whole-value uses and overlapping projections without poisoning disjoint siblings. Constant array indexes remain independent; dynamic indexes conservatively overlap every element. Stores reinitialize the exact projection and its descendants, and CFG joins preserve these path-specific states. Compiler-generated pointer projections inherit the logical ownership domain of their immediate base when no source-level field name exists, while tagged-enum discriminants are tracked separately from moved payloads.

Compiler-generated `drop.*` cleanup regions are still validated structurally while trusting the drop-flag/discriminant predicate selecting the active cleanup edge. Pattern bindings that take ownership of enum payload storage begin a fresh logical `storage.live` lifetime, so repeated loop iterations and shadowed bindings do not inherit stale drop state from previous owners.

This verifier is invoked directly by HIR-to-MIR lowering; a verification failure is a compiler error and no backend is allowed to consume the invalid module. Forge treats ownership markers as zero-cost metadata. The malformed-MIR regression suite deliberately constructs use-after-drop, double-drop, CFG-joined use-after-move, and legal move-then-reinitialize modules to keep the boundary executable rather than documentary.

## Semantic query database

Compiler semantic services share a Raz-owned query database with canonical symbol/type/request identities, memoization, cycle detection, dependency tracking, and targeted reverse-dependent invalidation. Semantic input families are fingerprinted independently so unrelated mutations do not flush the entire query cache. Physical modules retain both full-source and exported-interface fingerprints for project-level incremental compilation.

See `docs/SEMANTIC-QUERIES.md` for the query identity and invalidation contract.

## Compiler implementations

The production frontend is the **Raz-written Stage 2 compiler** under `compiler`. It owns project loading and the compiler semantics required for the production compiler.

The production compiler is organized into responsibility-based semantic Raz modules under `compiler/src/`. Each file has an explicit compiler namespace/import edge and normal project builds compile the modules independently through the package graph. `compiler/bootstrap-source-order.txt` is not a production ordering contract; it is frozen seed metadata used only when Stage 0 must reconstruct Stage 1. The final `src/main.rz` module contains process and pipeline orchestration; lexer, HIR, MIR, Forge emission, LLVM emission, backend selection, and project loading remain independent source units.

`src/bootstrap/compiler` is the **frozen native bootstrap seed** used only to create the first Raz-written compiler and exercise bootstrap compatibility. It is not a second production language implementation and does not receive new language semantics for feature parity. The lexer/parser/semantic/HIR/MIR/lowering seed is pinned by `scripts/stage0-semantic-freeze.sha256`; `raz-stage0-semantic-freeze` fails if those sources drift. Intentional changes are limited to bootstrap-compatibility repairs needed to keep building the canonical Raz compiler.

Recursive bootstrap is a permanent release invariant. Stage 1 produces Stage 2 and Stage 2 produces Stage 3. The recursively generated Stage 2 and Stage 3 Forge modules must be byte-identical, and the corresponding native objects must also be deterministic.

## Backends

### Forge

Forge is the default production backend and lives under `src/forge` as the canonical C++ Forge 2.0 library. The short-lived Raz rewrite has been removed. Forge owns typed SSA verification, optimization, machine IR, ABI lowering, register allocation, instruction encoding, JIT infrastructure, and deterministic ELF/COFF object emission.

The self-hosted compiler links Forge **in-process** through the audited `raz_forge_bridge` boundary. Forge C API v14 exposes structured construction for aggregates, globals, functions, exact block/value names, parameters, target features, and generic operations with operands, successors, source ranges, alignment, and attributes, plus the existing `O0/O1/O2/O3/Os/Oz` optimization and COFF/ELF object-emission APIs. No production `forge-codegen` subprocess is required.

Raz MIR lowers directly into Forge C API v14 for scalar code, aggregates and arrays, references and slices, function pointers, target features, globals, and native TLS. The structured path supports integer/`f32`/`f64` functions, native scalar stack locals, mutable-parameter spills, direct internal and external calls, C/platform ABI metadata and `@link_name`, arithmetic, floating-point division, numeric casts, numeric comparisons, selects, loops, jumps, branches, MIR block arguments, returns, and scalar integer module storage. Forge optimization can promote eligible local stack slots through its normal scalar pipeline. Optional `.fir` output is generated independently for diagnostics and fixed-point comparison. Async/runtime-heavy MIR families use the verified in-memory FIR compatibility path, preserving full language coverage without moving Raz semantics into C++.

### LLVM

The LLVM IR backend is implemented in Raz under `compiler/src/backend/llvm/`. It consumes the same MIR as Forge and emits textual LLVM IR directly. The Raz-owned target/toolchain layer then invokes an external LLVM/Clang toolchain for object or executable production. Target triples, CPU/features, optimization/LTO, relocation/code models, visibility/linkage, libraries, and linker arguments are compiler options rather than C++ compiler semantics.

LLVM never silently falls back to Forge. Backend-specific capability gaps are explicit. TLS is native on both production backends; Forge v14 emits ELF/COFF TLS objects and LLVM uses LLVM native TLS lowering.

See `docs/backends.md` for the detailed backend contract and qualification corpus.

## Runtime and standard library

`src/runtime` is the permanent narrow native boundary for services that must cross into the host OS or ABI. The production standard library lives under `library/core`, `library/alloc`, and `library/std` and is written in Raz. Target-specific unavoidable extensions belong under `library/platform`; unstable APIs belong under `library/experimental`.

The release workflow audits the self-hosted compiler's native helper use against `scripts/native-boundary-allowlist.txt`. The Forge entry is a backend-library ABI bridge, not a language-semantic shim. LLVM native-output orchestration uses the process-launch boundary and follows the same native-boundary policy.

## Determinism

Project source discovery and path dependency traversal are deterministic. Canonical manifests prevent cycles and duplicate dependency loading. Recursive self-host qualification verifies deterministic compiler generation at both Forge-IR and native-object levels.

See `docs/SELF-HOSTING.md` for the complete stage and release model.

### Native runtime boundary

The production runtime is intentionally **primitive-first**. High-level policy is implemented in Raz and must not be duplicated in C++ merely for convenience. The native runtime owns host boundaries such as allocation, raw memory intrinsics, atomics/locks/conditions, thread creation, clocks/randomness, file handles and directory iteration, process launch, sockets/DNS, TLS/OpenSSL, CPU/ABI queries, and the compiler-required erased callable/trait and async-frame ABI.

The Raz standard library owns composition above those primitives. In particular, channels, latch/semaphore/barrier/once policy, cancellation, task scopes, worker scheduling, async file/socket jobs, reactor watches, timers, rich future composition, recursive filesystem create/remove, whole-file read/write policy, and socket send-all/receive-exact loops are implemented in `.rz`.

The self-host compiler has **zero Stage-1-specific native ABI**. Arena storage, references, ASCII/byte repacking, process/environment/stdio adapters, socket/TLS adaptation, whole-file I/O, absolute lexical path normalization, recursive source/tree discovery, recursive copy/create/remove, tool-adapter packing, host-platform adaptation, and Ed25519 arena packing all live in Raz. C++ exposes only generic host/runtime primitives (allocation, files/directories, processes, sockets/TLS, platform queries, and raw byte-oriented crypto). Stage 1 is therefore a Raz compatibility/data-model layer, not a native runtime concept. Compiler semantics are implemented in Raz. Stage 0 is frozen at the bootstrap contract and must shrink or remain stable rather than track production feature parity.

## Native source organization

The native bootstrap/runtime code intentionally favors a **small number of obvious entry points** without forcing unrelated subsystems into a single multi-thousand-line file.

Runtime code is split into normal translation units by responsibility:

- `src/runtime/runtime.cpp` is the small runtime entry/anchor file.
- `src/runtime/tls.cpp`, `objects.cpp`, `async.cpp`, `core.cpp`, `files_process.cpp`, `network.cpp`, and `platform_threads_crypto.cpp` are real independent translation units.
- `src/runtime/runtime_internal.hpp` contains only the shared private runtime types/helpers required across those translation units.

The largest bootstrap compiler entry files stay compact, while implementation that intentionally shares one stateful translation unit is grouped in conventional private `.hpp` files under `detail/`:

- `src/bootstrap/compiler/semantic/semantic_analyzer.cpp` keeps the semantic-analysis context while `semantic/detail/` groups type/trait resolution, ownership, comptime evaluation, module/function analysis, statements/expressions, and generic materialization.
- `src/bootstrap/compiler/lowering/hir_to_mir/hir_to_mir.cpp` keeps the MIR-lowering entry point while its detail headers contain the stateful lowering machinery and module orchestration.
- `src/bootstrap/tools/raz/main.cpp` remains the executable entry point while `tools/raz/detail/` groups CLI options, build/project commands, LSP semantics/server code, auxiliary commands, and dispatch helpers.

There are no `.inc` implementation files. New files should be created only for a clear responsibility boundary; line count alone is not a reason to split a cohesive implementation.

## Incremental workspace and build pipeline

Raz's project driver and LSP share a persistent module graph rather than maintaining
separate dependency models. The graph is stored as `.raz/cache/workspace-v1.state`
and records module source/import fingerprints plus forward and reverse dependency
edges. A changed module therefore produces one transitive dirty set for both command
line builds and editor invalidation.

Ordinary modules use dependency-sensitive build fingerprints. A module fingerprint is
based on its source, import shape, target/profile settings, and the public interface
fingerprints of the modules it actually imports. Package-wide interface closure is
retained only where exported generic bodies can depend on package-private helpers.
This prevents unrelated public declarations elsewhere in the package from forcing a
recompile.

Each compiled module also writes `raz-incremental-stages-v1` metadata containing
source, import, interface, semantic, HIR, MIR, Forge IR, and final build fingerprints.
These stage keys are the stable invalidation contract for deeper cache reuse.

For native builds, ordinary packages emit one cached native object per module from the
cached Forge IR. Independent object emission is parallel and controlled by `--jobs`.
Before object emission, Raz canonicalizes duplicate generated definitions, assigns
each equivalent specialization/helper to one deterministic owner module, and rewrites
non-owner copies as external declarations. This keeps generic-heavy packages on the
per-module object path on every host without relying on linker-specific multiple-
definition behavior. Native object cache state records both the Forge-IR input
fingerprint and the emitted object-content fingerprint. Objects are emitted through a
temporary file and are not replaced when regeneration produces identical bytes. The
final link is content-addressed over module object bytes, dependency artifacts, runtime/
Forge/OpenSSL native libraries, and the exact linker command/tool selection. Stable
native inputs reuse cached content digests from `native/link-inputs.state`; metadata
changes trigger a byte rehash but do not force a relink when contents are unchanged.
Static `.a`/`.lib` outputs use the same content-addressed policy and remain untouched
when their module objects are unchanged. A cached aggregate object remains only as a
conservative fallback for genuinely conflicting or non-equivalent duplicate definitions.


## Incremental native specialization ownership
## Generic specialization ownership

Per-module native objects remain the default even when generic-heavy modules independently materialize the same generated specialization. Before native object emission, the project driver canonicalizes generated Forge IR functions, compares duplicate bodies structurally, and chooses one deterministic owner using stable module-source ordering. Equivalent non-owner copies are rewritten as external declarations.

This preserves ordinary strong-definition diagnostics: same-named functions with different bodies, internal definitions, globals, or any duplicate that cannot be proven equivalent are not silently coalesced and retain the safe aggregate fallback. A fresh build can therefore reuse every module object and the final link even for large generic applications without relying on linker-specific multiple-definition flags or platform-specific COMDAT behavior.


## MIR analysis and ownership firewall

The self-host compiler routes executable semantics through a verified MIR pipeline before Forge, LLVM, RXE, WASM, or interpreter consumers run. MIR owns a basic-block graph, reusable dataflow sets, use/last-use information, dominance queries, and explicit place/reference/drop facts. HIR remains the authority for source-level borrow legality, while MIR analyses provide backend-neutral control-flow and ownership facts; native backends do not implement ownership policy.

MIR transforms preserve instruction-index identity where required: equal-target conditional branches are canonicalized and literal scalar operations are folded in place. Transformations that physically remove instructions use a single remapping transform so instruction-index register identities remain valid.
