# Optimizer and analysis

Forge 1.3 provides reusable analyses and deterministic scalar optimization passes for frontend authors and compiler integrations.

## Core analyses

`forge::analysis::FunctionAnalysisManager` caches and invalidates:

- control-flow graphs and reachability,
- use/definition information,
- dominator trees,
- conservative pointer-origin alias analysis,
- natural-loop information.

Analysis invalidation is dependency-scoped. Operation-only rewrites invalidate use/definition and alias state while preserving CFG, dominator, and loop caches. Terminator-only CFG rewrites may report touched source blocks; when the block set and reachability stay stable, predecessor/successor buckets are repaired locally and dominators are updated from the changed successor frontier. Reachability changes and structural block insertion/removal conservatively fall back to whole-function CFG-derived analysis rebuilds. Cache invalidation is lazy: storage is retained until an analysis is requested again, allowing repeated scalar cleanup rounds to reuse hash-table capacity instead of reallocating it. Passes that do not declare a mutation scope are conservatively treated as invalidating all analyses.

### Alias model

Pointers are classified as stack, global, argument, or unknown origins. Constant `ptr.offset` and `field.address` operations retain a precise byte offset. Alias queries return `no_alias`, `may_alias`, or `must_alias`.

Forge proves distinct stack allocations and distinct globals do not alias. Different pointer arguments remain conservative and may alias. Unknown pointer operations also remain conservative.

```cpp
forge::analysis::FunctionAnalysisManager analyses(function);
const auto left = analyses.aliases().location("%left", 8);
const auto right = analyses.aliases().location("%right", 8);
const auto relation = analyses.aliases().alias(left, right);
```

## Scalar stack promotion

`ScalarStackPromotionPass` removes non-escaping scalar stack slots within a basic block and across arbitrary reachable control flow, including loops. Cross-block promotion computes immediate dominators and iterated dominance frontiers, prunes block-parameter placement with slot liveness, and performs a dominator-tree SSA rename. Loop latches therefore pass the value current at the end of the latch into explicit loop-header block parameters instead of relying on iterative predecessor-value guessing.

The construction is regression-tested with acyclic joins, counted loops, conditional loop updates, and nested loops; every promoted fixture is compared through the interpreter to preserve pre-optimization semantics.

## SSA merge simplification

`MergeParameterSimplificationPass` treats Forge block parameters as SSA phi nodes and removes trivial merges to a fixed point. Incoming values are canonicalized through earlier simplifications, self-referential loop edges are ignored when a single external defining value exists, and predecessor edge-argument lists are shortened in lockstep with removed parameters. This collapses identical incoming-value joins, phi-of-phi forwarding chains, and loop forms such as `header(%x) <- preheader(%initial), latch(%x)` without disturbing non-trivial cyclic phis.

The pass runs immediately after scalar stack promotion and again inside the scalar cleanup fixpoint, allowing SCCP, CSE, copy propagation, DCE, and CFG simplification to consume the reduced SSA graph in the same optimization level. Interpreter regressions cover both branch-path forwarding and self-referential loop-phi elimination.

## SSA-aware sparse conditional constant propagation

`SparseConditionalConstantPropagationPass` tracks both an SSA value lattice (`unknown`, `constant`, `overdefined`) and executable CFG edges. Block parameters meet values only from executable predecessor edges, so constants created before a branch can remain constant through mem2reg phi/block parameters even when multiple runtime paths reach the join. A constant block parameter is materialized as a local `const` after its incoming edge arguments are removed, preserving SSA dominance while exposing the fact to later passes.

Constant branch conditions mark only the taken edge executable. After the sparse fixed point, SCCP rewrites proven constant operations, folds proven branches, removes unreachable blocks, and immediately runs merge-parameter simplification. This lets dead predecessor removal turn multi-input phis into single-input/trivial phis in the same cleanup wave. Regression coverage includes both a dynamic first branch whose two executable paths carry the same constant through distinct block parameters and an edge-sensitive case where one potential predecessor is killed by an inner constant branch before the join fact is computed.

`SimplifyCFGPass` then collapses straight-line continuations when a block has exactly one predecessor and that predecessor reaches it with an unconditional jump. The continuation's block parameters are substituted with the unique predecessor edge arguments, the jump is removed, and the continuation operations are spliced into the predecessor. Entry blocks, self-loops, conditional edges, and multi-predecessor joins are deliberately excluded. This turns SCCP-created single-predecessor joins into ordinary straight-line SSA immediately, exposing further CSE/DCE opportunities without duplicating blocks.

