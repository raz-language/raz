# Performance Qualification

Raz treats compiler and generated-program performance as release-quality signals, not one-off benchmark claims.

## Compiler suite

`tools/benchmark-compiler.py` measures cold and warm frontend checks, incremental leaf edits, Forge and LLVM build latency, peak compiler RSS where the host exposes it, and semantic-query hit/miss counts. The full tier also checks the Raz compiler project itself so compiler-scale workloads remain visible.

## Runtime suite

`tools/benchmark-runtime.py` builds every maintained language workload through Forge and LLVM at `-O0`, `-O1`, `-O2`, `-O3`, `-Os`, and `-Oz`. It records compile latency, artifact size, process wall time, benchmark-internal nanoseconds when emitted by the workload, and peak RSS where available.

The maintained workload set covers CRC32, HTTP request parsing, integer reduction, branch-heavy integer work, and integer formatting. Add workloads when a new runtime subsystem becomes performance-critical; do not replace broad coverage with a benchmark-specific compiler special case.

## Baselines and thresholds

Measurements are written as stable JSON. `tools/check-performance-regression.py` compares a current snapshot with an intentional baseline using `benchmarks/config/performance-thresholds.json`.

Baselines are hardware/toolchain specific and must be promoted deliberately. CI must not invent a baseline from the current run because that would make regression enforcement meaningless. A missing baseline records measurements without failing; a supplied baseline makes the configured regression ceilings mandatory.

## Bootstrap integration

After a successful self-host bootstrap:

```text
python tools/run-benchmarks-from-bootstrap.py --suite full --output-dir benchmark-results
```

A hardware-specific baseline can be supplied with `--baseline-dir`.

The nightly workflow uploads benchmark JSON even when no baseline is configured so trends remain inspectable across runs.
