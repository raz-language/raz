# Raz 1.0 performance model

Raz is a native systems language. Performance-sensitive behavior should be visible in source, measurable in generated code, and attributable to a specific compiler, backend, runtime, or library decision.

## Native pipeline

Raz source is parsed and type-checked by the Raz frontend, lowered through HIR and MIR, and translated to verified Forge IR. Forge performs optimization, machine lowering, register allocation, and x86-64 object emission. Production programs therefore execute as native machine code rather than through a bytecode VM or interpreter.

Forge was chosen in part to prove that a compact backend can serve a serious language frontend without requiring the frontend to integrate LLVM. This is an integration and maintainability decision, not a decision to accept slower execution. Raz and Forge can improve together at every layer of the native pipeline.

## Zero-cost language goals

The stable language is designed so common abstractions do not require hidden managed-runtime machinery:

- owned values use deterministic lifetime/drop semantics rather than a mandatory tracing GC;
- generic static dispatch is monomorphized;
- trait-object dispatch is explicit where dynamic dispatch is requested;
- closures use ownership-aware capture environments;
- slices and references are bounds/lifetime-aware views rather than heap objects by default;
- `defer` and `Drop` make cleanup explicit in control-flow lowering;
- async state is lowered into compiler-managed state machines rather than requiring one global scheduler model; and
- raw-pointer and FFI work remains an explicit unsafe boundary.

## Memory and allocation

Raz 1.0's `alloc` foundations use geometric capacity growth for vectors, deques, and owned strings. Ordinary-alignment growth uses an in-place-capable aligned `realloc` path, while over-aligned values preserve their stricter allocation contract. Wrapped deque movement is performed in at most two bulk spans. Bulk copy, move, fill, equality, and search operations use host memory primitives, while scalar byte access stays in Raz so parsers and codecs do not pay an ABI call per byte.

Compiler-internal arenas also keep hot scalar bounds metadata adjacent to the allocation. This avoids the global lock/hash lookup that once made recursive compiler generations dramatically slower.

## Concurrency and I/O

The stable library exposes:

- ordered atomics and fences;
- mutexes, reader/writer locks, conditions, semaphores, barriers, latches, and one-time initialization;
- bounded MPMC channels, plus a cacheline-separated lock-free SPSC ring with batched transfer operations;
- worker-pool execution and futures;
- monotonic timing and hardware-thread discovery;
- synchronous TCP/UDP/DNS plus socket tuning;
- nonblocking socket mode, reusable batch `poll`/`WSAPoll` readiness sets, and a wake-driven readiness reactor; and
- async filesystem/socket foundations.

These APIs intentionally stay close to host capabilities so high-level libraries can build batching and application-specific scheduling without paying for a mandatory heavyweight runtime.

## Measuring performance

Prefer representative application benchmarks over isolated syntax microbenchmarks. For compiler and runtime work, record at least:

1. wall-clock throughput and latency;
2. allocation count/bytes where relevant;
3. generated Forge IR and optimization level;
4. native object size;
5. target CPU/OS/toolchain; and
6. correctness results from the same build.

Performance patches should improve a general mechanism rather than recognize one benchmark shape.

## Optimization principles

Compiler and library optimization should target broadly useful mechanisms such as scalar simplification, alias-aware memory promotion, instruction selection, register allocation, code layout, vectorization, batching, allocation behavior, and target-aware lowering. Benchmark-specific source patterns are not part of the optimization contract.

## Compiler iteration performance

Project loading is incremental-aware before semantic analysis begins. On a cache miss,
Raz reads each module once and reuses that retained source for namespace discovery,
import discovery, and deterministic topological assembly. Namespace/import ownership
is indexed so large module graphs avoid repeated global import scans. Top-level
declaration identity is maintained in an open-addressed `(package, namespace, name)`
index, so duplicate detection does not rescan every previously declared symbol.

For unchanged semantic checks, Raz persists a versioned project-source snapshot and
semantic key under `target/cache`. The snapshot is validated against every contributing
manifest/module using file size and a normalized high-resolution modification tick;
a valid snapshot avoids project traversal and source rereads, and an exact semantic-key
match avoids rebuilding HIR. Same-size rewrites therefore invalidate correctly on
filesystems whose native file-clock epoch is signed. Any uncertainty is treated as a
miss and runs the normal compiler pipeline.

For a non-interface source edit, `raz check` can reuse the previous successful semantic
result for ordinary non-generic bodies in source-clean modules. Declarations and exported
interfaces are still rebuilt globally, while the changed module and modules affected by an
upstream interface change are fully checked. Build/code-generation commands remain
conservative until persisted HIR/MIR/object restoration is available.

## Generic-heavy incremental builds

Generated generic specializations use deterministic module ownership before native object emission. Structurally identical copies produced by multiple modules are kept in exactly one owner module and referenced externally from the others. This allows generic-heavy applications to retain per-module native object caching instead of collapsing to an aggregate object. Conflicting definitions still take the conservative fallback path.


## Content-addressed native linking

The native link cache is keyed by the bytes the linker actually consumes rather than
source timestamps or merely whether a compiler worker ran. Module object cache state
tracks separate input and object-content fingerprints. Re-emitting an object with
identical bytes keeps the existing object file and does not dirty the link. Dependency
archives and native runtime/Forge/OpenSSL libraries are likewise content fingerprinted,
with a metadata-keyed digest cache to keep hot no-op builds cheap. This is deliberately not a platform-specific partial/incremental linker: when any actual
link-input byte or linker configuration changes, Raz invokes the system linker normally.
That preserves byte-for-byte clean-build semantics across Linux and Windows.

## Backend compilation efficiency

Forge avoids rediscovering control-flow and module facts during native lowering. Machine
lowering builds a block-name index once per function and reuses it for control-flow edge
resolution. Runtime callback validation similarly indexes signature declarations once per
module rather than searching the function list for every binding.

Register-allocation call splitting derives per-block register-use and last-use tables once
per allocator iteration. Same-block and continuation-use queries are then constant-time
lookups instead of rescans of the remaining instruction stream for every live register.
The transformation and its pressure heuristics are unchanged; only repeated analysis work
is removed.

The Raz-to-Forge bridge also classifies structured-native support once for a module and
carries that result through fallback object emission and qualification diagnostics. A
complex module therefore does not rescan every function, local, and MIR instruction merely
to repeat an unchanged backend-capability decision.
