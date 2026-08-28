#!/usr/bin/env python3
# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0
"""Verify MIR optimization is an independent policy package above core MIR."""
from pathlib import Path
import sys
ROOT=Path(__file__).resolve().parents[2]
SRC=ROOT/'compiler'/'src'
MIR=SRC/'raz_mir'
OPT=SRC/'raz_mir_opt'
DRIVER=SRC/'raz_driver'
problems=[]
manifest=(OPT/'raz.toml').read_text(encoding='utf-8') if (OPT/'raz.toml').is_file() else ''
if 'name = "raz-mir-opt"' not in manifest: problems.append('raz_mir_opt package manifest is missing or misnamed')
if 'mir = "../raz_mir"' not in manifest: problems.append('raz_mir_opt must depend on raz_mir')
for forbidden in ('hir = "../raz_hir"','frontend = "../raz_parser"','borrowck = "../raz_borrowck"'):
    if forbidden in manifest: problems.append(f'raz_mir_opt has unnecessary dependency: {forbidden}')
required=('simplify_cfg','const_prop','copy_prop','scalar','dce','cfg_cleanup','remap','pipeline')
for module in required:
    if not (OPT/'src'/'mir_opt'/'transform'/f'{module}.rz').is_file(): problems.append(f'missing optimizer module: {module}')
if (MIR/'src'/'mir'/'transform').exists(): problems.append('transform implementation leaked back into raz_mir')
mir_manifest=(MIR/'raz.toml').read_text(encoding='utf-8')
mir_source='\n'.join(p.read_text(encoding='utf-8') for p in (MIR/'src').rglob('*.rz'))
if 'raz_mir_opt' in mir_manifest or 'mir_opt::' in mir_source or 'raz_mir_opt::' in mir_source:
    problems.append('raz_mir depends back on raz_mir_opt')
driver_manifest=(DRIVER/'raz.toml').read_text(encoding='utf-8')
driver_main=(DRIVER/'src'/'compiler_main.rz').read_text(encoding='utf-8')
if 'mir_opt = "../raz_mir_opt"' not in driver_manifest: problems.append('driver missing raz_mir_opt dependency')
if 'run_mir_pipeline(&mut mir, llvm_options.optimization_kind)' not in driver_main: problems.append('driver does not orchestrate MIR optimization')
pipeline=(OPT/'src'/'mir_opt'/'transform'/'pipeline.rz').read_text(encoding='utf-8')
if 'verify_mir_ownership_semantics' in pipeline: problems.append('optimizer owns borrow-checking legality')
for api in ('verify_mir_structure','simplify_mir_cfg','propagate_mir_constants','propagate_mir_copies','simplify_mir_scalars','eliminate_mir_dead_values','cleanup_mir_cfg'):
    if api not in pipeline: problems.append(f'optimizer pipeline missing {api}')
if problems:
    print('mir-opt-package: FAIL')
    for x in problems: print('  '+x)
    sys.exit(1)
print('mir-opt-package: PASS')
print('  raz_mir owns IR, analysis, lowering, verification, and interpretation')
print('  raz_mir_opt owns optimization transforms/policy with one-way mir-opt -> mir dependency')
print('  raz_driver orchestrates borrowck -> mir-opt -> borrowck')
