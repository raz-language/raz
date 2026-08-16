#!/usr/bin/env python3
# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0

from pathlib import Path
import re, subprocess, sys
root=Path(__file__).resolve().parents[2]
isa=(root/'compiler/src/backend/rxe/isa.rz').read_text()
decoder=(root/'compiler/src/backend/rxe/decoder.rz').read_text()
reference=(root/'compiler/src/backend/rxe/reference.rz').read_text()
assert 'rxe_isa_version() -> i64 { return 1; }' in isa
assert 'rxe_format_version() -> i64 { return 7; }' in isa
ops=re.findall(r'fn rxe_op_([a-z0-9_]+)\(\) -> i64 \{ return (\d+); \}',isa)
assert len(ops)==68, f'RXE v1 expects 68 ISA opcodes, found {len(ops)}'
assert len({int(n) for _,n in ops})==len(ops), 'duplicate opcode number'
for token in ['rxe_decode_verify_features','rxe_decode_verify_layouts','rxe_decode_callable_selector','rxe_decode_verify_signatures_exports','rxe_decode_verify_blocks','rxe_decode_verify_semantics']:
    assert token in decoder, f'missing independent decoder semantic check {token}'
assert 'return rxe_decode_verify_semantics(data, length, header);' in decoder
assert 'rxe_reference_matches_mir_seeded' in reference and 'rxe_reference_conformance_next' in reference
subprocess.run([sys.executable,str(root/'tools/generate-rxe-reference.py')],check=True,cwd=root,stdout=subprocess.DEVNULL)
manifest=(root/'docs/RXE-ISA-v1.md').read_text()
assert 'RXE ISA: **v1**' in manifest
for name,num in ops:
    assert f'| {num} | `{name.replace("_", ".")}` |' in manifest, f'manifest missing opcode {name}'
for case in ['bad-binary-features.case','bad-binary-selector.case','bad-binary-layout-range.case','bad-binary-signature-range.case','bad-binary-export-order.case','bad-binary-block-range.case']:
    assert (root/'tests/rxe-invalid'/case).is_file(), f'missing binary corruption case {case}'
print('rxe-compatibility: PASS (ISA v1 semantic manifest + independent binary semantic checks)')
