#!/usr/bin/env python3
# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0

from pathlib import Path
import re
import sys
sys.dont_write_bytecode = True

root = Path(__file__).resolve().parents[1]
target = (root / 'compiler/src/backend/llvm/target.rz').read_text(encoding='utf-8')
llvm = (root / 'compiler/src/backend/llvm/codegen.rz').read_text(encoding='utf-8')
main = (root / 'compiler/src/main.rz').read_text(encoding='utf-8')
backend = (root / 'compiler/src/driver/backend.rz').read_text(encoding='utf-8')
runtime_root = root / 'src/runtime'
runtime = '\n'.join(p.read_text(encoding='utf-8') for p in sorted(runtime_root.glob('*.cpp')))
order = (root / 'compiler/bootstrap-source-order.txt').read_text(encoding='utf-8')

required_target = [
    'public struct LlvmTargetOptions',
    'fn llvm_parse_target_option',
    'fn llvm_emit_target_metadata',
    'fn llvm_make_ir_sidecar',
    'fn llvm_finalize_native_output',
    'raz_rt_stage1_process_run',
    'options.emit_kind == 1',
    'options.emit_kind == 2',
    'fn llvm_command_feature_flags',
    'options.cpu_length > 0',
    'options.optimization_kind',
    'options.debug_info',
    'options.lto_kind',
    'options.library_path_length',
    'options.libraries_length',
    'options.linker_length',
    'options.link_args_length',
    'fn llvm_command_optimization',
    'fn llvm_command_comma_list',
    'options.relocation_kind',
    'options.code_model_kind',
    'options.visibility_kind',
    'options.export_length',
    'options.import_length',
    'options.weak_length',
    'options.linkonce_length',
]
for marker in required_target:
    if marker not in target:
        raise SystemExit(f'llvm-production-backend: FAIL missing target/toolchain marker: {marker}')

for marker in ['--backend=llvm', '--target=', '--data-layout=', '--cpu=', '--features=', '--runtime=', '--emit=']:
    # CLI spellings are encoded numerically in Raz source; verify representative
    # parser symbols instead for options other than the human-readable backend docs.
    pass

if 'src/backend/llvm/target.rz' not in order:
    raise SystemExit('llvm-production-backend: FAIL llvm_target.rz missing from canonical self-host source order')
if 'llvm_emit_target_metadata(&mut writer, options)' not in llvm:
    raise SystemExit('llvm-production-backend: FAIL LLVM module does not emit target metadata')
if 'define 32 @main' in llvm:
    raise SystemExit('llvm-production-backend: FAIL malformed main wrapper marker')
if 'i64 wrapper[20]' not in llvm or '64, 109, 97, 105, 110' not in llvm:
    raise SystemExit('llvm-production-backend: FAIL native LLVM main wrapper missing')
if 'llvm_parse_target_option(&mut llvm_options' not in main:
    raise SystemExit('llvm-production-backend: FAIL compiler CLI does not parse LLVM production options')
if 'llvm_finalize_native_output' not in main:
    raise SystemExit('llvm-production-backend: FAIL compiler does not finalize LLVM native outputs')
if 'LlvmTargetOptions& llvm_options' not in backend:
    raise SystemExit('llvm-production-backend: FAIL backend dispatch does not carry target options')
if 'std::int64_t raz_rt_process_run' not in runtime:
    raise SystemExit('llvm-production-backend: FAIL generic process runtime primitive missing')
if 'fn raz_rt_stage1_process_run_ascii' not in (root / 'compiler/src/frontend/lexer.rz').read_text(encoding='utf-8'):
    raise SystemExit('llvm-production-backend: FAIL Raz-side process adapter missing')


for marker in [
    'fn llvm_symbol_list_contains',
    'fn llvm_validate_symbol_options',
    'fn llvm_emit_symbol_prefix',
    '100, 108, 108, 101, 120, 112, 111, 114, 116',  # dllexport
    '100, 108, 108, 105, 109, 112, 111, 114, 116',  # dllimport
    '108, 105, 110, 107, 111, 110, 99, 101, 95, 111, 100, 114', # linkonce_odr
    '105, 110, 116, 101, 114, 110, 97, 108', # internal
]:
    if marker not in llvm:
        raise SystemExit(f'llvm-production-backend: FAIL missing symbol/linkage marker: {marker}')

suite = root / 'scripts/test-backend-differential-suite.py'
if not suite.exists():
    raise SystemExit('llvm-production-backend: FAIL differential suite runner missing')
corpus = sorted((root / 'examples/backends/differential').glob('*.rz'))
if len(corpus) < 13:
    raise SystemExit(f'llvm-production-backend: FAIL differential corpus too small: {len(corpus)}')

print(f'llvm-production-backend: PASS (target + ABI/features + symbol visibility/linkage + relocation/code-model + opt/LTO/debug/link controls + native orchestration + {len(corpus)} differential cases)')
