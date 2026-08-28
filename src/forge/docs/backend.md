# Forge x86-64 backend

Forge lowers verified SSA IR into target-neutral machine IR before target encoding. The x86-64 backend supports System V AMD64 and Windows x64 scalar/pointer calling conventions, JIT image emission, ELF64 objects, and COFF AMD64 objects.

## Allocation pipeline

1. Number blocks and instructions deterministically.
2. Compute shared block use/def and live-in/live-out sets.
3. Build instruction-level live-after data and live intervals.
4. Apply loop-depth and use-frequency spill weighting.
5. Constrain call-crossing integer values to callee-saved registers or spills.
6. Apply copy, unary, and two-address allocation hints where call-safe.
7. Run linear scan over stable virtual-register order.
8. Color non-overlapping spills into reusable aligned stack slots.
9. Compute the final frame and save only assigned nonvolatile registers.
10. Encode abstract locations and verify all relocation/layout assumptions.

The allocator currently assigns one location to each virtual register for its complete interval. Segmented live ranges and transition copies are not yet part of the allocation model.

## Calling conventions

Call marshaling uses a shared parallel-copy planner for GPR and XMM arguments. Acyclic placements move directly, reciprocal integer cycles use `xchg`, larger homogeneous cycles use one scratch register, and only unresolved heterogeneous components use a local stack fallback. Stack-passed arguments are written before ABI destinations are modified.

### System V AMD64

- Independent integer and floating register sequences
- Six integer/pointer argument registers
- Eight scalar XMM argument registers
- Overflow arguments on the stack
- Exact call-site stack alignment

### Windows x64

- Position-based first four argument slots
- Integer or XMM register selected by each slot's type
- Arguments after the fourth on the stack
- Mandatory 32-byte shadow space
- Exact alignment padding

Forge keeps aggregates pointer-oriented internally, but explicit native calling conventions lower eligible by-value parameters and returns through the platform ABI. ABI-indirect aggregates use hidden result storage.

## Instruction selection and code quality

The machine optimizer and encoder support:

- Integer and floating arithmetic
- Signed/unsigned comparisons and NaN-correct floating predicates
- Compare/branch fusion
- Immediate arithmetic, shifts, comparisons, returns, and memory stores
- Scaled-`LEA` and shift strength reduction for constant multipliers factored as `1`, `3`, `5`, or `9` times a power of two
- Stack memory-source arithmetic and load/return folding
- x86 base-plus-disp32 address modes
- Floating zero idioms
- Constant rematerialization
- Deferred spill-store elimination and store/load forwarding
- Two-entry integer and floating spill caches
- Spill-slot reuse and frame compaction
- Frameless leaf functions
- Fixed-point short conditional/unconditional branch relaxation
- Fallthrough selection and branch inversion

Exact counters and encoded-byte ceilings are enforced by focused CTest quality fixtures.

## JIT memory policy

The JIT allocates read/write memory, copies a fully relocated image, changes the mapping to read/execute, flushes the instruction cache, and owns the mapping for the engine lifetime. Named lookup never transfers memory ownership.

## Object emission

ELF and COFF writers provide deterministic section, symbol, and relocation ordering. ELF objects include `.note.GNU-stack`; COFF raw sections use deterministic alignment. Duplicate definitions are rejected before symbol-table construction. The test suite checks byte-for-byte reproducibility and links generated ELF objects into native executables.

## Platform boundaries

The backend does not yet provide:

- Native by-value aggregate ABI classification
- True variadic function definitions
- Unwind and debug-frame metadata
- Segmented live-range allocation
- Non-x86-64 targets

These are tracked as future backend work rather than implied capabilities.

## ABI classification

`<forge/platform/abi.hpp>` classifies named aggregates for System V AMD64 and Windows x64. This analysis is intended for frontend signature lowering and future register-classified aggregate code generation.

## Native libraries

`<forge/object/archive.hpp>` emits deterministic static archives with linker symbol indexes for ELF64 and COFF AMD64 members. `<forge/object/native_link.hpp>` can invoke a host compiler driver to produce shared libraries.

## Segmented liveness and hole-aware allocation

Forge records each virtual register as a set of disjoint live segments instead of relying only on a single bounding interval. This matters when mutually exclusive control-flow paths are interleaved in physical block order: their bounding intervals may overlap even though the values can never be live at the same time.

The allocator uses these segments to:

- measure register pressure from actual live regions;
- build exact same-class interference edges;
- identify live-range holes; and
- recover physical registers for false spills after the initial linear scan.

This release keeps one final location per virtual register. It does not yet move a value between a register and a spill slot along one genuinely live path; explicit transition-based splitting remains a future allocator stage.

`forge-codegen --stats` reports:

```text
segmented-intervals
live-range-holes
interference-edges
hole-aware-register-reuses
```

## Transition-based live-range splitting

Forge inserts explicit split transitions at call boundaries when register pressure exceeds the ABI-safe register capacity. Eligible values are stored to compiler-reserved frame slots immediately before the call, reloaded into new virtual registers immediately afterward, and all dominated same-block uses are rewritten to the post-call segment.

The initial implementation is conservative: it handles same-block post-call uses, splits all floating call-crossing values supported by the scalar backend, and splits integer values beyond the two callee-saved allocation registers. Values carried across CFG edges remain on the existing conservative allocation path.

`forge-codegen --stats` reports `live-range-splits`, `split-transition-stores`, `split-transition-loads`, and `split-transition-bytes`.

### Critical-edge transition splitting

When a call path converges with other predecessors, Forge can create a dedicated split-edge block, reload spilled values there, and merge the reloaded/original values through machine block parameters. This preserves SSA semantics across multi-predecessor continuations without globally rewriting values that remain valid on other paths.

The current implementation handles a single successor from the call block and repairs all incoming edge argument lists deterministically.

## Global copy-affinity coalescing

Local linear-scan copy reuse is extended by a post-allocation affinity stage. The stage uses segmented interference rather than bounding intervals, allowing copy-related virtual registers separated by control-flow layout or liveness holes to share a physical register when no real segment conflicts. Stack-backed copy destinations are recovered when the source register remains globally safe. Allocation statistics expose `global_copy_affinity_count` and `copy_spills_recovered`.

## Native aggregate parameters

Named aggregate parameters and returns are lowered according to the configured native x86-64 ABI. Register-passed parameters are expanded into INTEGER/SSE pieces and reconstructed in callee-local storage. Register-returned aggregates are loaded into RAX/RDX and XMM0/XMM1 as classified, and callers reconstruct them into aligned Forge aggregate storage. ABI-indirect aggregates retain the hidden result-buffer path.
