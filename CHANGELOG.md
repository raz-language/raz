# Changelog

### Official GitHub-backed package registry

- Made `raz-language/packages` the zero-configuration official Raz package registry, fetched from its raw `main` index over HTTPS.
- Added shorthand `raz add <package>` and `raz add <package>@<constraint>` while retaining explicit aliases, path dependencies, private registries, mirrors, and offline package-store reuse.
- Changed ordinary `raz publish` to prepare an immutable `.raz-publish/packages/<name>/<version>.dpk` GitHub submission; explicit HTTP/HTTPS and filesystem registries retain direct publishing.
- Excluded `.raz-publish/` from deterministic package trees so repeated submission generation remains byte-stable.
- Added official-registry regression coverage and extended registry publishing/package-store tests for shorthand resolution and repeatable GitHub submissions.

- Added stable module identities and module-owned HIR/MIR function views as the foundation for module-granular incremental serialization and code generation.

### Persistent self-host incremental build state

- Added package-local `.raz/cache` persistence to the Raz-written compiler driver.
- Added exact whole-source/backend build keys and completed-artifact restoration for unchanged normal builds.
- Exported physical-module source/interface fingerprints from HIR beyond `HirBuilder` lifetime and persist them across compiler processes.
- Kept check, test, and run commands on the full compiler path rather than allowing artifact-cache short circuits.
- Added a versioned cache contract and `scripts/check-persistent-incremental.py`.

### Incremental semantic query invalidation and module fingerprints

- Replaced global-revision cache validity with semantic-input-family invalidation for traits, methods, aggregates, generic templates, functions, and associated items.
- Added exact query invalidation with reverse dependency propagation so only affected cached parents are discarded transitively.
- Added deterministic physical-module source and exported-interface fingerprints for project-level incremental reuse.
- Kept the semantic revision as telemetry/sequencing rather than a global cache-flush condition.
- Added `compiler/src/hir/query/invalidation.rz`, `compiler/src/hir/query/fingerprints.rz`, and `scripts/check-query-invalidation.py`.
- Consolidated phase-oriented query/MIR notes into enduring `docs/SEMANTIC-QUERIES.md` and `docs/MIR.md` architecture references.

### Phase 3C canonical semantic identities and database revisions

- Added exact-byte `SymbolId` interning and complete structural `TypeId` interning for semantic query identity; hashes are lookup accelerators only.
- Migrated inherent-method queries to `(receiver TypeId, SymbolId)` keys and enabled collision-safe negative method caching.
- Migrated trait implementation and trait-bound queries to canonical TypeId keys; revision synchronization now makes negative bound caching safe across semantic mutation.
- Migrated concrete associated-type normalization to `(base TypeId, TraitId, SymbolId)` keys and revision-safe negative results.
- Added semantic database revision stamping to every shared query-cache entry; declaration/materialization-table changes advance the revision and invalidate stale results.
- Added `compiler/src/hir/query/symbols.rz`, `compiler/src/hir/query/types.rz`, and `scripts/check-query-identities.py`; the compiler now contains 77 semantic Raz modules while Stage 0 remains frozen.

### Phase 3 generic query migration and canonical semantic request IDs

- Added canonical monomorphization request interning with exact packed generic arguments; fingerprints are lookup accelerators only and cannot define cycle identity.
- Migrated generic function, struct, and enum instantiation to shared query wrappers with memoization, cycle detection, dependency recording, epoch validation, and uncached materializer bodies.
- Migrated concrete associated-type normalization to the shared query engine using stable trait-local associated-type ordinals instead of source offsets.
- Removed dedicated generic function/struct/enum result caches and dedicated associated-type result caches from `HirBuilder`.
- Added `compiler/src/hir/query/identity.rz` and `scripts/check-query-generics.py`; the compiler now contains 75 semantic Raz modules while Stage 0 remains frozen.

### Phase 3 query-driven semantic engine foundation

