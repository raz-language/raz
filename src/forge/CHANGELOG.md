## Unreleased
- Added full-width AArch64 Q-register allocation/spilling: call-local vectors use `v16`-`v23`, while 128-bit vectors live across calls receive 16-byte aligned spill homes because AAPCS64 preserves only the low 64 bits of `v8`-`v15`.
- Added native `i32`/`i64` contiguous add reductions using NEON `ADDV`/`ADDP`, explicit packed chain/postfix-DAG execution, and automatic reduction recognition from ordinary contiguous scalar load/add trees.
- Fixed AArch64 machine-optimizer alias resolution ordering so rewritten operands are resolved before virtual-register compaction, preventing a compacted ID from being interpreted through a stale alias table.
- Added vector pressure, vector-live-across-call, reduction-result identity, chain, DAG, and native-instruction regression coverage.
- Began AArch64 Advanced SIMD/NEON lowering for 128-bit packed integer map pseudos (`i32`/`i64` in-place, scalar-map, two-source map, and three-source chained map), with scalar tail handling and native vector-operation accounting.
- Made packed-vector verifier diagnostics target-neutral and corrected AArch64 immediate-selection stats aggregation.

### AArch64 native backend

- Added deterministic Mach-O arm64 relocatable object emission with Darwin leading-underscore symbols, `LC_BUILD_VERSION`, `LC_SYMTAB`/`LC_DYSYMTAB`, `__TEXT,__text`, `__DATA,__data`, and native `__thread_data`/`__thread_vars` TLV descriptors.
- Added Darwin arm64 `BRANCH26`, `PAGE21`, `PAGEOFF12`, `TLVP_LOAD_PAGE21`, `TLVP_LOAD_PAGEOFF12`, and `UNSIGNED` relocation emission plus `__tlv_bootstrap` TLS calls.
- Added `--format=macho`, `forge-codegen --emit-macho`, and C API v16 `FORGE_ABI_DARWIN_ARM64`; Apple AArch64 auto-format now selects Mach-O.
- Upgraded the AArch64 allocator with copy coalescing, CFG-hole-aware physical-register recovery, spill-slot coloring/reuse, and frame-byte savings accounting.
- Added native AArch64 ADD/SUB/CMP/shift immediate selection with dead constant-materialization elision and changed scalar encoding to use allocated registers directly.
- Added a correctness-first AArch64 machine encoder and deterministic ELF64 `EM_AARCH64` object writer with AAPCS64 scalar calling, stack overflow arguments, direct/indirect calls, aggregate register returns, x8 indirect results, global/function address relocations, and Linux initial-exec TLS.
- Added AAPCS64 aggregate classification including one-to-four-member homogeneous `f32`/`f64` aggregates and explicit register-piece widths.
- Made machine lowering target-aware so AArch64 cross-compilation cannot inherit the host x86 ABI. AArch64 currently bypasses the x86-specific machine-optimizer pseudo forms until instruction selection and allocation are target-aware.
- Added `forge compile --arch=aarch64`, AArch64 support to `forge-codegen`, and C API v16 `FORGE_ABI_AAPCS64`/`FORGE_ABI_DARWIN_ARM64` object emission/classification.
- Added AArch64 encoder, ELF relocation, TLS, >8-argument AAPCS64, deterministic-object, and C-API regression coverage.
- Added a native AArch64 linear-scan register allocator using AAPCS64 callee-saved `x19`-`x28` and `v8`-`v15`, deterministic spill slots, frame save/restore emission, and register-pressure regression coverage.
- Added an AArch64 canonical machine-combine pass for copy propagation, zero-offset pointer canonicalization, and redundant-width-cast cleanup while keeping x86-only pseudos isolated.

