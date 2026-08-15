# Raz WebAssembly ABI v1

This document defines the stable WebAssembly ABI emitted by Raz for the wasm32 core-module target. It is a compatibility contract between the Raz compiler, the Raz standard library, and WASI hosts. Changes that alter an externally observable layout, tag, calling convention, or host-boundary rule require an ABI-version change or an explicitly compatible extension.

## Target model

Raz emits core WebAssembly modules directly from backend-neutral MIR. The baseline target is wasm32 with 32-bit linear-memory addresses. MIR machine values and handles use a uniform 64-bit bit-pattern representation internally. WebAssembly function boundaries use native `f32` and `f64` valtypes when the Raz signature is floating point; integer, boolean, reference, callable, aggregate, and handle values use the backend's i64 machine-value convention unless a host ABI requires narrowing.

## Linear memory

Bytes `0..511` are reserved for generated WASI adapter scratch. Allocator metadata begins at byte `512`. Static Raz data begins at byte `1024`. The runtime heap begins at the next eight-byte-aligned address after the final unique static-data object, with a minimum base of `1024`.

Aggregate MIR storage uses eight-byte slots. Structures and fixed arrays are contiguous slot sequences. A slice is a two-slot image `{data_handle, length}`. Static string literals are NUL terminated and byte-identical literals are deduplicated.

The wasm32 address-space ceiling is `0xffffffff`. Allocation arithmetic must fail or trap before publishing an address beyond that ceiling. WebAssembly page growth failure traps at the allocation boundary.

## Reference representation

A non-negative reference is a byte address in linear memory.

A local reference is encoded as `-(slot + 1)`, producing the range `-1, -2, ...`.

A module-global reference uses the tag base `-1048577`; global `n` is encoded as `-1048577 - n`. Values less than or equal to `-1048577` are therefore global-reference tags. The range between local tags and the global-reference base remains reserved.

## Module globals

WebAssembly global `0` is the mutable linear-memory frontier. WebAssembly global `1` is the most recent WASI errno. Non-extern Raz module globals follow these reserved globals one-for-one. When async functions exist, the generated current-async-frame global is appended after user globals.

## Functions and callables

Direct Raz function signatures use native WebAssembly float valtypes for `f32`/`f64` and the i64 machine-value convention for other MIR values.

A first-class non-capturing function value is its WebAssembly function-table index represented as an i64 machine value. Indirect calls use the HIR callable signature and `call_indirect`.

A capturing closure is a linear-memory object. Slot `0` contains the generated closure-adapter table index. Slots `1..N` contain captures as eight-byte MIR bit patterns. Closure adapters receive the user-visible arguments plus the closure environment handle, reload captures, convert float bit patterns at typed boundaries, and call the synthesized Raz closure function.

The function table and element section are omitted when the module has no indirect callables, closure adapters, or async pollers.

## Async and future frame

Compiler-generated async functions and the low-level Raz future runtime share a 64-byte header followed by eight-byte spill slots:

- `+0`: resume MIR instruction/state
- `+8`: result bits
- `+16`: poll function-table index, or `-1` for an externally completed future
- `+24`: spill-slot count
- `+32`: terminal status (`0` pending, `1` complete, `-1` cancelled)
- `+40`: reserved awaited-future field
- `+48`: reserved awaited-result field
- `+56`: reserved awaited-status field
- `+64`: first spill slot

`await` resumes at instruction granularity. Pending nested Raz futures are polled through the WebAssembly function table. Parent current-frame state is saved/restored across nested polls.

## Runtime allocator

General runtime allocation is a reusable address-ordered free-list heap with block splitting, adjacent-block coalescing, aligned-pointer recovery metadata, terminal-block frontier reclamation, and a minimum 16-byte user alignment. Supported explicit alignments are powers of two up to 4096. `realloc` may shrink in place, grow into an adjacent free block, grow a terminal block in place, or allocate-copy-free as a fallback. Bulk copy/fill use WebAssembly `memory.copy` and `memory.fill`.

Compiler-owned transient MIR aggregate allocation may use the deterministic aggregate arena independently of the general runtime allocator.

## WASI preview1 boundary

WASI preview1 is hidden behind Raz's permanent runtime boundary. WASI descriptors and pointers are narrowed to i32 only at host-call boundaries; Raz-facing values remain in the machine-value ABI.

Every fallible generated WASI adapter stores the returned errno in reserved WebAssembly global `1`. `raz_rt_last_error_code` reads that global.

Directory preopens are discovered with `fd_prestat_get`. Named guest mount paths are matched to preopen names and stripped before calling WASI `path_*` operations. Exact mount roots lower to `.`. Relative existing paths are probed against available preopens. Paths that cannot be resolved within host-provided capabilities fail through the Raz error contract.

A zero-argument synchronous or async Raz `main` may receive the exported WASI command entry `_start`. Integer results become process exit status; void returns status zero. Async command entry polls the Raz future state machine and yields through `poll_oneoff` while pending.

Environment mutation and child-process launch are not provided by the current preview1 target and fail deterministically as unsupported operations rather than fabricating success.

## SIMD

The wasm32 SIMD target uses standardized `v128` instructions. The shared Raz `core::simd` API has direct lowering for supported `i8x16`, `i16x8`, `i32x4`, `f32x4`, and `f64x2` operations. Raz's `i64x4` abstraction uses two `v128` halves. Native targets retain equivalent permanent runtime implementations.

## Compatibility rules

The following are ABI-breaking without an explicit compatibility mechanism: changing reference tag ranges; changing the two reserved global indices; changing the 64-byte future header or field offsets; changing aggregate slot width; changing closure object slot ordering; changing slice slot ordering; changing guest-visible preopen routing semantics; changing the meaning of callable table indices; or moving static data below the reserved low-memory boundary.

Adding new imports, SIMD operations, internal helper functions, optimization transforms, or omitted dead sections is ABI-compatible when existing observable layouts and calling conventions remain unchanged.
