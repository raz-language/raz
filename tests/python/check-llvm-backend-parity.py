# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0

from pathlib import Path
import re

root = Path(__file__).resolve().parents[2]
llvm_files = [
    root / "compiler/src/backend/llvm/writer.rz",
    root / "compiler/src/backend/llvm/globals_codegen.rz",
    root / "compiler/src/backend/llvm/codegen.rz",
]
# The production LLVM backend is intentionally split across these modules;
# audit the backend as a whole rather than coupling opcode coverage to one file.
llvm = "\n".join(path.read_text(encoding="utf-8") for path in llvm_files)
mir = "\n".join(path.read_text(encoding="utf-8") for path in sorted((root / "compiler/src/mir").rglob("*.rz")))

required_cases = {1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,41,42,43,44,45,46,47,48,49}
seen = {int(x) for x in re.findall(r"opcode == (\d+)", llvm)}
missing = sorted(required_cases - seen)
if missing:
    raise SystemExit(f"llvm-backend-parity: FAIL missing MIR opcode cases: {missing}")

required_closure_markers = [
    "hir_append_closure_call_captures",
    "function_closure_capture_counts",
    "mir_emit_typed(mir, 44",
]
combined = mir + (root / "compiler/src/hir/semantic/ownership.rz").read_text(encoding="utf-8")
for marker in required_closure_markers:
    if marker not in combined:
        raise SystemExit(f"llvm-backend-parity: FAIL missing closure lowering marker: {marker}")

async_markers = [
    "fn llvm_emit_async_function",
    "fn llvm_async_emit_state_store",
    "fn llvm_async_emit_slot_load",
    "fn llvm_async_emit_slot_store",
    "async_decl_0",
    "async_decl_10",
    "async_decl_11",
    "writer_async_function_name",
    "return llvm_emit_async_function",
]
for marker in async_markers:
    if marker not in llvm:
        raise SystemExit(f"llvm-backend-parity: FAIL missing async lowering marker: {marker}")

ownership_markers = [
    "fn llvm_async_emit_slot_store_owned",
    "fn llvm_async_emit_slot_disarm",
    "fn llvm_emit_async_structure_cleanup_function",
    "async_decl_13",
    "async_decl_14",
    "hir_structure_needs_drop",
]
for marker in ownership_markers:
    if marker not in llvm:
        raise SystemExit(f"llvm-backend-parity: FAIL missing async ownership marker: {marker}")

merge_markers = [
    "fn llvm_async_block_argument_count",
    "fn llvm_async_block_argument_ordinal",
    "block_argument_count",
    "local_count + parameter_count + await_count + ordinal",
]
for marker in merge_markers:
    if marker not in llvm:
        raise SystemExit(f"llvm-backend-parity: FAIL missing async merge marker: {marker}")

print("llvm-backend-parity: PASS (49 MIR opcode forms + hidden-capture closures + async state machine)")
print("llvm-backend-parity: PASS async owned-slot cleanup + frame-backed CFG merge values")
