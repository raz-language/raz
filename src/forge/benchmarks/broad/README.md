# Broad Forge/LLVM benchmark matrix

This benchmark is an optimization acceptance gate, not a single-score contest. It verifies output equivalence before timing and reports each workload independently so a compiler change cannot hide a severe regression behind one unusually large win.

Workload families currently cover dependent integer arithmetic, multi-recurrence loops, data-dependent control flow, high register pressure, floating-point recurrence, fixed-offset memory loads, integer and floating-point call chains, memory reductions and updates, call-crossing liveness, branch diamonds, floating dot products, and overwritten-store elimination.

```bash
python3 benchmarks/broad/run.py --samples 7 --opt-level O2
python3 benchmarks/broad/run.py --samples 7 --opt-level O3
python3 benchmarks/broad/run.py --samples 5 --check
```

`--check` applies the per-kernel limits in `thresholds.json`. The limits are intentionally independent and moderately tolerant of host noise. They are not claims that the present ratios are final performance targets; they prevent broad regressions while optimization work proceeds.

Results are written to `build/broad-bench/results-o2.json` or `results-o3.json`, including Forge and LLVM `.text` sizes.

## Extended coverage

The broad gate also includes:

- `multi_recur_100`: four simultaneously loop-carried integer recurrences and a rotating backedge.
- `branch_merge_200`: a branch diamond with SSA merge parameters on every iteration.
- `float_dot4`: floating-point loads, multiplication, reduction, and XMM pressure.
- `store_overwrite`: alias-aware elimination of a store fully overwritten before any read.
- `global_store_overwrite`: global dead-store elimination across basic-block boundaries.

These are intentionally distinct from the original Fibonacci, branch-walk, and scalar floating recurrence kernels. They prevent improvements to one exact CFG or recurrence shape from standing in for broad backend quality.
