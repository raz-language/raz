#!/usr/bin/env python3
# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0

from pathlib import Path

root = Path(__file__).resolve().parents[2]
future = (root / 'compiler/src/backend/wasm/runtime_future.rz').read_text(encoding='utf-8')
codegen = (root / 'compiler/src/backend/wasm/codegen.rz').read_text(encoding='utf-8')
wasi = (root / 'compiler/src/backend/wasm/wasi.rz').read_text(encoding='utf-8')
order = (root / 'compiler/host-source-order.txt').read_text(encoding='utf-8')

assert 'src/backend/wasm/runtime_future.rz' in order
assert 'public import raz_compiler_backend_wasm_runtime_future;' in codegen
assert 'wasm_future_runtime_function_supported(source, hir, function_index)' in codegen
assert 'wasm_future_runtime_emit_body(&mut section, source, hir, function_index)' in codegen
for name in [
    'wasm_future_create',
    'wasm_future_complete_i64',
    'wasm_future_cancel',
    'wasm_future_status',
    'wasm_future_wait_millis',
    'wasm_future_result_i64',
    'wasm_future_then_i64',
    'wasm_future_continuation_count',
    'wasm_future_destroy',
]:
    assert name in future, f'missing future runtime primitive: {name}'
for marker in [
    'wasm_future_runtime_function_supported',
    'wasm_future_runtime_emit_body',
    'wasm_future_i64_store_const(&mut body, 0, -1, 16)',
    'wasm_future_i64_store_const(&mut body, 0, 1, 32)',
    'wasm_future_i64_store_const(&mut body, 0, -1, 32)',
    'wasm_future_i64_load(&mut body, 0, 8)',
    'wasm_wasi_emit_poll_delay_local(&mut body, 1)',
    'wasm_wasi_set_last_error(&mut body, 58)',
]:
    assert marker in future, f'missing future ABI marker: {marker}'
assert 'fn wasm_wasi_emit_poll_delay_local' in wasi
print('wasm-future: PASS (shared async frame ABI + complete/cancel/status/result/wait/destroy + explicit unsupported native continuation callbacks)')
