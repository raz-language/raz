# Raz code-generation backends

Raz lowers source through one typed HIR and one backend-neutral MIR. Backend selection changes code generation, not language semantics.

## Forge

Forge is the default native backend on x86-64 Windows/Linux and is linked directly into the production compiler. Its production x86-64 path supports textual and structured IR, register allocation, target ABI lowering, aggregate layout, TLS, external calls, function pointers, control flow, scalar/aggregate operations, and deterministic ELF/COFF output. Forge also contains an experimental AArch64 backend with deterministic ELF64 and Mach-O arm64 output, Linux TLS IE and Darwin TLV TLS, a native linear-scan allocator over the shared liveness model, callee-saved scalar `x19`-`x28`/`v8`-`v15` allocation, call-local Q allocation in `v16`-`v23`, call-safe 16-byte vector spills, copy coalescing/CFG-hole recovery/spill-slot coloring, target-safe immediate/canonical machine combines, and 128-bit NEON integer map/reduction plus packed chain/postfix-DAG lowering.

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

The LLVM IR backend is implemented in Raz and consumes the same MIR. Raz emits textual LLVM IR itself; native object and executable production then uses an external LLVM/Clang toolchain. LLVM remains the default native backend on AArch64 hosts until Forge's AArch64 backend reaches allocation, optimizer, runtime-link, and recursive-bootstrap parity.

```text
raz build --backend=llvm
raz llvm --emit=llvm source.rz output.ll
raz llvm --emit=obj --opt=3 source.rz output.obj
raz llvm --emit=exe source.rz app.exe
```

Backend controls include target triple, data layout, CPU/features, optimization, debug information, LTO, relocation model, code model, visibility, native runtime/library paths, linker selection, linker arguments, and symbol linkage metadata.


## Native target matrix

| Target | Forge native | LLVM native | Object format | Calling convention |
|---|---:|---:|---|---|
| `x86_64-pc-windows-msvc` | yes | yes | COFF | Windows x64 |
| `x86_64-unknown-linux-gnu` | yes | yes | ELF64 | System V AMD64 |
| `aarch64-unknown-linux-gnu` | experimental | yes | ELF64 | AAPCS64 |
| `arm64-apple-macos` | experimental | yes | Mach-O 64 | Darwin AArch64 |

An explicit cross-target `--emit=obj` needs only a Clang installation that supports the requested target. Cross-target executable linking additionally requires a target-compatible runtime archive via `--runtime=<path>` plus any sysroot/library flags required by the target toolchain. Raz never links the host runtime into a foreign-target executable.

## Aggregate and reference model

Backend-neutral MIR preserves Raz's ownership/reference semantics. Forge may lower aggregates directly into native layouts. LLVM maintains the representation required by the shared MIR/runtime contract and uses typed references/function pointers where the ABI requires them. Both implementations must preserve the same observable source-language behavior.

## Async

Structured async lowering is defined before backend selection. Suspension state, ownership across `await`, cleanup, and control-flow merges are represented in compiler IR so a backend cannot weaken ownership semantics.

## Module storage and TLS

Module-level scalar and aggregate storage is represented before backend selection. Backends provide platform-appropriate data/TLS emission while preserving initialization, access, and destruction rules.

## Qualification

Backend changes are checked with shared language examples, backend-differential tests, native artifact tests, and recursive compiler builds. A backend is not allowed to silently accept a program with different language semantics from the other production backend.