- Added one self-hosted semantic query runtime with stable numeric keys, two semantic epochs, memoization, cycle detection, dependency-edge recording, and query-kind invalidation.
- Migrated concrete trait-implementation lookup and recursive trait-bound solving off their dedicated cache/recursion-guard storage.
- Migrated inherent-method resolution to query-backed positive caching with authoritative name-byte revalidation.
- Migrated aggregate layout and field-offset computation to shared two-word query results; nested layout queries now record dependency edges.
- Removed the obsolete dedicated trait, method, layout, and field-offset cache arenas from `HirBuilder`.
- Added `scripts/check-query-engine.py` and kept the frozen Stage 0 semantic manifest unchanged.

### MIR Phase 2 complete: verified ownership, reborrow provenance, and canonical backend firewall

- Closed the MIR 2.x architecture with 72 semantic Raz compiler modules and no Stage 0 semantic growth.
- Added explicit parent-loan provenance for reborrows; child loans inherit canonical referent paths instead of disappearing at dereference boundaries.
- Added nested reborrow ancestry handling, shared-to-exclusive rejection, parent/child lifetime containment, and provenance validation.
- Made `verify_mir_ownership_semantics` the canonical backend legality firewall before optimization and again before Forge/LLVM code generation.
- Preserved HIR ownership checks as early diagnostics while requiring MIR to independently prove executable ownership legality.
- Added `scripts/check-mir2-final.py` and wired the final Phase-2 contract into normal verification.
- Kept the frozen Stage 0 semantic manifest unchanged; the remaining native Forge package audit failure is the pre-existing absence of target ABI/layout source files, not a MIR regression.

### MIR 2G borrow regions and projection-aware loan conflicts

- Added backend-invisible shared/exclusive borrow events on the same canonical MIR ownership paths used by moves and reinitialization.
- Added CFG active-loan dataflow with conservative predecessor joins and projection-aware shared-vs-exclusive conflict checks.
- Added non-lexical loan expiration derived from the last MIR use of a reference value or its stored reference-local holder.
- Moves and reinitialization now conflict with overlapping active loans inside the MIR ownership firewall.
- Added `scripts/check-mir2g-borrow-regions.py` and verification wiring; Stage 0 semantic files remain frozen.

### MIR 2F projection-aware partial moves

- Extended backend-invisible MIR ownership events with canonical root-slot plus static field-projection paths.
- Added projection overlap semantics: ancestor/descendant moves conflict while disjoint sibling fields remain independently usable.
- Added CFG MAY-moved propagation for projected paths and subtree clearing for whole-local or exact field reinitialization.
- Extracted HIR ownership-path lowering into `mir/ownership/lowering_paths.rz`, keeping the main MIR lowering unit below its architecture cap.
- Pinned ownership-event program points against DCE so semantic move/reinitialization events cannot drift to a neighboring instruction during compaction.
- Added `scripts/check-mir2f-partial-moves.py` and verification wiring; Stage 0 semantic files remain frozen.

### MIR 2E CFG ownership dataflow

- Added backend-invisible MIR ownership-event metadata so whole-local moves are represented semantically without introducing executable backend opcodes.
- Added a forward CFG ownership lattice with MUST-initialized and MAY-moved state, conservative predecessor joins, move/use validation, and whole-local reinitialization on store.
- Whole-local move expressions now record MIR move events when they actually consume the source; Copy-aggregate moves remain non-consuming.
- MIR instruction compaction remaps ownership-event program points alongside register identities, branch targets, function ranges, and call arguments.
- Wired path-sensitive local move verification into the MIR ownership firewall while keeping HIR as the user-facing diagnostic authority during staged migration.
- Added `scripts/check-mir2e-ownership-dataflow.py` to preserve the ownership-dataflow architecture in normal verification.



### MIR 2D CFG and scalar optimization

- Added edge-value-safe constant branch folding, unreachable-block elimination, empty-jump threading, and fallthrough jump removal in self-hosted Raz MIR.
- Extended MIR constant propagation through scalar comparisons and boolean operators.
- Added algebraic scalar canonicalization that rewrites consumers through the metadata-safe MIR value remapper and lets compacting DCE erase redundant identities.
- Added `check-mir2d-cfg-scalar.py` to keep the optimizer architecture in the normal verification gate.

