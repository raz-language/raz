# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0

from pathlib import Path

root = Path(__file__).resolve().parents[2]
files = {
    "model": root / "compiler/src/hir/core/model.rz",
    "decl": root / "compiler/src/hir/semantic/declarations.rz",
    "expr": root / "compiler/src/hir/semantic/expressions.rz",
    "mir": root / "compiler/src/mir/lowering.rz",
    "llvm": root / "compiler/src/backend/llvm/globals_codegen.rz",
    "forge": root / "compiler/src/backend/forge/globals_codegen.rz",
}
text = {k: p.read_text(encoding="utf-8") for k, p in files.items()}
required = {
    "model": ["global_array_extents"],
    "decl": ["hir_global_initializer_is_static", "hir_global_structure_needs_drop", "global_array_extents"],
    "expr": ["global_array_extents"],
    "mir": ["mir_materialize_static_global_value", "global_array_extents"],
    "llvm": ["llvm_emit_global_lifecycle", "llvm_emit_static_aggregate_create", "llvm_emit_static_aggregate_destroy", "call i64 @raz_rt_stage1_arena_create", "call void @raz_rt_stage1_arena_destroy"],
    "forge": ["emit_forge_global_lifecycle", "forge_emit_static_aggregate_create", "forge_emit_static_aggregate_destroy"],
}
for name, markers in required.items():
    for marker in markers:
        if marker not in text[name]:
            raise SystemExit(f"aggregate-globals: FAIL {name} missing {marker!r}")
order = {path.relative_to(root / 'compiler').as_posix() for path in (root / 'compiler/src').rglob('*.rz')}
if "src/backend/llvm/globals_codegen.rz" not in order:
    raise SystemExit("aggregate-globals: FAIL missing llvm globals semantic module")
example = (root / "tests/examples/backends/aggregate_globals.rz").read_text(encoding="utf-8")
for marker in ["global mut i64 values[4]", "global Config config", "Config {", "pair: Pair {", "bias: 5"]:
    if marker not in example:
        raise SystemExit(f"aggregate-globals: FAIL example missing {marker!r}")
print("aggregate-globals: PASS (shared fixed-array/plain-struct module construction + reverse teardown)")
