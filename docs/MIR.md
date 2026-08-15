# Mid-level Intermediate Representation

MIR is Raz's canonical executable semantic boundary. HIR may diagnose errors
earlier, but Forge, LLVM, and the interpreter consume only MIR that has passed
independent structural and ownership verification.

## Pipeline

```text
source
  -> HIR
  -> MIR lowering
  -> structural CFG verification
  -> MIR ownership verification
  -> CFG and scalar optimization
  -> instruction remapping and compaction
  -> CFG cleanup
  -> structural and ownership reverification
  -> Forge / LLVM / interpreter
```

## Structure and analysis

MIR owns its model, builder, control-flow graph, dataflow utilities, dominance,
liveness, use tracking, verifier, transformation pipeline, and interpreter.
Backend code does not define language-level ownership or control-flow policy.

Instruction-remapping support rewrites value operands, branch targets, function
instruction ranges, call/capture arguments, and semantic ownership metadata as
compacting transformations remove instructions.

## Ownership semantics

Ownership is represented by backend-invisible MIR metadata and CFG dataflow.
The verifier tracks definite initialization, moved state, partial moves,
projection paths, field reinitialization, shared/exclusive loans, non-lexical
loan expiration, and reborrow provenance.

Projection overlap is structural: ancestor/descendant paths overlap while
sibling fields remain independent. Reborrows retain parent-loan provenance and
must remain within the parent lifetime. The ownership verifier runs before and
after optimization, preventing transformed MIR from reaching a backend if its
semantics are no longer valid.

## Optimization

The backend-neutral MIR pipeline includes constant propagation, algebraic
canonicalization, copy propagation, dead-code elimination, unreachable-block
removal, jump threading, and safe CFG simplification. Transformations preserve
edge-value semantics and ownership program points.

## Boundary rule

Borrow checking, MIR semantics, optimization policy, diagnostics, traits,
generics, pattern matching, and language features evolve in Raz. Native code is
limited to bootstrap, runtime/OS/ABI services, and backend integration.