## MIR 2C identity remapping and compacting optimization
- Moved semantic module ordering, module dependency snapshots, private/interface dirty classification, and per-module optimized-MIR fingerprints into the self-hosted Raz compiler; ordinary acyclic packages now schedule modules from imports instead of filesystem order.

- Added a complete `MirInstructionMap` remapping layer for instruction-indexed MIR values.
- Added atomic rewriting of register operands, branch targets, function instruction ranges, and auxiliary call/closure capture arguments during instruction compaction.
- Replaced the old metadata-overloading copy-propagation placeholder with true consumer rewriting for same-type MIR casts.
- Upgraded dead-code elimination from analysis-only to conservative, side-effect-aware compaction that iterates to a fixed point.
- Extended MIR use accounting to include asynchronous/callable register operands and the shared call/capture argument arena.
- Added a bounded DCE convergence guard and `scripts/check-mir2c-remap.py`, wired into normal verification.
- Kept Stage 0 semantics frozen; the 62-module compiler successfully bootstraps from the frozen Stage 0 seed.

## MIR 2B analysis, ownership facts, and index-stable optimization

- Added an explicit arena-backed MIR basic-block graph with successor/predecessor accounting and branch-target validation.
- Added reusable dataflow sets, CFG reachability, instruction-register use/last-use summaries, and a bootstrap-simple dominance query API.
- Added MIR-owned place, local-move, reference/borrow, and drop fact modules without duplicating the authoritative HIR borrow checker during migration.
- Strengthened structural MIR verification so every compilation builds and validates the CFG before a backend can consume it.
- Added index-stable CFG simplification and scalar constant folding to the production MIR pipeline; copy propagation and DCE now have dedicated pass boundaries, with DCE intentionally analysis-only until instruction-index remapping is introduced.
- Corrected the stale CFG classification of opcode 25: it creates a local reference in current MIR and is not a terminator.
- Added `scripts/check-mir2b-analysis.py` and verification wiring to prevent the new analysis/ownership layers from collapsing back into backend-specific logic.

## MIR 2.0 architecture

- Moved `MirModule` out of HIR and into `compiler/src/mir/core/model.rz`; HIR no longer owns backend IR storage.
- Split MIR allocation/emission primitives into `mir/core/builder.rz` and the MIR interpreter/qualification corpus into `mir/interpreter.rz`.
- Added backend-neutral CFG queries, structural MIR verification, and a single `run_mir_pipeline` boundary consumed by normal compilation before execution or code generation.
- Reduced the monolithic HIR-to-MIR lowering unit from more than 4.4k lines to about 3.4k lines without changing language semantics; its remaining expression/cleanup/statement recursion is intentionally kept together until the lowering API is made cycle-free.
- Added `scripts/check-mir2-architecture.py` and verification wiring so MIR ownership cannot drift back into HIR or bypass the production pipeline.

## Semantic compiler modules

- Removed `compiler/source-order.txt` from the canonical compiler package so normal Stage 1+ builds use the semantic package/module pipeline instead of ordered physical concatenation.
- Added explicit `raz_compiler_*` namespaces and semantic import edges across all 44 compiler modules while preserving the historical dependency closure during migration.
- Retained `compiler/bootstrap-source-order.txt` only as frozen seed/recovery metadata; bootstrap drivers materialize a stripped legacy Stage 1 view for Stage 0 without teaching the C++ seed any new language semantics.
- Added `scripts/check-compiler-semantic-modules.py` and verification wiring to prevent accidental regression back to production concatenation.
- Kept the Stage 0 semantic-freeze manifest unchanged; language/compiler evolution remains owned by Raz.

## Stage 0 semantic freeze and self-host ownership

