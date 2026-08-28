#!/usr/bin/env python3
# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0
from pathlib import Path
import sys
ROOT=Path(__file__).resolve().parents[2]
source=(ROOT/'compiler/src/raz_driver/src/bindgen.rz').read_text(encoding='utf-8')
cli=(ROOT/'compiler/src/raz_driver/src/cli.rz').read_text(encoding='utf-8')
commands=(ROOT/'compiler/src/raz_driver/src/commands.rz').read_text(encoding='utf-8')
order={path.relative_to(ROOT/'compiler').as_posix() for path in list((ROOT/'compiler').rglob('*.rz'))}
checks={
 'bindgen production module':'src/raz_driver/src/bindgen.rz' in order,
 'bindgen CLI command':'cli_arg_equals_literal(value, length, "bindgen")' in cli and 'return 43;' in cli,
 'bindgen dispatch':'cli_command == 43' in commands and 'bindgen_command(process_argc)' in commands,
 'C layout structs':'@repr(C)' in source or '114,101,112,114,40,67,41' in source,
 'Windows/Unix long policy':'windows_abi' in source and 'w_long' in source,
 'typedef substitution':'BindgenAliasTable' in source and 'bindgen_alias_find' in source,
 'function pointer typedefs':'bindgen_emit_function_pointer_type' in source,
 'union storage carrier':'bindgen_emit_union_storage' in source,
 'integral bitfield storage':'bindgen_emit_bitfield_storage' in source and 'bindgen_bitfield_capacity' in source,
 'C integer macro normalization':'bindgen_emit_macro_expr' in source,
 'bounded conditional preprocessing':'BindgenMacroTable' in source and 'bindgen_pp_eval' in source and 'pp_active' in source,
 'inline anonymous aggregate storage':'bindgen_emit_inline_anonymous_aggregate' in source and 'bindgen_inline_aggregate_size_align' in source,
 'C ABI externs':'@abi(C)extern fn ' in source,
}
failed=[k for k,v in checks.items() if not v]
if failed:
 print('bindgen-source: FAIL')
 for x in failed: print('  '+x)
 sys.exit(1)
print(f'bindgen-source: PASS ({len(checks)} contracts)')
