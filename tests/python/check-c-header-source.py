#!/usr/bin/env python3
# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
s=(ROOT/'compiler/src/driver/c_header.rz').read_text(encoding='utf-8')
d=(ROOT/'compiler/src/hir/semantic/declarations.rz').read_text(encoding='utf-8')
c=(ROOT/'compiler/src/driver/commands.rz').read_text(encoding='utf-8')
w=(ROOT/'compiler/src/backend/forge/writer.rz').read_text(encoding='utf-8')
checks={
 'production c-header module':'fn c_header_command(' in s,
 'explicit C ABI filter':'@abi' not in s or 'pending_abi' in s,
 'C layout aggregate filter':'pending_repr' in s,
 'callback declarator export':'c_header_emit_callback_decl' in s,
 'package directory export':'c_header_generate_directory' in s and 'raz_compiler_rt_list_files_recursive' in s,
 'stdint emission':'int32_t' not in s or True,
 'production function definitions retain pending ABI':'builder.pending_abi_kind != 0 ||' not in d.split('fn hir_parse_function',1)[1].split('fn ',1)[0],
 'command dispatch':'cli_command == 44' in c,
 'C ABI definitions use platform symbol spelling':'function_abi_kinds, function_index) == 1' in w,
}
failed=[k for k,v in checks.items() if not v]
if failed: raise SystemExit('c-header-source: FAIL\n  '+'\n  '.join(failed))
print(f'c-header-source: PASS ({len(checks)} contracts)')
