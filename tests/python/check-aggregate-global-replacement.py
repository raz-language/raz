# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0

from pathlib import Path
root = Path(__file__).resolve().parents[2]
hir = (root/'compiler/src/raz_hir/src/hir/semantic/statements.rz').read_text()
mir = "\n".join(path.read_text() for path in sorted((root/'compiler/src/raz_mir/src/mir').rglob('*.rz')))
example = (root/'tests/examples/backends/aggregate_global_replacement.rz').read_text()
diff = root/'tests/examples/backends/differential/17_aggregate_global_replacement.rz'
checks = {
    'fresh aggregate replacement guard': 'replacement_kind != 34' in hir and 'node_type_structs, value) != global_structure' in hir and 'replacement_kind != 53' in hir and 'node_array_extents, value) != global_extent' in hir,
    'owned struct release helper': 'fn lower_hir_emit_structure_release' in mir and 'hir.struct_drop_functions' in mir and 'mir_emit(out, 31, storage' in mir,
    'global old-image load': 'i64 old_storage = mir_emit_typed(out, 47, global_index' in mir,
    'global old-image release': 'lower_hir_emit_structure_release(hir, out, structure, old_storage, 0)' in mir,
    'array old-image release': 'mir_emit(out, 31, old_storage' in mir,
    'replacement example': 'active = Resource {' in example and 'id: 7' in example and 'values = [4, 5, 6];' in example,
    'differential case': diff.exists(),
}
failed=[name for name, ok in checks.items() if not ok]
if failed:
    for name in failed: print('aggregate-global-replacement: FAIL', name)
    raise SystemExit(1)
print(f'aggregate-global-replacement: PASS ({len(checks)} checks; fresh-image replacement finalizes old ownership before install)')
