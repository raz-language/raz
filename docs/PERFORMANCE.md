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

Raz 1.0's `alloc` foundations use geometric capacity growth for `RawVec` and owned strings. Repeated small growth therefore avoids one reallocation per append. Bulk copy, move, and fill operations cross the native memory boundary directly instead of executing byte-at-a-time loops in Raz.

Compiler-internal arenas also keep hot scalar bounds metadata adjacent to the allocation. This avoids the global lock/hash lookup that once made recursive compiler generations dramatically slower.

## Concurrency and I/O

The stable library exposes:

- ordered atomics and fences;
- mutexes, reader/writer locks, conditions, semaphores, barriers, latches, and one-time initialization;
- bounded MPMC channels and cancellation tokens;
- worker-pool execution and futures;
- monotonic timing and hardware-thread discovery;
- synchronous TCP/UDP/DNS plus socket tuning;
- nonblocking socket mode and a wake-driven readiness reactor; and
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

## Current optimization priorities

The largest remaining standard-library opportunities are described in `STANDARD-LIBRARY-PERFORMANCE.md`. Backend work should focus on broadly useful Forge improvements such as scalar optimization, alias-aware memory promotion, instruction selection, register allocation, code layout, vectorization opportunities, and target expansion rather than frontend-specific special cases.
## Generic-heavy incremental builds

Generated generic specializations use deterministic module ownership before native object emission. Structurally identical copies produced by multiple modules are kept in exactly one owner module and referenced externally from the others. This allows generic-heavy applications to retain per-module native object caching instead of collapsing to an aggregate object. Conflicting definitions still take the conservative fallback path.


## Content-addressed native linking

The native link cache is keyed by the bytes the linker actually consumes rather than
source timestamps or merely whether a compiler worker ran. Module object cache state
tracks separate input and object-content fingerprints. Re-emitting an object with
identical bytes keeps the existing object file and does not dirty the link. Dependency
archives and native runtime/Forge/OpenSSL libraries are likewise content fingerprinted,
with a metadata-keyed digest cache to keep hot no-op builds cheap. On the 20-module
generic-heavy application used by the release regression, a fully fresh build remains
about 60 ms in the current Linux test environment after the digest cache is warm.

This is deliberately not a platform-specific partial/incremental linker: when any actual
link-input byte or linker configuration changes, Raz invokes the system linker normally.
That preserves byte-for-byte clean-build semantics across Linux and Windows.
