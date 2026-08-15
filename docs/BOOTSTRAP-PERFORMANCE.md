# Bootstrap performance

Raz's recursive bootstrap compiles the production compiler with a compiler produced by the preceding generation. This workload exercises the frontend, semantic database, HIR/MIR construction, Forge lowering, object emission, native linking, filesystem access, and project loader together.

The bootstrap is therefore both a self-hosting check and a representative compiler-throughput workload.

## Performance-sensitive mechanisms

### Mutable parameters

Self-host lowering keeps immutable parameters as direct parameter values. Parameters that are mutated are spilled into typed frame-local storage at function entry, and subsequent reads and writes use the normal local load/store representation. This keeps ordinary function entry lightweight while preserving correct mutable-parameter semantics.

### Arena metadata

Compiler arena allocations keep bounds metadata adjacent to the allocation data. Hot scalar arena access can validate bounds from this metadata without a global registry lookup. Lifetime-sensitive operations such as destruction, reference creation, reference access, and bulk-copy validation retain the registry-based ownership checks.

### Lexing and parsing

Production parsing performs the lexical traversal required for syntax construction and invalid-token diagnostics. The normal compilation path does not perform a redundant full-source token-count scan before parsing.

### Deterministic compiler source input

Recursive generations copy the canonical `compiler/src/` tree and use `compiler/bootstrap-source-order.txt` only where the bootstrap seed requires deterministic reconstruction. Normal self-host compiler builds use the project graph and module imports.

### Structured Forge integration

Recursive native compiler builds use Forge's in-process structured C API. MIR is lowered directly into Forge structures and Forge emits the native object without requiring a compiler-sized textual FIR file to be serialized and reparsed.

Textual FIR remains available for diagnostics, inspection, tooling, and compatibility behavior where explicitly selected.

### Native linking

The project driver fingerprints the actual object and library bytes supplied to the linker. Stable link inputs reuse cached digests, and unchanged native objects are not replaced merely because their producer executed again. This avoids unnecessary relinks while preserving clean-build output semantics.

## Measuring recursive compiler throughput

For meaningful measurements, record:

1. host CPU, memory, operating system, and compiler toolchain;
2. bootstrap profile and Forge optimization level;
3. source module count;
4. frontend/HIR/MIR/backend elapsed time;
5. emitted object size;
6. peak memory where available; and
7. recursive fixed-point result.

Use the same source tree and toolchain configuration when comparing results. Compiler changes should be evaluated on the complete self-host workload rather than a single synthetic source pattern.

## Diagnostic heartbeat

The bootstrap launcher monitors the compiler diagnostic file while a recursive generation is active and emits a periodic elapsed-time heartbeat. The default interval is 15 seconds and can be configured with:

```powershell
scripts/bootstrap.ps1 -HeartbeatSeconds 30
```

The diagnostic codes are compact internal markers for startup, project loading, parser completion, HIR completion, MIR completion, and backend emission. They are intended to show that a long recursive compile is still active without affecting compiler semantics.

The diagnostic writer uses a small fixed buffer and does not share the normal large Forge output buffer, keeping observation overhead negligible relative to the compiler workload.

## Deterministic qualification

Recursive compiler qualification compares the native compiler objects produced by the self-host generations. Stable inputs are expected to converge to byte-identical objects. Any mismatch is treated as a determinism or self-hosting failure and is investigated before release artifacts are produced.