- Froze the native Stage 0 lexer/parser/semantic/HIR/MIR/lowering seed at the last verified lifetime-region bootstrap contract.
- Removed trait/coherence feature-parity growth from the C++ bootstrap compiler; production trait evolution now lives exclusively in the Raz-written compiler.
- Added `raz-stage0-semantic-freeze` with a SHA-256 manifest so accidental Stage 0 semantic growth fails CI.
- Added `raz-selfhost-trait-solver`; nested associated-type normalization and generic trait overlap qualification now run through the Raz-written compiler rather than using Stage 0 as a semantic oracle.
- Added an active-obligation recursion guard to the Raz trait solver so recursive trait proof terminates deterministically.
- Restored the large application compile gate to its pre-regression profile after removing duplicated Stage 0 trait-resolution work.
## Trait solver and coherence hardening

- Added source-order-independent coherence checks between blanket/generic implementations and concrete positive or negative implementations.
- Added an orphan boundary preventing compiler-owned `Copy`/`Clone`/`Drop` semantics from being implemented or negated for compiler-owned primitive/structural types while preserving local-trait implementations for those types.
- Added active-obligation cycle guards to the native and self-host trait solvers so recursive proof queries terminate deterministically instead of relying on recursion depth.
- Made associated-type normalization recursive through generic arguments, allowing forms such as `Wrap<T::Trait::Item>` to canonicalize after monomorphization.
- Made trait and associated-item selection ambiguity-safe: multiple applicable candidates fail resolution instead of depending on implementation registration order.
- Added native and self-host regression coverage plus `raz-trait-solver-audit` for the strengthened solver contract.

## MIR lifetime-region inference

- Added zero-cost `borrow.bind` MIR metadata connecting reference aliases to their borrowed source projections.
- Added CFG-use-driven borrow-region inference with non-lexical region endpoints, reborrow parent constraints, loop-backedge detection, and async-suspension tracking.
- MIR verification now rejects inferred regions that outlive moved/dropped/dead source storage and rejects unsafe local borrows crossing suspension without stable frame storage.
- Reference parameters participate as caller-owned regions, allowing explicit lifetime-return relationships and parameter borrows across `await` to remain valid.

## Projection-granular MIR ownership

- Added zero-cost `place.path` MIR metadata binding physical storage projections to logical ownership paths.
- Upgraded the CFG ownership verifier from root-only state to field/index/enum-payload projection state with conservative dynamic-index overlap.
- Added exact projection reinitialization, disjoint sibling preservation, whole-value invalidation after partial moves, and projection-aware CFG joins.
- Added logical lifetime restart for enum pattern bindings and separate tagged-enum discriminant provenance.
- Added projection-focused malformed/valid MIR regressions and expanded `raz-verified-mir-audit`.

## Verified MIR ownership firewall

- Added verifier-only MIR ownership operations for storage lifetime, whole-value moves, and shared/exclusive borrows.
- Added CFG-aware MIR ownership-state propagation that rejects use-after-move/drop and double-drop paths while allowing whole-place reinitialization.
- Kept ownership markers backend-neutral and zero-cost; Forge consumes them only as verified semantic metadata.
- Added malformed-MIR regressions for use-after-drop, double-drop, CFG-joined use-after-move, and legal reinitialization after move, plus `raz-verified-mir-audit`.


## Recursive trailing-comma compatibility

- Fixed recursive self-host parsing of formatter-inserted trailing commas across function parameters, call arguments, HIR semantic reparsing, ownership analysis, generic instantiation, and trait solving; Stage 2/3/4 now converge with canonical multiline formatting.
## Windows WERROR and formatting hardening

- Made every Windows `NOMINMAX` definition guard-aware so command-line `-DNOMINMAX=1` and local headers coexist cleanly under Clang `/WX`.
- Kept `NOMINMAX` and `WIN32_LEAN_AND_MEAN` enabled globally for Windows SDK macro safety.
- Ran the canonical Raz formatter across all maintained compiler, standard-library, example, test, and embedded `.rz` sources.
- Added `scripts/format-cpp-spacing.py` to enforce a blank line between adjacent namespace-scope C/C++ function definitions without mass-reformatting expressions.
- Added permanent `raz-source-format` and `raz-cpp-spacing-format` CTest gates and wired both checks into `scripts/verify.ps1`.

