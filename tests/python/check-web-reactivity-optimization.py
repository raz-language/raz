#!/usr/bin/env python3
# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0

from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
ui = (ROOT / 'library' / 'web' / 'ui' / 'ui.rz').read_text(encoding='utf-8')
codegen = (ROOT / 'compiler' / 'src' / 'raz_codegen_web' / 'src' / 'web' / 'codegen.rz').read_text(encoding='utf-8')

checks = {
    'event exposes changed state slot': 'public fn raz_web_event_state_slot(i64 event_id) -> i64' in ui,
    'binding index is structure-revision keyed': 'bindingIndexRevision' in codegen and 'raz_web_structure_revision' in codegen,
    'binding index maps state slots': 'let bindingIndex = new Map()' in codegen and 'addBinding' in codegen,
    'string text bindings indexed': "data-raz-bind-string" in codegen and "'text-string'" in codegen,
    'i64 text bindings indexed': "data-raz-bind-i64" in codegen and "'text-i64'" in codegen,
    'input string bindings indexed': "data-raz-state-string" in codegen and "'input-string'" in codegen,
    'input i64 bindings indexed': "data-raz-state-i64" in codegen and "'input-i64'" in codegen,
    'input bool bindings indexed': "data-raz-state-bool" in codegen and "'input-bool'" in codegen,
    'direct event passes changed slot': 'applyStateBindings(changedSlot)' in codegen,
    'full reconciliation can still patch all bindings': 'applyStateBindings()) patchRoot' in codegen,
    'structure change invalidates binding index': 'bindingIndexRevision = null' in codegen,
    'derived bindings remain conservatively refreshed': 'derivedBindings' in codegen and 'raz_web_derived_i64' in codegen,
}
failed = [name for name, ok in checks.items() if not ok]
if failed:
    for name in failed:
        print(f'web-reactivity-optimization: FAIL: {name}')
    raise SystemExit(1)
print(f'web-reactivity-optimization: PASS ({len(checks)} checks)')
