#!/usr/bin/env python3
# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0

from pathlib import Path
root = Path(__file__).resolve().parents[2]
float_src = (root / "compiler/src/backend/wasm/float.rz").read_text()
codegen = (root / "compiler/src/backend/wasm/codegen.rz").read_text()
order = {path.relative_to(root / 'compiler').as_posix() for path in (root / 'compiler/src').rglob('*.rz')}
need_float = [
    "fn wasm_float_decimal_bits",
    "fn wasm_float_emit_literal",
    "f32.reinterpret_i32",
    "f64.reinterpret_i64",
    "i32.reinterpret_f32",
    "i64.reinterpret_f64",
    "fn wasm_float_emit_binary",
    "fn wasm_float_emit_compare",
    "fn wasm_float_emit_numeric_cast",
    "f32.convert_i64_s",
    "f64.convert_i64_s",
    "i64.trunc_f32_s",
    "i64.trunc_f64_s",
    "f32.demote_f64",
    "f64.promote_f32",
]
for token in need_float:
    if token not in float_src:
        raise SystemExit(f"wasm-float: missing {token}")
need_codegen = [
    "wasm_float_emit_literal",
    "wasm_float_emit_bits_to_value",
    "wasm_float_emit_value_to_bits",
    "wasm_float_valtype",
    "wasm_float_emit_numeric_cast",
        "wasm_float_emit_memory_load",
        "wasm_float_emit_memory_store",
    "indirect_return_type",
]
for token in need_codegen:
    if token not in codegen:
        raise SystemExit(f"wasm-float: codegen missing {token}")
if "src/backend/wasm/float.rz" not in order:
    raise SystemExit("wasm-float: semantic compiler source graph missing float backend")
print("wasm-float: PASS (literals + arithmetic/comparisons + numeric casts + typed indirect/closure ABI)")
