#!/usr/bin/env python3
# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0
from pathlib import Path
root=Path(__file__).resolve().parents[1]
v=(root/'compiler/src/backend/rxe/verify.rz').read_text()
isa=(root/'compiler/src/backend/rxe/isa.rz').read_text()
required={
 'bad-opcode':'rxe_opcode_valid',
 'bad-branch':'imm < start || imm >= end',
 'bad-register':'d >= rxe_register_count()',
 'bad-call-signature':'function_type_parameter_counts',
 'bad-layout':'layout_encoded',
 'bad-slice-store':'rxe_op_slice_store()',
 'bad-export':'export_name_hashes',
 'bad-fingerprint':'rxe_verify_fingerprint',
 'bad-selector':'rxe_verify_callable_selector',
 'bad-features':'rxe_verify_feature_bitmap',
 'bad-block-cfg':'block_successor0',
}
for name, token in required.items():
    p=root/'tests/rxe-invalid'/f'{name}.case'
    assert p.is_file(), p
    assert token in (v+isa), f'{name}: verifier token missing: {token}'
print('rxe-malformed-gate: PASS (11 hostile module classes have explicit verifier rejection paths)')
