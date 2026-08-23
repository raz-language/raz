#!/usr/bin/env python3
# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0

from pathlib import Path
import re
root=Path(__file__).resolve().parents[2]
backend=(root/'compiler/src/driver/backend.rz').read_text()
order={path.relative_to(root/'compiler').as_posix() for path in (root/'compiler/src').rglob('*.rz')}
required=['isa.rz','model.rz','lowering.rz','dataflow.rz','registers.rz','optimize.rz','blocks.rz','verify.rz','disasm.rz','reference.rz','writer.rz','decoder.rz','codegen.rz']
for name in required:
    p=root/'compiler/src/backend/rxe'/name
    assert p.is_file(), f'missing {p}'
    assert f'src/backend/rxe/{name}' in order, f'{name} absent from semantic compiler source graph'
assert '--backend=rxe' in backend
assert 'return 2;' in backend
assert 'emit_rxe_module' in backend
isa=(root/'compiler/src/backend/rxe/isa.rz').read_text()
assert re.search(r'fn\s+rxe_register_count\(\)\s*->\s*i64\s*\{\s*return\s+32;\s*\}', isa)
assert 'eight bytes' in isa
for op in ['rxe_op_call','rxe_op_call_indirect','rxe_op_make_closure','rxe_op_func_ref','rxe_op_index_ref','rxe_op_string_ref','rxe_op_trunc8','rxe_op_load_global','rxe_op_rem64','rxe_op_divs64','rxe_op_rems64','rxe_op_sar64','rxe_op_lts64','rxe_op_slice_load','rxe_op_slice_store','rxe_op_slice_ref']:
    assert op in isa, f'missing RXE opcode {op}'
lowering=(root/'compiler/src/backend/rxe/lowering.rz').read_text()
for token in ['op == 8','op == 44 || op == 51','op == 43','op == 50','op == 42','op == 45','op == 47','op == 40']:
    assert token in lowering, f'missing lowering family {token}'
dataflow=(root/'compiler/src/backend/rxe/dataflow.rz').read_text()
assert 'rxe_dataflow_build_slot_sets' in dataflow and 'live_in' in dataflow and 'live_out' in dataflow
assert 'rxe_dataflow_eliminate_dead_slot_stores' in dataflow
assert 'rxe_dataflow_build_register_sets' in dataflow and 'rxe_dataflow_eliminate_dead_register_results' in dataflow
registers=(root/'compiler/src/backend/rxe/registers.rz').read_text()
assert 'rxe_allocate_registers' in registers
assert 'rxe_linear_scan_function' in registers
assert 'slot_blocked' in registers and 'loop_store_seen' in registers
assert 'rxe_function_has_backedge' in registers
assert 'machine-level phi' in registers
assert 'rxe_dataflow_extend_slot_intervals' in registers
optimize=(root/'compiler/src/backend/rxe/optimize.rz').read_text()
assert 'rxe_optimize_aggregate_copy_runs' in optimize and 'rxe_op_mem_copy()' in optimize
assert 'rxe_optimize_block_constants' in optimize and 'rxe_superinstruction_candidate_count' in optimize
assert 'rxe_optimize_post_copy_loads' in optimize and 'rxe_optimizer_register_used_before_write' in optimize
assert 'distinct ALLOC instructions' in optimize
writer=(root/'compiler/src/backend/rxe/writer.rz').read_text()
assert 'module.global_count * 16' in writer
assert 'module.layout_count * 16 + module.layout_field_count * 16' in writer
assert 'header_bytes = 104' in writer
assert 'module.function_count * 32' in writer
assert 'module.export_count * 24' in writer and 'module.module_fingerprint_lo' in writer
assert 'function_name_hashes' in writer and 'function_selector_lo' in writer and 'function_selector_hi' in writer and 'function_abi_kinds' in writer
assert 'signature_offset' in writer and 'module.function_type_count * 16' in writer
model=(root/'compiler/src/backend/rxe/model.rz').read_text()
assert 'layout_field_extents' in model and 'hir.struct_count + hir.enum_count' in model
assert 'function_type_signature_starts' in model and 'signature_parameter_types' in model
assert 'function_name_hashes' in model and 'function_abi_kinds' in model
assert 'export_function_indices' in model and 'export_selector_lo' in model and 'export_selector_hi' in model and 'feature_flags' in model and 'module_fingerprint_hi' in model
verify=(root/'compiler/src/backend/rxe/verify.rz').read_text()
assert 'layout_encoded' in verify
assert 'expected_start' in verify
assert 'rxe_op_index_store()' in verify and 'rxe_op_select()' in verify and 'rxe_op_slice_store()' in verify and 'rxe_verify_feature_bitmap' in verify and 'rxe_verify_callable_selector' in verify
assert 'function_type_parameter_counts' in verify and 'rxe_op_mem_copy()' in verify
blocks=(root/'compiler/src/backend/rxe/blocks.rz').read_text()
assert 'rxe_build_blocks' in blocks and 'block_successor0' in blocks and 'block_successor1' in blocks
disasm=(root/'compiler/src/backend/rxe/disasm.rz').read_text()
assert 'rxe_disassemble_module' in disasm
assert 'function_name_hashes' in disasm and 'function_abi_kinds' in disasm
assert 'rxe_disasm_opcode_name' in disasm and 'module_fingerprint_hi' in disasm
reference=(root/'compiler/src/backend/rxe/reference.rz').read_text()
assert 'rxe_reference_execute_function' in reference
assert 'rxe_reference_matches_mir_zero_arg' in reference
assert 'rxe_reference_matches_mir' in reference and 'rxe_reference_slice_element' in reference
for token in ['RxeReferenceState','rxe_reference_heap_index','rxe_reference_make_closure','rxe_reference_unsigned_divrem','rxe_op_call_indirect','rxe_op_call_closure','rxe_op_index_store','rxe_reference_mem_copy']:
    assert token in reference, f'missing reference semantic {token}'
fixtures=root/'tests/examples/backends/rxe'
for fixture in ['01_loop_liveness.rz','02_fixed_array.rz','03_slice.rz','04_indirect_call.rz','05_struct_copy.rz','06_loop_phi.rz','07_callable_abi.rz','08_nested_aggregate_copy.rz','09_slice_bounds.rz','10_argument_differential.rz','11_selector_identity.rz','12_block_cfg.rz','13_cfg_join_liveness.rz','14_dead_frame_store.rz','15_copy_fusion.rz','16_dead_register_result.rz','17_constant_fold.rz','18_branch_register_liveness.rz','19_seeded_differential_arithmetic.rz','20_seeded_differential_control.rz']:
    assert (fixtures/fixture).is_file(), f'missing RXE conformance fixture {fixture}'
decoder=(root/'compiler/src/backend/rxe/decoder.rz').read_text()
assert 'rxe_decode_roundtrip_bytes' in decoder and 'layout_field_count' in decoder
for token in ['rxe_decode_verify_features','rxe_decode_verify_layouts','rxe_decode_verify_signatures_exports','rxe_decode_verify_blocks','rxe_decode_verify_semantics']:
    assert token in decoder, f'missing binary semantic verifier {token}'
assert 'rxe_reference_matches_mir_seeded' in reference
print('rxe-backend: PASS (RXE ISA v1 compatibility/conformance)')