- Added non-duplicating edge-specialized branch threading for tiny pure predicate blocks. The pass evaluates conditions per predecessor edge, bypasses the shared control block only when successor dataflow is dominance-safe, and rejects side effects, trapping/unsupported operations, escaping block-local SSA values, and any case that would require code cloning.
- Extended SCCP/CFG cleanup with edge-sensitive predecessor elimination and safe straight-line block merging. `SimplifyCFGPass` now substitutes single-predecessor block parameters from unconditional jump arguments and splices the continuation into its predecessor, with interpreter regression coverage proving a phi becomes constant only after an impossible predecessor edge is removed.
- Upgraded SCCP to an SSA-aware executable-edge/value-lattice solver: constants now propagate through block parameters, impossible branch edges are pruned, constant phis are materialized locally, dead blocks are removed, and newly trivial merges are simplified immediately. Added an interpreter-backed regression where a runtime branch feeds the same constant through distinct phi inputs and a downstream branch folds away.
- Marked three dormant x86-64 encoder helpers `[[maybe_unused]]`, keeping the strict `-Werror` standalone build and Windows host build warning-free without changing code generation.
- Replaced the temporary backedge restriction in `ScalarStackPromotionPass` with liveness-pruned dominance-frontier mem2reg: loop-carried scalar slots now receive real block parameters and dominator-tree SSA renaming, with predecessor edge arguments wired from the value current at each latch. Added counted-loop, conditional-loop, nested-loop, and acyclic-join interpreter regressions before re-enabling loop promotion at `-O2`.
- Strengthened `MergeParameterSimplificationPass` into a fixed-point SSA cleanup: identical incoming block parameters, self-plus-single-external loop phis, and phi-of-phi chains are canonicalized and removed while predecessor edge arguments stay structurally aligned. The pass now runs directly after mem2reg and inside scalar cleanup so SCCP/CSE/DCE immediately see the simplified SSA graph.
### Optimizer correctness

- Temporarily restricted cross-block stack promotion around CFG backedges after the previous iterative predecessor-value construction was shown to create stale loop state and make the optimized production Raz LSP spin indefinitely at `-O2`; the dominance-frontier implementation above now supersedes that temporary restriction.
- Restored the target ABI/data-layout tests to source packages and extended the workspace packager so Forge's legitimate `tests/target/` directory is preserved alongside `src/target/` and `include/forge/target/`.

### Compile throughput

- `PassManager::run` no longer delegates to `run_with_report`. Production
  compilation ran the reporting path, which timestamps every pass and appends a
  record per function/pass pair; none of that is observable from `run`. Scoped
  analysis invalidation is unchanged — only the telemetry is skipped.
- `forge compile` verifies once at the optimized-IR boundary instead of
  re-verifying the whole module after every pass. `forge opt` keeps per-pass
  verification for diagnosing which pass broke the IR.
- Measured on `benchmarks/broad/kernels.fir` at `-O3`: 191 ms to 44 ms
  (best of five, same host), with byte-identical object output.

### Build

- Restored `tests/target/data_layout_tests.cpp` and
  `tests/target/abi_classification_tests.cpp`. `CMakeLists.txt` referenced both,
  but neither was present, so the project could not configure at all and CI
  could never have run.
- Added `is_power_of_two`, `is_aligned`, `checked_align_to`, and `align_to` to
  `forge/target/data_layout.hpp`. `checked_align_to` reports overflow rather
  than wrapping, so a hostile or generated alignment cannot silently place a
  field before the start of its aggregate.

## C API v14 / binary IR v24

- Added first-class thread-local globals and `tls.address` through IR, verifier, interpreter, machine lowering, x86-64 encoding, C API, and binary serialization.
- ELF64 uses `.tdata` and `R_X86_64_GOTTPOFF`; COFF x64 uses `.tls$AAA`, `_tls_index`, and `SECREL`.
- Incremental native artifacts preserve TLS fixups and JIT external resolution fails explicitly rather than executing unresolved TLS placeholders.
- Fixed callback global parsing when array extents are lexed with the symbol name.

# Changelog

