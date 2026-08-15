# IR reference

Forge IR is a typed SSA representation organized into modules, functions, basic blocks, block parameters, and operations.

## Values and types

Every SSA value has one type. Core scalar types include booleans, fixed-width integers, `f32`, `f64`, pointers, and `void`. Aggregate and global declarations are represented explicitly.

## Functions and blocks

A function contains ordered basic blocks. Blocks may declare typed parameters used to transfer values across control-flow edges. Every reachable block ends with exactly one terminator.

## Verification

The verifier checks:

- Unique symbols and SSA definitions
- Operand existence and definition ordering
- Operand and result types
- Block successor existence
- Successor argument count and types
- Terminator placement
- Reachability and dominance requirements
- Global and aggregate constraints

## Text and binary forms

Canonical textual IR is intended for inspection, tests, and tool interchange. Binary IR is a versioned transport/cache representation. Both forms are verified before compilation or execution.

The checked-in examples under `examples/` are the best executable syntax reference for the current release.
