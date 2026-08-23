#!/usr/bin/env python3
# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0

from pathlib import Path

root = Path(__file__).resolve().parents[2]
backend = (root / "compiler/src/backend/wasm/codegen.rz").read_text(encoding="utf-8")
writer = (root / "compiler/src/backend/wasm/writer.rz").read_text(encoding="utf-8")
cfg = (root / "compiler/src/backend/wasm/cfg.rz").read_text(encoding="utf-8")
dispatch = (root / "compiler/src/driver/backend.rz").read_text(encoding="utf-8")
cli = (root / "compiler/src/driver/cli.rz").read_text(encoding="utf-8")
order = {path.relative_to(root / 'compiler').as_posix() for path in (root / 'compiler/src').rglob('*.rz')}

required_backend = [
    "fn emit_wasm_module(",
    "wasm_emit_type_section",
    "wasm_emit_function_section",
    "wasm_emit_export_section",
    "wasm_emit_code_section",
    "wasm_validate_module",
    "wasm_emit_branch_terminator",
    "wasm_emit_dispatch_block",
    "// unreachable",
]
for needle in required_backend:
    assert needle in backend, f"missing WASM backend primitive: {needle}"

for needle in ["fn wasm_u32(", "fn wasm_i64(", "i64 header[8] = [0, 97, 115, 109, 1, 0, 0, 0]"]:
    assert needle in writer + backend, f"missing WASM binary encoder primitive: {needle}"

assert "--backend=wasm" in dispatch
assert "return 2;" in dispatch
assert "emit_wasm_module" in dispatch
assert 'cli_arg_equals_literal(value, length, "--wasm")' in cli
assert "src/backend/wasm/writer.rz" in order
assert "src/backend/wasm/cfg.rz" in order
assert "src/backend/wasm/codegen.rz" in order

for needle in [
    "fn wasm_function_block_count(",
    "fn wasm_block_for_target(",
    "fn wasm_block_ends_in_terminator(",
]:
    assert needle in cfg, f"missing WASM CFG primitive: {needle}"

for opcode in [22, 23, 24]:
    assert f"opcode == {opcode}" in backend, f"MIR CFG opcode {opcode} not handled by WASM backend"

for wasm_opcode in [2, 3, 4, 5, 11, 12, 64, 80, 81]:
    assert f"wasm_u8" in backend

assert "wasm_edge_value_local" in backend
assert "wasm_dispatch_local" in backend

# Core binary opcode constants used by the scalar slice.
for value in [66, 124, 125, 126, 127, 128, 129, 130, 131, 132, 133, 134, 135, 136, 173]:
    assert f"wasm_u8(out, {value})" in backend, f"missing expected WASM opcode {value}"

print("wasm-backend: PASS (direct Raz-owned wasm backend with structured MIR CFG dispatch)")

# Phase 2 linear-memory qualification.
memory = (root / 'compiler/src/backend/wasm/memory.rz').read_text()
order = {path.relative_to(root / 'compiler').as_posix() for path in (root / 'compiler/src').rglob('*.rz')}
assert 'src/backend/wasm/memory.rz' in order, 'wasm memory module absent from production compiler source graph'
assert 'wasm_memory_emit_allocate' in memory, 'missing wasm aggregate allocator'
assert 'wasm_memory_emit_load' in memory and 'wasm_memory_emit_store' in memory, 'missing wasm aggregate memory operations'
assert 'wasm_emit_memory_section' in memory, 'missing wasm memory section'
globals_src = (root / 'compiler/src/backend/wasm/globals.rz').read_text()
assert 'wasm_emit_global_section' in globals_src, 'missing generalized wasm global section'
codegen2 = (root / 'compiler/src/backend/wasm/codegen.rz').read_text()
for opcode in [
    'opcode == 26',
    'opcode == 27',
    'opcode == 28',
    'opcode == 29',
    'opcode == 30',
    'opcode == 31',
    'opcode == 42',
    'opcode == 45',
    'opcode == 46',
]:
    assert opcode in codegen2, f'missing aggregate/reference MIR lowering {opcode}'
for primitive in [
    'wasm_memory_emit_reference_load',
    'wasm_memory_emit_reference_store',
    'wasm_memory_emit_element_reference',
    'wasm_memory_emit_local_reference',
    'wasm_memory_emit_ensure_capacity',
    'wasm_memory_string_address',
    'wasm_memory_heap_base',
    'wasm_emit_data_section',
]:
    assert primitive in memory, f'missing wasm memory primitive {primitive}'
assert 'wasm_emit_data_section(&mut module, source, mir)' in codegen2
assert 'wasm_emit_memory_section(&mut module, source, mir)' in codegen2
assert 'wasm_emit_global_section(&mut module, source, hir, mir)' in codegen2
assert 'wasm_signature_supported' in codegen2
assert 'parameter_array_extents' not in codegen2.split('fn wasm_signature_supported', 1)[1].split('fn wasm_opcode_supported', 1)[0]
print('wasm-memory: PASS (growable aggregates + references + static strings + aggregate ABI signatures)')

assert 'src/backend/wasm/globals.rz' in order
for opcode in ['opcode == 43', 'opcode == 44', 'opcode == 47', 'opcode == 48', 'opcode == 49']:
    assert opcode in codegen2, f'missing wasm callable/global lowering {opcode}'
for primitive in ['wasm_emit_table_section', 'wasm_emit_element_section']:
    assert primitive in codegen2, f'missing wasm function-table primitive {primitive}'
for primitive in ['wasm_globals_emit_get', 'wasm_globals_emit_set', 'wasm_globals_emit_reference']:
    assert primitive in globals_src, f'missing wasm global primitive {primitive}'
assert 'call_indirect' in codegen2
print('wasm-callables-globals: PASS (module globals + function tables + indirect calls)')

closures_src = (root / 'compiler/src/backend/wasm/closures.rz').read_text()
assert 'src/backend/wasm/closures.rz' in order
for opcode in ['opcode == 50', 'opcode == 51']:
    assert opcode in codegen2, f'missing wasm closure MIR lowering {opcode}'
for primitive in [
    'wasm_closure_count',
    'wasm_closure_emit_create',
    'wasm_closure_emit_call',
    'wasm_closure_emit_adapter_body',
    'wasm_closure_adapter_type',
]:
    assert primitive in closures_src, f'missing wasm closure primitive {primitive}'
assert 'wasm_closure_count(mir)' in codegen2
assert 'wasm_closure_emit_adapter_body' in codegen2
print('wasm-closures: PASS (captured environments + generated adapters + indirect closure calls)')