`BranchThreadingPass` performs edge-specialized jump threading without cloning operations. For a tiny pure predicate block (at most three scalar operations), each incoming edge is evaluated independently using constants already available on that edge. When the condition is provably true or false, and every value required by the selected successor is already available at the predecessor, the predecessor edge bypasses the predicate block directly. The pass refuses blocks with side effects or unsupported/trapping operations, and it also refuses any block whose parameters or local SSA results are used directly outside that block; outward dataflow must travel through explicit successor arguments. This keeps threading dominance-safe and prevents code growth while still eliminating branch diamonds that global SCCP cannot fold.

## Memory forwarding

`MemoryForwardingPass` tracks known values within a basic block. It:

- forwards a stored SSA value into a later load from the same exact location,
- removes repeated loads from an unchanged location,
- preserves facts across writes proven not to alias,
- invalidates facts at calls, unknown memory effects, and possible aliases.

## Loop information and LICM

Natural loops are identified from dominator-backed CFG backedges. Each loop records its header, latch, block set, and unique preheader when one exists.

`LoopInvariantCodeMotionPass` hoists non-trapping operations from canonical loop headers when every SSA operand is defined outside the loop or was already proven invariant. It does not hoist loads, calls, division, remainder, or operations with side effects.

`LoopInvariantGuardHoistingPass` performs a non-duplicating form of loop unswitching for canonical natural loops whose header contains only a branch on a loop-invariant value. With a unique preheader and exactly one in-loop successor, the invariant decision moves to the preheader and the loop header becomes an unconditional jump along the selected loop arm. Exit payloads are remapped through the preheader's initial header arguments, so the transform is accepted only when every value needed by the bypassed exit already dominates the preheader. Loop-variant predicates, self-loop headers, ambiguous/multi-arm loop exits, and cases requiring cloned operations remain unchanged.

This removes a repeated invariant branch from every loop iteration without duplicating the loop body. Interpreter regressions cover both outcomes of a runtime invariant flag and verify that a genuinely induction-dependent header predicate is not hoisted.

## Broad scalar cleanup

Forge canonicalizes commutative integer expressions and inverse comparison predicates for common-subexpression elimination. For example, `a < b` and `b > a` share one dominating predicate. Scalar cleanup simplifies neutral or self-canceling operations, including `x - x`, `x ^ x`, `x & x`, `x | x`, integer division/remainder by one, multiplication by negative one, double `neg`/`not`, all-bits `and/or`, XOR with all-bits-one, and redundant integer selects. Integer self-comparisons are folded when their result is independent of runtime data.

Dominance-aware CSE also reuses repeated pure address calculations (`global.address`, `ptr.offset`, and `field.address`) and identical integer selections. This reduces duplicated address arithmetic and merge work without forwarding loads or crossing memory side effects.

At `-O2` and `-O3`, Forge runs a second SCCP/algebraic/CSE cleanup wave after memory promotion, if-conversion, and merge simplification. This exposes optimization opportunities created by earlier structural passes instead of requiring each transform to duplicate cleanup logic.

## Standard pipelines

- `-O0`: no optimization passes.
- `-O1`: constant folding, algebraic simplification, copy propagation, DCE, and CFG simplification.
- `-O2`: adds CSE, alias-aware memory forwarding, and CFG simplification.
- `-O3`: adds LICM and another aggressive scalar cleanup cycle.
- `-Os` and `-Oz`: retain code-size-oriented pipelines.

Pass reports remain available through `forge-opt --stats --pass-timing` and `forge compile --pass-stats`.

### Constant-trip loop unrolling (`-O3`)

Forge eliminates small canonical counted loops when the initial value, limit, and positive stride are compile-time constants. The transform is limited to side-effect-free bodies of at most eight operations and at most eight iterations. Dynamic loops, trapping operations, calls, memory accesses, irregular exits, and larger loops remain unchanged. This keeps code growth predictable while exposing the unrolled body to SCCP, algebraic simplification, copy propagation, and dead-code elimination.

## Incremental use-def repair

Operation-only passes can report touched blocks. Materialized use-def information is repaired by subtracting and rescanning only those block contributions; unannotated or structural rewrites conservatively fall back to whole-function invalidation.
