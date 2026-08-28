# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0

from pathlib import Path

root = Path(__file__).resolve().parents[2]
simd = (root / "compiler/src/raz_codegen_wasm/src/wasm/simd.rz").read_text()
codegen = (root / "compiler/src/raz_codegen_wasm/src/wasm/codegen.rz").read_text()
order = {path.relative_to(root / 'compiler').as_posix() for path in list((root / 'compiler').rglob('*.rz'))}
core = (root / "library/core/simd/simd.rz").read_text()
native = (root / "src/runtime/platform_threads_crypto.cpp").read_text()

legacy = [
    "wasm_simd_runtime_function_supported", "wasm_simd_emit_runtime_body",
    "wasm_simd_op(body, 0)", "wasm_simd_op(body, 11)",
    "wasm_simd_emit_binary_body(section, 240, false)",
    "wasm_simd_emit_binary_body(section, 206, true)",
    "wasm_simd_emit_i64_reduce_body", "wasm_simd_emit_f64_reduce_body",
]
for token in legacy:
    if token not in simd:
        raise SystemExit(f"missing SIMD token: {token}")

new_runtime_names = [
    "raz_rt_i32x4_add", "raz_rt_i32x4_sub", "raz_rt_i32x4_mul",
    "raz_rt_i32x4_min", "raz_rt_i32x4_max", "raz_rt_i32x4_equal",
    "raz_rt_i32x4_splat", "raz_rt_i32x4_neg", "raz_rt_i32x4_reduce_add",
    "raz_rt_i32x4_all_true", "raz_rt_i32x4_bitmask", "raz_rt_i32x4_less",
    "raz_rt_i32x4_greater", "raz_rt_i32x4_and", "raz_rt_i32x4_or",
    "raz_rt_i32x4_xor", "raz_rt_i32x4_not",
    "raz_rt_f32x4_add", "raz_rt_f32x4_sub", "raz_rt_f32x4_mul",
    "raz_rt_f32x4_div", "raz_rt_f32x4_min", "raz_rt_f32x4_max",
    "raz_rt_f32x4_equal", "raz_rt_f32x4_splat", "raz_rt_f32x4_abs",
    "raz_rt_f32x4_neg", "raz_rt_f32x4_sqrt", "raz_rt_f32x4_reduce_add",
    "raz_rt_f32x4_less", "raz_rt_f32x4_greater", "raz_rt_f32x4_ceil",
    "raz_rt_f32x4_floor", "raz_rt_f32x4_trunc", "raz_rt_f32x4_nearest",
    "raz_rt_f32x4_from_i32x4", "raz_rt_i32x4_from_f32x4_sat",
    "raz_rt_i8x16_swizzle", "raz_rt_i8x16_splat", "raz_rt_i8x16_bitmask",
    "raz_rt_i8x16_any_true",
    "raz_rt_i8x16_add", "raz_rt_i8x16_sub", "raz_rt_i8x16_min",
    "raz_rt_i8x16_max", "raz_rt_i8x16_equal", "raz_rt_i8x16_all_true",
    "raz_rt_i8x16_shl", "raz_rt_i8x16_shr_s", "raz_rt_i8x16_shr_u",
    "raz_rt_i16x8_add", "raz_rt_i16x8_sub", "raz_rt_i16x8_mul",
    "raz_rt_i16x8_min", "raz_rt_i16x8_max", "raz_rt_i16x8_equal",
    "raz_rt_i16x8_splat", "raz_rt_i16x8_all_true", "raz_rt_i16x8_bitmask",
    "raz_rt_i16x8_shl", "raz_rt_i16x8_shr_s", "raz_rt_i16x8_shr_u",
    "raz_rt_i32x4_shl", "raz_rt_i32x4_shr_s", "raz_rt_i32x4_shr_u",
]
for name in new_runtime_names:
    if name not in core:
        raise SystemExit(f"core::simd missing declaration: {name}")
    if name not in native:
        raise SystemExit(f"native runtime missing SIMD parity: {name}")
    backend_name = "wasm_simd_" + name.removeprefix("raz_rt_")
    if backend_name not in simd:
        raise SystemExit(f"WASM SIMD backend missing lowering selector: {name}")

opcode_paths = [
    "wasm_simd_emit_binary_body(section, 174, false)",  # i32x4.add
    "wasm_simd_emit_binary_body(section, 181, false)",  # i32x4.mul
    "wasm_simd_emit_binary_body(section, 55, false)",   # i32x4.eq
    "wasm_simd_emit_i32_splat_body", "wasm_simd_emit_i32_reduce_body",
    "wasm_simd_emit_i32_scalar_mask_body(section, 164)", # i32x4.bitmask
    "wasm_simd_emit_binary_body(section, 228, false)",   # f32x4.add
    "wasm_simd_emit_binary_body(section, 231, false)",   # f32x4.div
    "wasm_simd_emit_f32_splat_body", "wasm_simd_emit_f32_reduce_body",
    "wasm_simd_emit_unary_body(section, 227)",           # f32x4.sqrt
    "wasm_simd_emit_unary_body(section, 250)",           # i32 -> f32
    "wasm_simd_emit_unary_body(section, 248)",           # f32 -> i32 sat
    "wasm_simd_emit_binary_body(section, 14, false)",    # i8x16.swizzle
    "wasm_simd_emit_i8_splat_body",
    "wasm_simd_emit_i32_scalar_mask_body(section, 100)", # i8x16.bitmask
    "wasm_simd_emit_binary_body(section, 110, false)",   # i8x16.add
    "wasm_simd_emit_shift_body(section, 107)",           # i8x16.shl
    "wasm_simd_emit_binary_body(section, 142, false)",   # i16x8.add
    "wasm_simd_emit_binary_body(section, 149, false)",   # i16x8.mul
    "wasm_simd_emit_integer_splat_body(section, 16)",
    "wasm_simd_emit_i32_scalar_mask_body(section, 132)", # i16x8.bitmask
    "wasm_simd_emit_shift_body(section, 139)",           # i16x8.shl
    "wasm_simd_emit_shift_body(section, 171)",           # i32x4.shl
]
for token in opcode_paths:
    if token not in simd:
        raise SystemExit(f"missing expanded SIMD lowering: {token}")

if "wasm_simd_emit_runtime_body" not in codegen:
    raise SystemExit("SIMD runtime body is not wired into wasm codegen")
if "src/raz_codegen_wasm/src/wasm/simd.rz" not in order:
    raise SystemExit("SIMD module missing from semantic compiler source graph")
print(f"wasm-simd: PASS ({len(new_runtime_names)} expanded runtime operations + legacy i64x4/f64x2)")
