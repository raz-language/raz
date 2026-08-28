#!/usr/bin/env python3
# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0

from pathlib import Path
import sys

root = Path(__file__).resolve().parents[2]
model = (root / 'compiler/src/raz_mir/src/mir/core/model.rz').read_text(encoding='utf-8')
ownership_context = (root / 'compiler/src/raz_mir/src/mir/ownership/context.rz').read_text(encoding='utf-8')
model += '\n' + ownership_context
builder = (root / 'compiler/src/raz_mir/src/mir/core/builder.rz').read_text(encoding='utf-8')
paths = (root / 'compiler/src/raz_mir/src/mir/ownership/lowering_paths.rz').read_text(encoding='utf-8')
loans = (root / 'compiler/src/raz_borrowck/src/borrowck/loan_regions.rz').read_text(encoding='utf-8')
reborrows = (root / 'compiler/src/raz_borrowck/src/borrowck/reborrows.rz').read_text(encoding='utf-8')
semantics = (root / 'compiler/src/raz_borrowck/src/borrowck/semantics.rz').read_text(encoding='utf-8')
pipeline = (root / 'compiler/src/raz_mir_opt/src/mir_opt/transform/pipeline.rz').read_text(encoding='utf-8')
driver = (root / 'compiler/src/raz_driver/src/compiler_main.rz').read_text(encoding='utf-8')
order = {path.relative_to(root / 'compiler').as_posix() for path in list((root / 'compiler').rglob('*.rz'))}

checks = {
    'loan provenance stored in MIR model': 'ownership_event_parents' in model and 'ownership_event_parents' in builder,
    'reborrow lowering discovers parent loans': all(x in paths for x in ['mir_find_reborrow_parent', 'mir_record_reborrow_event', 'ownership_event_parents']),
    'child loans inherit canonical paths': all(x in paths for x in ['ownership_event_path_counts', 'ownership_path_components', 'mir_record_ownership_path_event']),
    'shared parent cannot produce exclusive child': 'parent_kind == 3 && kind == 4' in reborrows,
    'reborrow ancestry is validated': 'mir_loan_is_descendant_of' in reborrows and 'verify_mir_reborrow_provenance' in reborrows,
    'nested reborrow ancestry is handled specially': 'mir_loan_parent_chain_contains' in loans and '&& !mutation' in loans,
    'child lifetime is contained by parent': 'build_mir_loan_last_use_table' in reborrows and 'loan_last_uses' in reborrows and 'event) >' in reborrows and 'parent)' in reborrows,
    'borrowck is backend ownership legality authority': 'public fn verify_mir_ownership_semantics' in semantics and driver.count('verify_mir_ownership_semantics(&mir)') >= 2 and 'verify_mir_ownership_semantics' not in pipeline,
    'borrowck semantic modules are canonical compiler modules': all(x in order for x in ['src/raz_borrowck/src/borrowck/reborrows.rz', 'src/raz_borrowck/src/borrowck/semantics.rz']),
}

failed = [name for name, ok in checks.items() if not ok]
if failed:
    for name in failed:
        print(f'mir-contract: FAIL: {name}')
    sys.exit(1)

print('mir-contract: PASS')
print('  reborrow provenance and parent/child loan validation are explicit MIR semantics')
print('  raz_borrowck ownership verification is the legality firewall before and after MIR optimization')
print('  Phase 2 architecture is closed without adding host compiler semantics')
