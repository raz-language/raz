# Architecture

Forge is organized as a verified, deterministic compiler pipeline. Each layer has a narrow contract and rejects malformed input before passing it downstream.

## Pipeline

1. **IR construction and parsing** — textual parsing, C++/C frontend APIs, canonical printing, and owned SSA data structures.
2. **Verification** — type, SSA, control-flow, block-argument, terminator, global, and operation validation.
3. **Analysis and optimization** — deterministic pass pipelines with optional verification and per-pass reports.
4. **Machine lowering** — target-neutral machine operations and control flow.
5. **Machine optimization** — liveness, DCE, CFG cleanup, copy propagation, instruction-selection preparation, and virtual-register compaction.
6. **Allocation and frame layout** — call-aware linear scan, weighted spills, reusable spill slots, and final frame computation.
7. **x86-64 encoding** — ABI marshaling, branch layout, instruction encoding, spill caching, and relocation generation.
8. **Execution and artifacts** — interpreter, W^X JIT, ELF64, COFF AMD64, incremental object assembly, and native linking.

## Core invariants

- IR is verified before optimization, interpretation, lowering, or code generation.
- Machine IR is verified after structural transformations.
- The interpreter is the semantic reference implementation.
- Pass order, declaration order, block order, symbol order, section order, and relocation order are deterministic.
- Compiler state is explicit; there is no hidden global pass registry.
- Frontends depend on the IR SDK or C API, not machine IR or encoder internals.

## SSA and control flow

Control-flow values are transferred through typed block arguments rather than PHI nodes. The verifier checks reachability, dominators, definition ordering, successor arity and types, and terminator placement.

## Binary IR

Binary IR is a versioned transport and cache format, not a dump of C++ object layouts. Fields are encoded explicitly in little-endian form, decoded into owned IR objects, and verified before use.

## Incremental compilation

Semantic and frontend fingerprints are tracked separately. Direct-call dependency graphs propagate invalidation to callers. Rebuilt functions can be encoded independently, stored as versioned artifacts, and assembled with unchanged cached functions into deterministic ELF or COFF output.

## Security posture

Text IR, binary IR, runtime bindings, external symbols, cached artifacts, and object metadata are treated as untrusted boundaries. Release gates include strict verification, fuzz-smoke targets, ASan/UBSan, leak checks, deterministic output, native linking, and interpreter/JIT differential execution.