## Repository hygiene and licensing

- Finalized Raz licensing under Apache License 2.0 and added root `NOTICE` attribution.
- Added compact copyright/SPDX headers to every maintained source, build, and script file.
- Added release-gating audits for license headers and repository hygiene.
- Removed stale bootstrap diagnostics, the unused generated `compiler-all.rz` aggregate, and the generated PDF documentation copy from the source tree.
- Added a root `.gitignore` covering native/compiler/package caches, bootstrap diagnostics, IDE state, and temporary artifacts.
- Packaging now refuses to omit `LICENSE`, `NOTICE`, or `AUTHORS.md`.


## Feature-oriented conformance layout

- Reorganized historical numbered `examples/phaseN` fixtures into feature-oriented directories for aggregates, ownership, comptime, reflection, effects, generics, traits, FFI, unsafe code, async, and backends.
- Renamed the hosted runtime fixture project from `phase4-runtime` to `runtime-interop` and removed numbered phase labels from CTest names and runtime fixture output.
- Added `raz-test-layout` / `scripts/check-test-layout.py` so numbered roadmap-phase naming cannot return to maintained examples or conformance tests.
- Updated stale aggregate fixtures to the frozen named-field struct syntax and taught compile-time evaluation to encode named struct literals in declaration-field order, including struct constants nested inside enum payloads and tuples.

- Upgraded `razfmt` to a width-driven structural layout engine with a 110-column target, multiline signatures/calls, boolean-condition wrapping, canonical `T&mut`, and trailing commas only for multiline parameter/argument lists.
- Added parser support and regression coverage for multiline trailing commas in both Stage-0 and Raz self-host parsing paths.
- Added `raz-formatter-layout` to verify idempotence, compact-vs-multiline decisions, boolean wrapping, and maximum line width.

## 1.0.0

Raz 1.0.0 establishes the self-hosted native compiler, standard library, package tooling, and dual-backend code-generation architecture.

### Language

- Static typing with ownership, borrowing, deterministic destruction, traits, generics, payload enums, pattern matching, closures, const evaluation, reflection, and structured async support.
- Package, module, visibility, import, and dependency semantics are enforced by the compiler.
- Backend-neutral HIR and MIR keep language behavior consistent across native backends.

### Compiler and backends

- The production compiler is written in Raz and reaches a deterministic recursive fixed point during bootstrap qualification.
- Forge is the default native backend.
- LLVM is available as an alternate backend for LLVM IR, object, and executable production.
- Query caches accelerate top-level symbols, traits, methods, associated types, layouts, lexical scopes, closure captures, and local-slot resolution.
- The compiler source is organized by frontend, HIR, MIR, backend, and driver responsibilities.

### Tooling

- `raz build`, `run`, `check`, `test`, `lint`, `fmt`, `doc`, `new`, `init`, and `clean` provide the core project workflow.
- `raz forge` and `raz llvm` expose backend-specific compilation controls.
- `raz doctor`, `backends`, `targets`, `profile`, `metadata`, and `graph` provide toolchain inspection.
- `raz add`, `remove`, `update`, `lock`, and `registry` provide deterministic package management.

### Standard library

- `std::path` now performs allocation-free lexical normalization of separators, `.` and `..` components and exposes root, stem, filename, parent, and extension primitives.
- `PathBuf` supports lexical normalization, component push/pop, filename stems, and extension replacement without filesystem access.
- `std::fs::bytes` provides owned binary file reads/writes over `ByteBuffer`, keeping allocation and buffering policy in Raz.
- `std::env::owned` adds `String`-based get/set/remove helpers, while `std::env::path` exposes an owned normalized current-directory `PathBuf`.
- `Vector<T>` and `Deque<T>` gain `contains` and `position` for `Eq` element types.
- Standard-library casts follow the canonical `value as Type` formatting rule.

