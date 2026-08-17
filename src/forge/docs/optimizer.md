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

## Memory forwarding

`MemoryForwardingPass` tracks known values within a basic block. It:

- forwards a stored SSA value into a later load from the same exact location,
- removes repeated loads from an unchanged location,
- preserves facts across writes proven not to alias,
- invalidates facts at calls, unknown memory effects, and possible aliases.

## Loop information and LICM

Natural loops are identified from dominator-backed CFG backedges. Each loop records its header, latch, block set, and unique preheader when one exists.

`LoopInvariantCodeMotionPass` hoists non-trapping operations from canonical loop headers when every SSA operand is defined outside the loop or was already proven invariant. It does not hoist loads, calls, division, remainder, or operations with side effects.

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