- Added non-duplicating loop-invariant guard hoisting for canonical natural loops: a runtime-invariant header branch can move to the unique preheader while the loop header becomes an unconditional in-loop jump, with strict dominance/exit-payload legality checks and interpreter coverage for both guard outcomes.
- C API v13 adds aggregate-element named arrays (`struct @T[N]` / `array @A[N]`) with recursive layout, ABI classification, verifier, printer/parser, and binary IR v23 support.
All notable changes to Forge are documented here. Forge follows [Semantic Versioning](https://semver.org/).

## 2.0.0 - 2026-07-29

- C API v13 adds structured aggregate/global/function/block/operation construction with exact caller-owned names, rich value metadata, target features, operands/successors, alignment, source ranges, and attributes.
- C API v11 added in-memory IR parsing, optimization-pipeline control, and COFF/ELF object emission for embedded compiler backends.

### Compiler and optimizer

- Added bounded scalar-cleanup fixpoint iteration at `-O2` and `-O3`.
- Added loop-capable mem2reg, cross-block scalar promotion, global load forwarding, and local/global alias-aware dead-store elimination.
- Added generalized scalar-evolution reduction, conservative constant-trip unrolling, LICM hardening, merge-parameter simplification, and SSA diamond if-conversion.
- Added dominance-aware and commutative CSE, predicate canonicalization, address-expression CSE, extended algebraic identities, and power-of-two unsigned division/remainder strength reduction.
- Added broad x86 multiplication strength reduction for powers of two and scaled `LEA` families.

### Backend and register allocation

- Expanded integer allocation to nine registers with ABI-safe entry capture and selective callee-saved use.
- Added cycle-correct shared parallel-copy scheduling for SSA edges, function entry, and outgoing integer/XMM call arguments.
- Added loop-edge affinity, destructive induction coalescing, direct physical copies, `xchg` swap lowering, hot-loop layout, and arithmetic-flags branch fusion.
- Added direct arithmetic, compare, `TEST`, `CMOV`, immediate, stack-memory, and return-value lowering improvements.
- Added frameless call-containing functions when no frame-resident data is required.

### Calls and floating point

- Removed blanket integer and floating argument snapshots in favor of cycle-safe ABI placement.
- Kept floating call arguments register-resident when they do not cross calls.
- Added direct call-return forwarding and call-result forwarding into arithmetic and subsequent calls for integer and floating values.
- Added register-resident floating entry arguments and direct XMM constant materialization.

### Correctness, validation, and release engineering

- Fixed LICM terminator corruption, multi-register parallel-copy rotation, destructive recurrence coalescing, comparison-CSE typing, and XMM entry hazards.
- Fixed Clang/MSVC Windows test-build portability by including `<algorithm>` explicitly where standard algorithms are used.
- Expanded differential performance coverage to 16 independent Forge-versus-LLVM workload families with semantic checks, code-size accounting, and unchanged per-kernel gates.
- Added strict 66-test release validation, sanitizer-safe gates, deterministic ELF/COFF coverage, installed C/C++ consumer gates, and package checksums.
- Unified production driver commands: `forge inspect`, `forge explain`, and `forge doctor`.
- Finalized native aggregate ABI parameters and returns for System V AMD64 and Windows x64.

## 1.10.0 - 2026-07-27

- Added executable native aggregate return lowering for explicit C, System V AMD64, and Windows x64 calling conventions.
- Added INTEGER returns through RAX/RDX and SSE returns through XMM0/XMM1, including mixed INTEGER/SSE System V aggregates.
- Added caller-side reconstruction of register-returned aggregates into aligned Forge aggregate storage.
- Preserved hidden result-buffer lowering for ABI-indirect and larger aggregate returns.
- Added bidirectional native C++ interoperability coverage for integer and mixed floating-point aggregate returns.
- Corrected SSE pointer encoding for extended x86-64 base registers used by aggregate return reconstruction.

## 1.9.0 - 2026-07-27

- Added executable native by-value aggregate parameter lowering for System V AMD64 and Windows x64.
- Small register-classified aggregate parameters are exploded into ABI integer/SSE pieces at call sites and reconstructed in callee-local storage.
- Large and Windows-indirect aggregates preserve the existing pointer-based ABI path.
- Added bidirectional native C++ interoperability coverage: native-to-Forge and Forge-to-native by-value struct calls.
- Preserved hidden-result-buffer aggregate returns; direct aggregate return registers remain the final ABI-lowering item.

## 1.8.0 - 2026-07-27

- Added segmented-interference-aware global copy affinity coalescing across block boundaries and liveness holes.
- Added recovery of copy destinations that were unnecessarily stack-backed when the source physical register is globally safe.
- Added allocator metrics for global copy affinities and recovered copy spills.

## 1.7.0 - 2026-07-27

- Added critical-edge live-range splitting for call paths that converge with other predecessors.
- Added explicit split-edge blocks with post-call reloads.
- Added SSA merge repair through machine block parameters and predecessor edge arguments.
- Added allocator metrics for critical-edge split values, edge blocks, and merge parameters.
- Added strict regression coverage for mixed call/non-call control-flow convergence.

## 1.6.0 - 2026-07-27

- Added conservative CFG-wide call-boundary splitting into single-predecessor continuation blocks.
- Added cross-block transition accounting while preserving critical-edge safety.

## 1.5.0 - 2026-07-27

### Added

- True transition-based live-range splitting around calls.
- Explicit register-to-stack stores before high-pressure call boundaries.
- Explicit stack-to-register reloads into new post-call virtual registers.
- Independent allocation of pre-call and post-call live-range pieces.
- Split-transition statistics in `forge-codegen --stats`.
- A regression proving five call-crossing values are reduced to two callee-saved intervals with three balanced store/reload transitions.

### Changed

- Floating values live across calls are split when their remaining uses stay in the same block.
- Integer values beyond the two available callee-saved allocation registers are split conservatively at call boundaries.
- Split slots are reserved in the function-local frame and participate in normal frame alignment.

### Safety

- The first transition splitter is deliberately limited to same-block post-call uses. Values live through CFG successors remain on the existing conservative callee-saved/spill path until edge-copy splitting is introduced.

All notable changes to Forge are documented here. Forge follows [Semantic Versioning](https://semver.org/).

## 1.4.0 - 2026-07-27

### Added

- Segmented machine live intervals that retain disjoint per-block liveness regions.
- Exact interference-edge analysis derived from segmented liveness.
- Hole-aware register recovery for mutually exclusive CFG paths.
- Allocator statistics for segmented intervals, live-range holes, interference edges, and recovered registers.
- A permanent interleaved-branch regression proving false bounding-range spills are recovered.

### Changed

- Register-pressure measurements now use real liveness segments rather than one start/end bounding interval.
- Spilled values are reconsidered after linear scan and may reuse a physical register when no segmented interference exists.

## 1.3.0 - 2026-07-27

### Added

- Conservative pointer-origin and alias analysis for stack allocations, globals, pointer arguments, copies, and constant pointer offsets.
- Public natural-loop discovery with canonical latch, header, block-set, and unique-preheader information.
- Alias-aware memory forwarding that removes redundant loads and forwards stored values across unrelated memory writes.
- Safe loop-invariant code motion for non-trapping operations in canonical loop headers.
- `memory-forwarding` and `licm` passes in the standard `-O2`/`-O3` pipelines.
- Regression coverage for overlapping ranges, disjoint stack objects, call invalidation, redundant loads, and loop semantics.

### Changed

- `-O2` now includes alias-aware memory forwarding.
- `-O3` now performs loop-invariant code motion before CSE and memory forwarding.

## 1.2.0 - 2026-07-27

### Native ABI support

- Added public System V AMD64 and Windows x64 aggregate ABI classification.
- Added integer, SSE, memory, and indirect aggregate classes.
- Added per-function ABI summaries covering register use, stack bytes, variadic state, and aggregate parameters.
- Added function calling-convention metadata for platform, C, System V, Windows x64, and fast conventions.
- Added variadic, internal, weak, and hidden symbol metadata to textual and binary Forge IR.
- Added C API v10 setters and deterministic function ABI JSON.

### Native libraries

- Added deterministic static archive generation for ELF64 and COFF AMD64 objects.
- Added a real archive symbol index accepted by native linkers.
- Added GNU long-member-name support and deterministic archive metadata.
- Added `forge archive create` for `.a` and compatible `.lib` archives.
- Added `forge link-shared` for host-toolchain shared-library linking.
- Added native static-library and shared-library link-and-run release gates.

### Quality

- Added ABI classification, signature metadata, binary round-trip, archive determinism, corrupt-object, and native library tests.
- Increased the strict release matrix to 63 tests.
- Preserved repository-wide Apache-2.0 licensing headers and the source hygiene gate.

### Current boundary

Forge exposes native ABI classification for frontend lowering decisions. Named aggregates in the current machine-code pipeline remain represented through pointer or hidden-result-storage lowering; register-classified by-value aggregate code generation remains planned work.

## 1.1.0 - 2026-07-27

### Frontend Integration API

- Added reusable source management, structured diagnostics, nested scopes, symbols, semantic declarations, and safe control-flow builders.
- Added `forge new-language` project scaffolding.
- Added repository-wide Apache-2.0 SPDX headers with Copyright 2026 Mario Vinciguerra.
- Added a repository hygiene gate that rejects unlicensed maintained source and build files.
- Fixed IRBuilder insertion-point lifetime by storing stable block handles across block-vector growth.
- Added MiniLang, a complete educational frontend showing source text through lexer, parser, AST, semantic lowering, Forge IR verification, interpretation, source maps, and x86-64 JIT execution.

## 1.0.0 - 2026-07-27

- Established the stable public compiler-core, frontend SDK, interpreter, x86-64 backend, object writers, incremental build pipeline, and release-quality contract.
- Added deterministic ELF64 and COFF AMD64 output, JIT/interpreter differential testing, cache-aware native linking, installed-package consumers, fuzz-smoke tests, and professional open-source documentation.

## Earlier releases

Before 1.0, Forge developed its verified IR, interpreter, JIT, backend optimizations, object writers, frontend SDK, and incremental build system through pre-release revisions. That history is intentionally consolidated here so the public changelog begins with the stable 1.0 API and support contract.
