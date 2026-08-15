#!/usr/bin/env python3
# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0

"""Freeze the WASM backend's complete MIR coverage contract."""
from pathlib import Path

root = Path(__file__).resolve().parents[1]
codegen = (root / "compiler/src/backend/wasm/codegen.rz").read_text(encoding="utf-8")
async_src = (root / "compiler/src/backend/wasm/async.rz").read_text(encoding="utf-8")

opcodes = {
    1: "const", 2: "parameter", 3: "local_load", 4: "add", 5: "sub", 6: "mul", 7: "div",
    8: "direct_call", 9: "local_store", 10: "return_value", 11: "eq", 12: "ne", 13: "lt",
    14: "le", 15: "gt", 16: "ge", 17: "select", 18: "logical_not", 19: "logical_and",
    20: "logical_or", 21: "return_void", 22: "branch", 23: "branch_if", 24: "block_value",
    25: "local_ref", 26: "ref_load", 27: "ref_store", 28: "aggregate_load", 29: "aggregate_store",
    30: "aggregate_alloc", 31: "aggregate_destroy", 32: "ownership_marker", 33: "owned_local_marker",
    34: "remainder", 35: "bit_and", 36: "bit_or", 37: "bit_xor", 38: "shift_left",
    39: "shift_right", 40: "numeric_cast", 41: "await", 42: "string_literal", 43: "function_ref",
    44: "indirect_call", 45: "element_ref", 46: "spawn_marker", 47: "global_load",
    48: "global_store", 49: "global_ref", 50: "closure_create", 51: "closure_call",
}

for opcode in sorted(opcodes):
    explicit = f"opcode == {opcode}" in codegen
    ranged = False
    if 4 <= opcode <= 7:
        ranged = "opcode >= 4 && opcode <= 7" in codegen
    elif 11 <= opcode <= 16:
        ranged = "opcode >= 11 && opcode <= 16" in codegen
    elif 34 <= opcode <= 39:
        ranged = "opcode >= 34 && opcode <= 39" in codegen
    if not (explicit or ranged):
        raise SystemExit(f"wasm-mir-coverage: missing MIR opcode {opcode} ({opcodes[opcode]})")

if "return false;" not in codegen.split("fn wasm_opcode_runtime_deferred", 1)[1].split("fn wasm_opcode_supported", 1)[0]:
    raise SystemExit("wasm-mir-coverage: runtime-deferred classification must be empty")

for marker in (
    "fn wasm_async_emit_poll_body",
    "opcode == 41",
    "wasm_async_emit_await",
    "wasm_async_store_frame_state",
    "wasm_async_poll_type_index",
):
    if marker not in codegen and marker not in async_src:
        raise SystemExit(f"wasm-mir-coverage: missing async lowering marker: {marker}")

signature = codegen.split("fn wasm_signature_supported", 1)[1].split("fn wasm_opcode_runtime_deferred", 1)[0]
if "function_return_function_types" in signature or "parameter_function_types" in signature:
    raise SystemExit("wasm-mir-coverage: first-class callable signatures are still rejected")
if "function_is_async" in signature:
    raise SystemExit("wasm-mir-coverage: async functions are still rejected at signature validation")

store = codegen.split("if (opcode == 29)", 1)[1].split("if (opcode == 30)", 1)[0]
if "wasm_emit_register_set(out, mir, function_index, ip);" not in store:
    raise SystemExit("wasm-mir-coverage: aggregate_store must preserve its MIR result value")

print("wasm-mir-coverage: PASS (51/51 live MIR opcodes emitted; await uses resumable wasm async frames)")
