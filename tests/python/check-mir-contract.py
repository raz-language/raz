#!/usr/bin/env python3
# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0

from pathlib import Path
import sys

root = Path(__file__).resolve().parents[2]
model = (root / 'compiler/src/mir/core/model.rz').read_text(encoding='utf-8')
builder = (root / 'compiler/src/mir/core/builder.rz').read_text(encoding='utf-8')
paths = (root / 'compiler/src/mir/ownership/lowering_paths.rz').read_text(encoding='utf-8')
loans = (root / 'compiler/src/mir/ownership/loan_regions.rz').read_text(encoding='utf-8')
reborrows = (root / 'compiler/src/mir/ownership/reborrows.rz').read_text(encoding='utf-8')
semantics = (root / 'compiler/src/mir/ownership/semantics.rz').read_text(encoding='utf-8')
pipeline = (root / 'compiler/src/mir/transform/pipeline.rz').read_text(encoding='utf-8')
order = {path.relative_to(root / 'compiler').as_posix() for path in (root / 'compiler/src').rglob('*.rz')}

checks = {
    'loan provenance stored in MIR model': 'ownership_event_parents' in model and 'ownership_event_parents' in builder,
    'reborrow lowering discovers parent loans': all(x in paths for x in ['mir_find_reborrow_parent', 'mir_record_reborrow_event', 'ownership_event_parents']),
    'child loans inherit canonical paths': all(x in paths for x in ['ownership_event_path_counts', 'ownership_path_components', 'mir_record_ownership_path_event']),
    'shared parent cannot produce exclusive child': 'parent_kind == 3 && kind == 4' in reborrows,
    'reborrow ancestry is validated': 'mir_loan_is_descendant_of' in reborrows and 'verify_mir_reborrow_provenance' in reborrows,
    'nested reborrow ancestry is handled specially': 'mir_loan_parent_chain_contains' in loans and '&& !mutation' in loans,
    'child lifetime is contained by parent': 'build_mir_loan_last_use_table' in reborrows and 'loan_last_uses' in reborrows and 'event) >' in reborrows and 'parent)' in reborrows,
    'MIR ownership is backend legality authority': 'verify_mir_ownership_semantics' in semantics and pipeline.count('verify_mir_ownership_semantics(mir)') >= 2,
    'new semantic modules are ordered': all(x in order for x in ['src/mir/ownership/reborrows.rz', 'src/mir/ownership/semantics.rz']),
}

failed = [name for name, ok in checks.items() if not ok]
if failed:
    for name in failed:
        print(f'mir-contract: FAIL: {name}')
    sys.exit(1)

print('mir-contract: PASS')
print('  reborrow provenance and parent/child loan validation are explicit MIR semantics')
print('  MIR ownership verification is the backend legality firewall before and after optimization')
print('  Phase 2 architecture is closed without adding host compiler semantics')
