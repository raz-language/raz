#!/usr/bin/env python3
# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0
from pathlib import Path
import sys
root=Path(__file__).resolve().parents[1]
required=[
 'compiler/src/mir/core/model.rz','compiler/src/mir/core/builder.rz','compiler/src/mir/lowering.rz',
 'compiler/src/mir/analysis/cfg.rz','compiler/src/mir/verify/verifier.rz',
 'compiler/src/mir/transform/pipeline.rz','compiler/src/mir/interpreter.rz']
problems=[]
for rel in required:
 p=root/rel
 if not p.is_file(): problems.append(f'missing {rel}')
model=(root/'compiler/src/mir/core/model.rz').read_text()
hir=(root/'compiler/src/hir/core/model.rz').read_text()
pipeline=(root/'compiler/src/mir/transform/pipeline.rz').read_text()
main=(root/'compiler/src/main.rz').read_text()
lowering=(root/'compiler/src/mir/lowering.rz').read_text()
if 'public struct MirModule' not in model: problems.append('MirModule is not MIR-owned')
if 'public struct MirModule' in hir: problems.append('HIR still owns MirModule')
if 'verify_mir_structure' not in pipeline: problems.append('pipeline does not verify MIR')
if 'run_mir_pipeline(&mut mir, llvm_options.optimization_kind)' not in main: problems.append('production compiler bypasses optimization-aware MIR pipeline')
if len(lowering.splitlines()) >= 3500: problems.append('lowering.rz regressed above 3500 lines')
if problems:
 print('mir2-architecture: FAIL')
 for p in problems: print(' ',p)
 sys.exit(1)
print('mir2-architecture: PASS')
print('  MIR owns its model/builder; lowering, CFG analysis, verification, transform pipeline, and interpreter are separate modules')
print(f'  lowering.rz: {len(lowering.splitlines())} lines')
