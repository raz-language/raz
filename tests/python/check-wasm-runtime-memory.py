# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0

from pathlib import Path
root=Path(__file__).resolve().parents[2]
p=(root/'compiler/src/raz_codegen_wasm/src/wasm/runtime_memory.rz').read_text()
m=(root/'compiler/src/raz_codegen_wasm/src/wasm/memory.rz').read_text()
b=(root/'library/alloc/box/box.rz').read_text()
c=(root/'compiler/src/raz_codegen_wasm/src/wasm/codegen.rz').read_text()
required=[
    'wasm_runtime_memory_copy','wasm_runtime_memory_move','wasm_runtime_memory_fill',
    'wasm_runtime_memory_alloc','wasm_runtime_memory_realloc','wasm_runtime_memory_alloc_aligned',
    'wasm_runtime_memory_dealloc','wasm_runtime_memory_dealloc_aligned',
    'wasm_runtime_memory_free_head_offset','wasm_runtime_memory_min_split',
    'wasm_runtime_memory_min_alignment','wasm_runtime_memory_max_alignment',
    'wasm_runtime_memory_max_request','wasm_runtime_memory_emit_trap_if',
    'Grow the terminal allocation in place whenever possible',
    'Split the adjacent free block at block+new_total',
    'Build a synthetic allocated tail and free it through the normal hardened',
    'rejecting a double free of the same block','Neighbor-overlap validation',
    'wasm_memory_emit_ensure_capacity','wasm_u32(&mut body, 10)','wasm_u32(&mut body, 11)'
]
missing=[x for x in required if x not in p]
assert not missing, missing
assert 'wasm_runtime_memory_function_supported' in c
assert 'wasm_runtime_memory_emit_body(&mut section, source, hir, mir, function_index)' in c
assert 'heap_end+65535 overflow' in m
assert 'memory.grow 0. A failed growth returns -1' in m
assert '4294967295' in m
for symbol in [
    'wasm_memory_prepare_strings', 'wasm_memory_release_strings',
    'wasm_memory_string_addresses_cache', 'wasm_memory_string_first_cache',
    'wasm_memory_string_heap_base_cache',
    'slot = (slot + 1) & (capacity - 1)',
]:
    assert symbol in m, symbol
assert 'wasm_memory_prepare_strings(source, mir)' in c
assert 'wasm_memory_release_strings();' in c
for symbol in ['allocate_zeroed','allocate_zeroed_aligned','allocate_zeroed_array<T>','raz_rt_fill(pointer, 0']:
    assert symbol in b, symbol
print('wasm-runtime-memory: PASS (hardened reusable heap + engine-valid realloc + prepared string pool + zeroed allocation)')
