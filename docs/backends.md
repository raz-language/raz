# Raz code-generation backends

Raz lowers source through one typed HIR and one backend-neutral MIR. Backend selection changes code generation, not language semantics.

## Forge

Forge is the default native backend and is linked directly into the production compiler. It supports textual and structured IR paths, native x86-64 object emission, register allocation, target ABI lowering, aggregate layout, module storage, TLS, external calls, function pointers, control flow, scalar and aggregate operations, and native object formats used by supported hosts.

Typical commands:

```text
raz build --backend=forge
raz forge source.rz output.fir
raz forge --forge-native source.rz output.obj
```

Optimization levels:

```text
--opt=0
--opt=1
--opt=2
--opt=3
--opt=s
--opt=z
```

## LLVM

The LLVM IR backend is implemented in Raz and consumes the same MIR. Raz emits textual LLVM IR itself; native object and executable production then uses an external LLVM/Clang toolchain.

```text
raz build --backend=llvm
raz llvm --emit=llvm source.rz output.ll
raz llvm --emit=obj --opt=3 source.rz output.obj
raz llvm --emit=exe source.rz app.exe
```

Backend controls include target triple, data layout, CPU/features, optimization, debug information, LTO, relocation model, code model, visibility, native runtime/library paths, linker selection, linker arguments, and symbol linkage metadata.

## Aggregate and reference model

Backend-neutral MIR preserves Raz's ownership/reference semantics. Forge may lower aggregates directly into native layouts. LLVM maintains the representation required by the shared MIR/runtime contract and uses typed references/function pointers where the ABI requires them. Both implementations must preserve the same observable source-language behavior.

## Async

Structured async lowering is defined before backend selection. Suspension state, ownership across `await`, cleanup, and control-flow merges are represented in compiler IR so a backend cannot weaken ownership semantics.

## Module storage and TLS

Module-level scalar and aggregate storage is represented before backend selection. Backends provide platform-appropriate data/TLS emission while preserving initialization, access, and destruction rules.

## Qualification

Backend changes are checked with shared language examples, backend-differential tests, native artifact tests, and recursive compiler builds. A backend is not allowed to silently accept a program with different language semantics from the other production backend.
