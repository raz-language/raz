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
- Generic AAPCS64 overflow arguments use at least eight-byte stack slots; Darwin arm64 fixed arguments retain their natural 1/2/4/8-byte widths and are tightly packed subject to natural alignment.
- Scalar integer/pointer returns in `x0` and scalar `f32`/`f64` returns in `v0`.
- Non-HFA aggregates up to 16 bytes split into integer register pieces.
- Homogeneous floating-point aggregates of one through four `f32` or `f64` members classified as HFA register pieces.
- Large aggregate indirect returns use the AAPCS64 hidden result address in `x8`.
- C variadic calls preserve the named/anonymous boundary through machine IR. Generic AAPCS64 keeps the anonymous tail in the normal register-allocation flow, while Darwin arm64 places every anonymous argument on the stack. C default promotions include `f32` to `f64` and narrow integer storage to at least 32 bits.
- Generic AAPCS64 rounds the next integer-register number to an even register for 16-byte-aligned integer/composite arguments; Darwin arm64 intentionally permits an odd starting register.
- Public ABI classification exposes generic AAPCS64 and Darwin arm64 separately, so tooling/C API stack-byte summaries use the same generic eight-byte overflow slots versus Darwin natural-width fixed stack packing as native code generation.

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

This boundary is deliberate: AArch64 receives useful target-independent cleanup plus its own legality-first SLP recognizers while never receiving a pseudo that only the x86-64 encoder understands. The AArch64 path forms 128-bit in-place scalar maps directly, proves separate source/destination runs disjoint when they resolve to distinct globals, and recognizes two-source maps, three-source left-deep maps, longer chains, branching postfix DAGs, reusable DAGs with shared subexpressions, and contiguous integer-add reductions. Opaque pointers and overlapping/shifted runs remain scalar unless provenance proves the transformation safe.

## Remaining parity work

The following are not yet production-parity with x86-64:

- allocator parity features such as caller-saved allocation and rematerialization (segmented-hole reuse, copy coalescing, and spill-slot reuse are implemented);
- broader AArch64-specific logical-immediate/address selection and peephole combines;
- remaining NEON/Advanced SIMD forms: general predicate/masked packs beyond the qualified contiguous half-vector tail model, automatic multi-Q width selection above 128 bits, floating reductions/comparisons, and conversion-heavy vector graphs;
- variadic function-definition/`va_list` callee support and the remaining uncommon aggregate/vector ABI corner cases;
- recursive native Raz bootstrap qualification using Forge AArch64.

LLVM remains the preferred Raz backend on AArch64 until those parity gates are complete.



## Advanced SIMD / NEON

Forge implements a production-checked NEON slice for the shared packed memory pseudos. `i32`/`i64` in-place maps, scalar maps, two-source maps, and three-source chained maps encode with 128-bit Q-register loads/stores and native `ADD`, `SUB`, `AND`, `ORR`, and `EOR`. The same canonical SLP path now recognizes contiguous `f32`/`f64` expression lanes and preserves floating add/sub/mul/div through scalar-broadcast maps, vector-vector maps, chained maps, postfix DAGs, and reusable DAGs; those lower to native `FADD`, `FSUB`, `FMUL`, and `FDIV` vector forms. Packed chains and postfix DAGs evaluate directly in Q registers, while reusable DAGs use identity-bearing node metadata so shared vector subexpressions are computed once and retained in a bounded scratch-Q pool. Integer contiguous reductions use `ADDV` for four-lane `i32` chunks and `ADDP` plus vector accumulation for `i64` chunks. Contiguous packed maps and expression graphs also support a safe half-vector tail: an 8-byte remainder is loaded/stored through `D` while the same NEON arithmetic runs on the active low lanes, so 6×`f32`/`i32` and 3×`f64`/`i64` do not fall back to scalar code or read past the proven run. A final remainder smaller than eight bytes stays scalar; general arbitrary lane predicates still require SVE-style or explicit masked lowering.

Automatic SLP uses resolved pointer provenance rather than raw virtual-register inequality. Exact in-place maps are always eligible when ordering is preserved; separate source/destination maps and multi-source expression packs are selected when all memory runs resolve to distinct globals. Opaque pointers, shifted overlaps, intervening stores, and calls remain hard legality barriers.

## Vector allocation and spill policy

The allocator treats scalar floating-point and full-width vector values as distinct pressure classes. Scalar `f32`/`f64` values may occupy the preserved low lanes of `v8`-`v15`; Q values use `v16`-`v23` when their complete live range is call-local. A Q value live across any call is stack-homed for its full 16-byte width, avoiding the incorrect assumption that AAPCS64 preserves all 128 bits of `v8`-`v15`. Vector spill slots are 16-byte aligned and participate in the same non-overlapping lifetime coloring used by scalar spills.

The encoder supports `load_stack_v128` and `store_stack_v128`, including SSA edge staging, so register pressure and call boundaries remain semantically correct. Logical `v256` and `v512` values are conservatively stack-homed and their stack transfers are decomposed into deterministic 16-byte Q chunks; they are not yet assigned a multi-register physical representation.

## JIT execution

Forge now has a native AArch64 JIT loader in addition to deterministic object emission. The loader resolves internal `BL` calls, function addresses, and read-only/writable global addresses directly against the allocated image. External host functions are routed through compact in-image `x16` veneers containing an absolute address, keeping call sites within architectural `BL` range even when the host process or shared library is mapped far from the JIT allocation. `forge-run --engine=jit` selects AAPCS64 on non-Apple ARM64 hosts and Darwin arm64 on Apple Silicon.

AArch64 JIT TLS uses a runtime-owned model rather than attempting to reuse object-file TLS relocations in process memory. Linux initial-exec and Darwin TLV four-instruction `tls.address` sequences are transactionally rewritten to `BL` a local per-symbol thunk; the thunk materializes an opaque descriptor in `x0` and tail-branches to the Forge TLS helper. Internal TLS blocks copy the encoded initializer lazily on first access by each host thread, so one `Engine` has independent state on every thread. External TLS uses the optional `tls_resolver` passed to the AArch64 `jit::load` overload and invokes it at access time, ensuring the returned address belongs to the calling thread rather than the thread that loaded the module. AArch64 register allocation treats `tls.address` as call-clobbering, which is required by the JIT helper and also models Darwin's native `__tlv_bootstrap` call correctly. `Engine::lookup_global` returns the current thread's internal-TLS address and `is_global_thread_local` exposes the storage class.

External non-TLS globals are resolved into an aligned read-only pointer table inside the JIT image. Their normal `ADRP` + `ADD` address sequence is rewritten at load time to `ADRP` + `LDR`, so the generated code reaches a nearby slot while that slot contains the unrestricted 64-bit host address. This removes the architectural `ADRP` distance restriction without changing ELF/Mach-O object relocation semantics. The relocation/TLS-thunk gates are host-independent; native execution, including cross-thread TLS isolation, is additionally exercised whenever the test suite runs on an ARM64 host.

## Reduction and packed-program lowering

AArch64 can form contiguous integer-add reductions directly from ordinary canonical machine IR when the scalar load tree proves an exact, power-of-two contiguous region. Reduction roots may feed later arithmetic, while interior nodes and load leaves must remain private; intervening stores or calls block formation so vector-load sinking cannot change memory observations. The optimizer resolves aliases before dead-vreg compaction so rewritten return/consumer operands cannot accidentally refer to a different compacted virtual register. Packed chain, postfix-DAG, and reusable-DAG programs are all supported, with reusable nodes evaluated once per Q chunk.
