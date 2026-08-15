# Bootstrap performance

Raz's recursive bootstrap compiles the production compiler with the compiler produced by the previous stage. Stage 2 -> Stage 3 is therefore the most useful end-to-end bootstrap performance test: it executes machine code emitted by the self-hosted compiler while compiling the complete compiler again.

## Stage 2 project-loader regression

A severe recursive-bootstrap regression originally looked like slow project discovery. Stage 1 -> Stage 2 completed normally, but the generated Stage 2 compiler could spend minutes before reaching the normal parse/HIR/MIR pipeline when invoked with `raz.toml`.

The root cause was compiler lowering, not filesystem I/O. Assignments whose HIR target was a **function parameter** were not represented by MIR stores. The right-hand side was computed, but the result was discarded. A loop such as:

```raz
while (equal > start && ascii_is_space(...)) {
    equal -= 1;
}
```

therefore became a loop whose induction variable never changed in self-generated machine code. Manifest parsing exposed the bug because its whitespace-trimming helpers mutate their `start` and `equal` parameters.

The self-hosted lowering now tracks parameters that are mutated, spills only those parameters into synthetic frame-local slots at function entry, and redirects subsequent reads and writes through the normal typed local load/store path. Immutable parameters remain direct parameter reads, so ordinary functions do not pay the spill cost. Direct assignment, compound assignment, loop mutation, pre-drop handling, and return paths all share the same mutable-parameter representation.

`raz-self-host-mutable-parameters` is a dedicated regression test. It lowers and executes a program that mutates a parameter through direct and compound assignments and a loop, and requires the native result to be 42.

## Arena scalar-access fast path

Correct mutable-parameter lowering made frame-slot traffic substantially more visible. The bootstrap runtime previously acquired the global arena mutex and performed an `unordered_map` lookup on every `raz_rt_stage1_arena_get` and `raz_rt_stage1_arena_set`. Generated compiler code performs these accesses extremely frequently, so correct parameter spilling exposed that synchronization path as a major performance cost.

Arena allocations now contain a private metadata header immediately before the data returned as the arena handle. The Raz-visible handle still points to the first `i64` element, and the global arena registry still owns allocation lifetime and reference validation. Scalar `get`/`set` operations read the private header directly for bounds validation and do not take the global registry lock. Destroy, reference creation, reference access, and bulk-copy validation retain the registry-based lifetime semantics.

This is an implementation optimization, not a Raz ABI change: existing generated code uses the same arena handles and calls the same runtime functions.

## Safe bulk source copying

`raz_rt_stage1_arena_copy` is a bounds-checked bulk-copy primitive. It replaces byte-at-a-time generated loops when concatenating compiler modules while retaining registry validation for both source and destination arenas.

## Removed duplicate lexical scan

The production driver previously ran `token_count(input)` immediately before `parse_module(input, ...)`. Parsing already performs a complete lexical traversal and reports invalid tokens, so the preflight scan duplicated a full traversal of the compiler source. The qualification helper still uses `token_count`; only the redundant production scan was removed.

## Deterministic recursive-stage source list

Recursive fixed-point stages copy the complete `compiler/src/` tree, read the canonical order from `compiler/bootstrap-source-order.txt`, and write `stage-input.txt` with `src/main.rz` last. The generated compiler consumes that source list directly.

The source list keeps fixed-point construction hermetic and independent of project-manager policy while still supporting a modular compiler tree. The production compiler separately supports and qualifies `raz.toml`, recursive source discovery, path dependencies, entrypoint ordering, cycle detection, and dependency deduplication.

The project-manifest route is no longer a performance workaround: after the mutable-parameter fix, the self-generated compiler can compile the complete compiler through either `stage-input.txt` or `raz.toml` and produce byte-identical Forge IR.


## Structured Forge fixed-point path

The recursive Windows bootstrap now requires `--forge-structured-only` for Stages 2 through 4. The compiler therefore lowers MIR directly into Forge's in-process structured C API and emits the native object beside the nominal `.fir` output stem. It no longer serializes the compiler into a multi-megabyte textual FIR module and asks Forge to parse that compatibility transport again.

Completing structured-backend coverage for the compiler required preserving aggregate shape through local loads, using the actual HIR local index rather than assuming local metadata is contiguous, materializing aggregate call arguments into native stack images, classifying aggregate stores from the destination field/element shape, and normalizing aggregate symbol names at the Forge bridge ABI boundary. Newly synthesized structure-field metadata is also initialized explicitly so callable-field state cannot inherit stale arena contents.

On the Linux qualification environment used for these measurements, the clean structured path measured about 27 seconds for Stage 1 -> Stage 2, 30 seconds for Stage 2 -> Stage 3, and 30 seconds for Stage 3 -> Stage 4 at `--opt=0`. More importantly, all three generated native objects were byte-identical with the same SHA-256. These measurements are diagnostic rather than guaranteed, but they demonstrate that the previous multi-minute fixed point was a backend transport/code-quality regression rather than an inherent cost of self-hosting.

The release invariant is now deterministic structured-object convergence for the retained Windows all-stage bootstrap. The legacy textual FIR route remains available as a compatibility path and for textual IR tooling; it is no longer used as the transport for compiler-sized recursive stages.

## Qualification measurements

On the Linux qualification environment used for the regression investigation, the corrected self-generated compiler produced the complete compiler in approximately 20 seconds through both the deterministic source-list input and the real `raz.toml` project loader. Stage 3 -> Stage 4 completed in approximately 20 seconds as well. The Stage 2, Stage 3, project-loader, and Stage 4 Forge modules were byte-identical.

These numbers are diagnostic measurements rather than performance guarantees; hardware, operating system, compiler profile, and filesystem behavior affect elapsed time. The fixed-point identity is the release invariant.

## Stage progress reporting

`scripts/bootstrap.ps1` launches recursive compiler stages with progress monitoring. While a stage is active it watches the compiler diagnostic file and reports phase transitions plus a periodic elapsed-time heartbeat. The default heartbeat interval is 15 seconds and can be changed with:

```powershell
./scripts/bootstrap.ps1 -StageStatusIntervalSeconds 30
```

The progress markers are:

- 80: compiler entry
- 81: startup / qualification probe complete
- 82: project manifest selected
- 83: recursive project loading started
- 84: recursive project loading completed
- 90: source package loaded
- 91: parse started
- 92: syntax parser completed
- 93: HIR completed
- 94: MIR completed
- 95: Forge backend emission completed

Progress diagnostics use a tiny 128-element writer rather than the normal Forge output buffer, so observing a recursive stage does not materially alter startup behavior.
