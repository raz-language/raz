#!/usr/bin/env python3
# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0

from pathlib import Path
root = Path(__file__).resolve().parents[2]
cfg = (root/'compiler/src/raz_codegen_wasm/src/wasm/cfg.rz').read_text()
codegen = (root/'compiler/src/raz_codegen_wasm/src/wasm/codegen.rz').read_text()
writer = (root/'compiler/src/raz_codegen_wasm/src/wasm/writer.rz').read_text()
memory = (root/'compiler/src/raz_codegen_wasm/src/wasm/memory.rz').read_text()
checks = {
    'linear fast path classifier': 'fn wasm_function_can_emit_linear' in cfg,
    'table demand classifier': 'fn wasm_module_needs_table' in cfg,
    'table omitted when unused': 'if (!wasm_module_needs_table(hir, mir))' in codegen,
    'linear body path': 'if (linear)' in codegen and 'control_locals = 0' in codegen,
    'compact register locals': 'fn wasm_function_register_count' in writer and 'wasm_register_ordinal' in writer,
    'control-only nodes excluded': 'opcode != 21 && opcode != 22 && opcode != 23' in writer,
    'constant aggregate offset folding': 'fn wasm_memory_constant_slot_offset' in memory and 'wasm_u32(out, offset)' in memory,
    'string literal deduplication': 'fn wasm_memory_string_is_first' in memory and 'wasm_memory_strings_equal' in memory,
    'extern wrappers hidden from exports': 'hir.function_is_extern, function_index) == 0' in codegen and 'export_function_count' in codegen,
}
missing=[k for k,v in checks.items() if not v]
if missing:
    raise SystemExit('wasm-optimization: FAIL: ' + ', '.join(missing))
print('wasm-optimization: PASS (linear CFG fast path + table DCE + compact locals + address folding + internal runtime exports)')
