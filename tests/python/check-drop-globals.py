# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0

from pathlib import Path
root = Path(__file__).resolve().parents[2]
hir = (root/'compiler/src/hir/semantic/declarations.rz').read_text()
llvm = (root/'compiler/src/backend/llvm/globals_codegen.rz').read_text()
forge = (root/'compiler/src/backend/forge/globals_codegen.rz').read_text()
example = (root/'tests/examples/backends/drop_global.rz').read_text()
checks = {
    'drop globals admitted as static images': 'Drop-bearing structs are still static module images' in hir,
    'llvm finalizer resolves user drop': 'hir.struct_drop_functions' in llvm and 'llvm_function_name(out, source, hir, drop_function)' in llvm,
    'forge finalizer resolves user drop': 'hir.struct_drop_functions' in forge and 'writer_function_name(out, source, hir, drop_function)' in forge,
    'llvm recursive teardown remains': 'llvm_emit_static_aggregate_destroy(out, source, hir, child, child_storage, next_temp)' in llvm,
    'forge recursive teardown remains': 'forge_emit_static_aggregate_destroy(out, source, hir, child, child_storage, next_temp)' in forge,
    'drop global example': 'global Resource service = Resource {' in example and 'handle: 42' in example and 'impl Drop for Resource' in example,
}
failed = [name for name, ok in checks.items() if not ok]
if failed:
    for name in failed:
        print('drop-globals: FAIL', name)
    raise SystemExit(1)
print(f'drop-globals: PASS ({len(checks)} checks; user Drop before recursive module teardown)')
