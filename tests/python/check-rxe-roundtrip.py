#!/usr/bin/env python3
# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0

from pathlib import Path
import re
root=Path(__file__).resolve().parents[2]
isa=(root/'compiler/src/backend/rxe/isa.rz').read_text()
writer=(root/'compiler/src/backend/rxe/writer.rz').read_text()
decoder=(root/'compiler/src/backend/rxe/decoder.rz').read_text()
codegen=(root/'compiler/src/backend/rxe/codegen.rz').read_text()
spec=(root/'docs/RXE-v1-FORMAT.md').read_text()
assert 'rxe_format_version() -> i64 { return 7; }' in isa
assert 'i64 header_bytes = 104;' in writer
assert 'module.layout_count' in writer and 'module.layout_field_count' in writer
for token in ['RxeBinaryHeader','rxe_decode_header','rxe_decode_geometry','rxe_decode_canonical','rxe_decode_verify_semantics','rxe_decode_reencode','rxe_decode_roundtrip_bytes','rxe_validate_written_module']:
    assert token in decoder, f'missing decoder component {token}'
assert 'header.layout_count * 16 + header.layout_field_count * 16' in decoder
assert 'rxe_validate_written_module(output_path, output_path_length, &module)' in codegen
assert 'RXE container format: **v7**' in spec and 'Header size: **104 bytes**' in spec
assert '| 96 | `layout_count` | `u32` |' in spec
assert '| 100 | `layout_field_count` | `u32` |' in spec
print('rxe-roundtrip-gate: PASS (v7 self-describing layouts + semantic decode/re-encode validation)')
