# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0

from pathlib import Path
import re

root = Path(__file__).resolve().parents[2]
files = {
    'model': root / 'compiler/src/raz_hir/src/hir/core/model.rz',
    'lexer': root / 'compiler/src/raz_lexer/src/lexer.rz',
    'decl': root / 'compiler/src/raz_hir/src/hir/semantic/declarations.rz',
    'expr': root / 'compiler/src/raz_hir/src/hir/semantic/expressions.rz',
    'stmt': root / 'compiler/src/raz_hir/src/hir/semantic/statements.rz',
    'mir': root / 'compiler/src/raz_mir/src/mir/lowering.rz',
    'forge_globals': root / 'compiler/src/raz_codegen_forge/src/forge/globals_codegen.rz',
    'forge_fn': root / 'compiler/src/raz_codegen_forge/src/forge/function_codegen.rz',
    'forge': root / 'compiler/src/raz_codegen_forge/src/forge/codegen.rz',
    'llvm': root / 'compiler/src/raz_codegen_llvm/src/llvm/codegen.rz',
    'llvm_globals': root / 'compiler/src/raz_codegen_llvm/src/llvm/globals_codegen.rz',
}
text = {k: p.read_text(encoding='utf-8') for k,p in files.items()}
text['mir'] = '\n'.join(path.read_text(encoding='utf-8') for path in sorted((root / 'compiler/src/raz_mir/src/mir').rglob('*.rz')))
required = {
    'model': ['global_count', 'global_mutable_flags', 'global_extern_flags', 'global_tls_flags', 'global_internal_flags', 'global_initializer_nodes'],
    'lexer': ['fn token_is_global', 'fn token_is_static', 'fn token_is_thread_local'],
    'decl': ['fn hir_parse_global_declaration', 'primitive_type_is_float(value_type)', 'global_initializer_nodes'],
    'expr': ['hir_find_global', 'hir_add_node(builder, 54'],
    'stmt': ['global_mutable_flags'],
    'mir': ['kind == 54', 'mir_emit_typed(mir, 47', 'mir_emit_typed(out, 48', 'mir_emit_typed(mir, 49', 'global_values'],
    'forge_globals': ['emit_forge_module_globals', 'global_tls_flags', 'writer_module_global_name'],
    'forge_fn': ['opcode == 47', 'opcode == 48', 'opcode == 49'],
    'forge': ['opcode == 47', 'opcode == 48', 'opcode == 49', 'emit_forge_module_globals'],
    'llvm': ['llvm_emit_module_globals', 'opcode == 47', 'opcode == 48', 'opcode == 49'],
    'llvm_globals': ['llvm_emit_module_globals_impl', 'thread_local', 'llvm_emit_global_lifecycle'],
}
for key, markers in required.items():
    for marker in markers:
        if marker not in text[key]:
            raise SystemExit(f'module-storage: FAIL missing {marker!r} in {files[key].relative_to(root)}')

order = {path.relative_to(root / 'compiler').as_posix() for path in list((root / 'compiler').rglob('*.rz'))}
for name in ['src/raz_codegen_forge/src/forge/globals_codegen.rz', 'src/raz_codegen_forge/src/forge/function_codegen.rz', 'src/raz_codegen_llvm/src/llvm/globals_codegen.rz']:
    if name not in order:
        raise SystemExit(f'module-storage: FAIL missing semantic source module {name}')

print('module-storage: PASS (shared HIR/MIR globals/statics + aggregate lifecycle + extern data + LLVM TLS)')