### Packages

- Path dependencies and registry dependencies share one deterministic lockfile model.
- Registry constraints support exact versions, caret ranges, tilde ranges, and minimum versions.
- Registry package contents are integrity-checked before use.
- Resolved registry packages are materialized into a shared content-addressed store and may be reused in offline mode.
- Registry indexes and package archives can be fetched over HTTP or verified HTTPS with ordered mirror fallback.
- Deterministic `.dpk` archives are created and extracted in Raz and verified against the complete package-tree checksum before use.
- `raz pack` and `raz publish` support deterministic archives, filesystem registries, HTTP/HTTPS publishing, Bearer authentication, and an optional detached-signature hook for private registries.

### Platforms

- Windows x64 and Linux x86-64 are qualified for the native toolchain.
- Windows bootstrap uses the selected MSVC toolchain consistently, including linker-path quoting and an explicit compiler stack reserve.

## Unreleased

### Raz rebrand

- Renamed the language and complete toolchain from its former name to **Raz** across documentation, compiler/runtime symbols, CMake targets, scripts, tests, package metadata, schemas, caches, registry settings, and generated backend identifiers.
- Changed the canonical Raz source extension from the former `.xy` extension to **`.rz`**. Maintained `.xy` sources are rejected by repository policy and the compiler accepts `.rz` as the language source format.
- Standardized user-facing/tooling names on `raz`, `razc`, `raz.toml`, `raz.lock`, `.raz/`, `RAZ_*`, and `raz_rt_*`.
- Added `raz-rebrand` qualification to detect legacy branding in paths, ordinary text, encoded byte arrays, packed CLI strings, bytewise emitters, and obsolete source extensions.


- Eliminated the remaining Stage-1-specific native ABI: path/tree traversal and recursive filesystem policy now live in Raz, tool/platform queries use generic host primitives, and Ed25519 uses raw byte-oriented crypto primitives with arena packing/unpacking performed in Raz. `runtime.cpp` contains zero `raz_rt_stage1_*` definitions.
- Fixed pathological generic-angle lookahead that could rescan across expression/block boundaries and drive large HTTP/application parses to multi-gigabyte memory use.
- Added scoped primitive integer constants such as `i64::MAX` to semantic and MIR lowering.
- Added owned filesystem/process conveniences and additional `Vector`/`Deque` collection operations.
- Added clean terminal status output with automatic ANSI color, `--quiet`, `--color auto|always|never`, `NO_COLOR`, and `RAZ_COLOR` support.
- Upgraded compiler diagnostics with stable error codes, file/line/column locations, numbered source excerpts, precise caret/range markers, contextual help, and clean failure summaries.
- Added human-readable frontend diagnostics to the self-hosted compiler instead of relying only on numeric bootstrap status codes.

### Native runtime reduction

- Moved latch, semaphore, reusable barrier, once, task-scope, channel, cancellation, executor, async file/socket scheduling, timer scheduling, reactor policy, recursive filesystem operations, whole-file I/O composition, and socket retry loops into Raz.
- Removed the legacy fixed-arity callable/trait-object runtime and consolidated weak/type-identity support onto the compiler's erased callable/trait ABI.
- Reduced the native future surface to the compiler-facing async kernel; rich future composition is Raz-owned.
- Removed obsolete Stage-1 JIT compatibility exports; Forge remains the owner of JIT infrastructure.
- Kept native code focused on OS/ABI boundaries and compiler-required bootstrap/async ABI support.
### Self-host compiler native-boundary reduction

- Moved Stage-1 arena allocation, lifetime, scalar get/set, range equality/hash, and arena copy into the Raz-written compiler while preserving the compatible bootstrap buffer header.
- Removed the unused Stage-1 reference runtime entirely.
- Moved Stage-1 process-argument, stdio, environment, path-existence, process-launch, socket, TLS, and whole-file read/write ASCII adapters into Raz over the normal permanent runtime ABI.
- Reduced `src/runtime/runtime.cpp` from 3,808 lines in the native-runtime-reduced baseline to 3,429 lines in this phase (2,460 lines below the original 5,889-line runtime).
- Tightened the native-boundary audit to reject reintroduction of the migrated Stage-1 native surfaces.


