# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0

from pathlib import Path

root = Path(__file__).resolve().parents[1]
files = {
    'ownership': root / 'compiler/src/hir/semantic/ownership.rz',
    'mir': root / 'compiler/src/mir/lowering.rz',
    'llvm': root / 'compiler/src/backend/llvm/codegen.rz',
    'forge_sync': root / 'compiler/src/backend/forge/function_codegen.rz',
    'forge_async': root / 'compiler/src/backend/forge/codegen.rz',
}
text = {name: path.read_text(encoding='utf-8') for name, path in files.items()}
required = {
    'ownership': ['root_kind == 4', 'mutable_borrow != 0', 'global_mutable_flags'],
    'mir': ['operand_kind == 54', 'mir_emit_typed(mir, 49'],
    'llvm': ['opcode == 49', 'ptrtoint', 'llvm_module_global_name'],
    'forge_sync': ['opcode == 49', 'writer_module_global_name'],
    'forge_async': ['opcode == 49', 'writer_module_global_name'],
}
for name, markers in required.items():
    for marker in markers:
        if marker not in text[name]:
            raise SystemExit(f'global-reference-gate: FAIL {name} missing {marker!r}')

example = (root / 'examples/backends/global_references.rz').read_text(encoding='utf-8')
for marker in ['global mut i64 counter', 'i64&mut handle = &mut counter', 'add_two(handle)']:
    if marker not in example:
        raise SystemExit(f'global-reference-gate: FAIL example missing {marker!r}')

print('global-reference-gate: PASS (&global/&mut global -> MIR opcode 49 -> Forge/LLVM address lowering)')
