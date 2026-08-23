# AArch64 backend

Forge has an experimental native AArch64 object backend for Linux and Darwin/macOS targets. It lowers the same verified Forge IR and canonical machine IR used by the x86-64 backend, applies AAPCS64-compatible argument/return classification, encodes A64 instructions directly, and writes deterministic ELF64 `EM_AARCH64` or Mach-O arm64 relocatable objects.

The backend uses an AArch64-specific linear-scan allocator over the shared machine liveness model. Scalar integer values are assigned to `x19`-`x28` and scalar floating values to `v8`-`v15`; those AAPCS64 callee-saved banks are preserved in the function frame. Full-width 128-bit vector values use `v16`-`v23` when they are call-local. Because AAPCS64 preserves only the low 64 bits of `v8`-`v15`, a Q value that remains live across a call is instead given a deterministic 16-byte spill home. Copy coalescing, CFG-hole-aware register recovery, and size-aware spill-slot coloring let disjoint live segments reuse both physical registers and stack storage. Keeping caller-saved `x0`-`x18` and `v0`-`v7` out of the scalar allocation pools leaves ABI argument/return registers and encoder scratch registers available without call-site live-value shuffling.

## Driver

```sh
forge compile module.fir --arch=aarch64 --format=elf -O2 -o module.o
forge compile module.fir --arch=aarch64 --format=macho -O2 -o module-macos.o
forge-codegen module.fir --arch=aarch64 --stats --allocation
forge-codegen module.fir --arch=aarch64 --emit-elf=module.o
forge-codegen module.fir --arch=aarch64 --emit-macho=module-macos.o
```

`--arch=auto` chooses AArch64 on an AArch64 host and x86-64 elsewhere. AArch64 supports ELF64 and Mach-O arm64 output; COFF remains explicitly unsupported. On an Apple AArch64 host, `--format=auto` selects Mach-O.

## Implemented ABI surface

- AAPCS64 integer/pointer arguments in `x0` through `x7`.
- Scalar floating-point arguments in `v0` through `v7`.
- Eight-byte aligned overflow argument slots on the caller stack.
- Scalar integer/pointer returns in `x0` and scalar `f32`/`f64` returns in `v0`.
- Non-HFA aggregates up to 16 bytes split into integer register pieces.
- Homogeneous floating-point aggregates of one through four `f32` or `f64` members classified as HFA register pieces.
- Large aggregate indirect returns use the AAPCS64 hidden result address in `x8`.

The aggregate lowering metadata records both register class and lane width, so four-member `f32` HFAs remain four 32-bit FP lanes instead of being flattened into two 64-bit chunks.

## Native encoding

The current encoder covers scalar integer and floating arithmetic, integer/FP conversions, comparisons, selects, pointer and stack memory, direct and indirect calls, function/global addresses, SSA edge copies, conditional/unconditional control flow, scalar and aggregate returns, Linux initial-exec TLS, and Darwin TLV access through `__tlv_bootstrap`. Allocated registers are now used directly as instruction operands/destinations for the scalar common path instead of bouncing through scratch registers.

ELF relocations currently emitted are:

- `R_AARCH64_CALL26`
- `R_AARCH64_ADR_PREL_PG_HI21`
- `R_AARCH64_ADD_ABS_LO12_NC`
- `R_AARCH64_TLSIE_ADR_GOTTPREL_PAGE21`
- `R_AARCH64_TLSIE_LD64_GOTTPREL_LO12_NC`

Mach-O arm64 emission uses `ARM64_RELOC_BRANCH26`, `PAGE21`, `PAGEOFF12`, `TLVP_LOAD_PAGE21`, `TLVP_LOAD_PAGEOFF12`, and `UNSIGNED` relocations. Native TLS is represented with `__DATA,__thread_data` plus `__DATA,__thread_vars` descriptors and Darwin-leading-underscore symbols.

## Optimizer boundary

AArch64 runs a canonical machine combine pass that propagates copy chains, canonicalizes zero-offset pointer operations and redundant same-width casts, and selects architectural ADD/SUB/CMP/shift immediate forms without weakening machine-SSA verifier invariants. The richer x86-64 optimizer still owns x86-only immediate, memory-source, fused-flags, and SLP/vector pseudos.

This boundary is deliberate: AArch64 receives useful target-independent cleanup plus its own alias-safe SLP map recognizer while never receiving a pseudo that only the x86-64 encoder understands. The AArch64 recognizer currently forms 128-bit in-place integer maps when the source and destination base are provably identical; separate-base maps wait for explicit alias/provenance proof.

## Remaining parity work

The following are not yet production-parity with x86-64:

- allocator parity features such as caller-saved allocation and rematerialization (segmented-hole reuse, copy coalescing, and spill-slot reuse are implemented);
- broader AArch64-specific logical-immediate/address selection and peephole combines;
- remaining NEON/Advanced SIMD forms: reusable packed DAGs, masked/tail vector forms, vector widths above 128 bits, and broader floating-point SIMD;
- full variadic AAPCS64/Darwin behavior and the remaining uncommon aggregate corner cases;
- AArch64 JIT execution;
- recursive native Raz bootstrap qualification using Forge AArch64.

LLVM remains the preferred Raz backend on AArch64 until those parity gates are complete.



## Advanced SIMD / NEON

Forge implements a production-checked NEON slice for the shared packed integer memory pseudos. `i32`/`i64` in-place maps, scalar maps, two-source maps, and three-source chained maps encode with 128-bit Q-register loads/stores and native `ADD`, `SUB`, `AND`, `ORR`, and `EOR`. Packed chain and postfix-DAG forms evaluate directly in Q registers. Integer contiguous reductions use `ADDV` for four-lane `i32` chunks and `ADDP` plus vector accumulation for `i64` chunks. Two-lane `i32` tails are emitted scalarly. The canonical AArch64 optimizer recognizes alias-safe unrolled in-place load/op/store runs and exact contiguous integer add-reduction trees, then compacts the eliminated scalar virtual registers.

Automatic SLP deliberately requires the source and destination base to be the same SSA pointer until Forge carries sufficient noalias/provenance metadata for separate regions. Explicit packed map pseudos still encode separate source/destination pointers.

## Vector allocation and spill policy

The allocator treats scalar floating-point and full-width vector values as distinct pressure classes. Scalar `f32`/`f64` values may occupy the preserved low lanes of `v8`-`v15`; Q values use `v16`-`v23` when their complete live range is call-local. A Q value live across any call is stack-homed for its full 16-byte width, avoiding the incorrect assumption that AAPCS64 preserves all 128 bits of `v8`-`v15`. Vector spill slots are 16-byte aligned and participate in the same non-overlapping lifetime coloring used by scalar spills.

The encoder supports `load_stack_v128` and `store_stack_v128`, including SSA edge staging, so register pressure and call boundaries remain semantically correct. Wider `v256`/`v512` stack forms remain explicitly rejected until the backend has a defined multi-register representation.

## Reduction and packed-program lowering

AArch64 can form contiguous integer-add reductions directly from ordinary canonical machine IR when the scalar load tree proves an exact, power-of-two contiguous region. The optimizer resolves aliases before dead-vreg compaction so rewritten return/consumer operands cannot accidentally refer to a different compacted virtual register. Explicit packed chain programs and non-reusable postfix DAG programs are also supported. Reusable DAGs remain gated until lifetime and register-reuse semantics are represented explicitly rather than guessed by the encoder.