### Diagnostics and language-server tooling

- Added a compiler-owned semantic symbol/occurrence index with exact source ranges and HIR-enriched types/signatures.
- Replaced LSP hover, completion, document/workspace symbols, definition, references, rename, signature help, document highlights, semantic tokens, and `auto` inlay hints with compiler-index-backed implementations.
- Added lexical-scope-aware identity so references/rename do not conflate shadowed locals, plus cross-open-buffer global navigation/rename.
- Expanded semantic tokens to classify functions, types, variables, parameters, fields/properties, enum members, and namespaces.

- Added `human`, `short`, and `json` compiler diagnostic formats.
- Added `raz-diagnostics-v1` and project-level `raz-project-diagnostics-v1` machine schemas.
- Project diagnostics now map generated semantic compilation units back to original `.rz` files, lines, UTF-16 columns, and byte offsets.
- Added structured diagnostic notes/help/fix-its and parser insertion fixes.
- Added ordered `--allow`, `--warn`, and `--deny` warning policy by diagnostic code/category.
- Added `raz diagnostics` and `raz-diagnostic-catalog-v1`, generated from compiler diagnostic literals.
- Replaced heuristic LSP diagnostics with the real in-memory compiler pipeline.
- Added UTF-16-correct LSP positions, compiler-backed quick fixes, full-text synchronization metadata, and diagnostic clearing on `didClose`.

## Source organization

- Split the native runtime monolith into normal responsibility-based `.cpp` translation units with a single private `runtime_internal.hpp` for shared implementation types/helpers.
- Split the native semantic analyzer into type/trait, ownership, comptime, module-analysis, statement/expression, and generic-materialization private `.hpp` sections.
- Split the native HIR-to-MIR and `raz` CLI implementations into logical private `.hpp` sections while keeping their public entry `.cpp` files compact.
- Removed `.inc` implementation files entirely and updated structural/native-boundary/diagnostic-catalog audits for the conventional `.cpp`/`.hpp` layout.

### Incremental workspace/build architecture

- Added self-hosted per-module HIR cache identities and versioned optimized-MIR cache records keyed by exact source and public-interface fingerprints.
- Added cross-process module cache-hit validation state to the Raz compiler driver so future module-local HIR/MIR deserialization can reject stale artifacts before reuse.
- Added content-addressed native object/link caching: object state now separates FIR-input and emitted-object fingerprints, byte-identical regeneration preserves the existing object, and link reuse is based on actual input contents plus linker configuration.
- Added persisted native-library content digests (`native/link-inputs.state`) so hot fresh builds avoid rehashing unchanged runtime/Forge/OpenSSL libraries while metadata churn alone cannot force a relink.
- Added content-addressed static archive reuse so unchanged `.a`/`.lib` outputs are not rewritten.
- Added persistent `raz-workspace-v1` module graphs shared by the project driver and LSP.
- Added source/import fingerprints, forward/reverse dependency edges, and transitive dirty propagation.
- Replaced package-wide module invalidation with direct dependency/interface fingerprints for ordinary modules.
- Added `raz-incremental-stages-v1` metadata covering source, semantic, HIR, MIR, Forge IR, and final build keys.
- Added cached per-module native object emission from Forge IR and a native link fingerprint/cache.
- Added bounded parallel module compiler workers and `raz --jobs <n>`.
- Added parallel native object generation for independent modules.
- Added deterministic native specialization ownership across modules: structurally equivalent shared generated functions are assigned one stable owner and non-owner copies become external declarations, keeping generic-heavy packages on the per-module object cache.
- Retained the aggregate native fallback only for genuinely conflicting or non-equivalent duplicate definitions.
