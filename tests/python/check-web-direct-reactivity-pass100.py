#!/usr/bin/env python3
# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0

from pathlib import Path
ROOT = Path(__file__).resolve().parents[2]
ui = (ROOT / 'library' / 'web' / 'ui' / 'ui.rz').read_text(encoding='utf-8')
codegen = (ROOT / 'compiler' / 'src' / 'raz_codegen_web' / 'src' / 'web' / 'codegen.rz').read_text(encoding='utf-8')
checks = {
    'derived dependency metadata': 'data-raz-derived-left' in ui and 'data-raz-derived-right' in ui,
    'derived dependency records left source': 'data-raz-derived-left' in ui and 'raw_i64_get(dependency_record, 8)' in ui,
    'derived dependency records right source': 'data-raz-derived-right' in ui and 'raw_i64_get(dependency_record, 16)' in ui,
    'browser avoids extra derived dependency ABI': 'instance.exports.raz_web_derived_depends_on' not in codegen,
    'browser filters computed updates by changed slot': "node.getAttribute('data-raz-derived-left') !== String(changedSlot)" in codegen and "node.getAttribute('data-raz-derived-right') !== String(changedSlot)" in codegen,
    'scoped conditional helper': 'fn when(Component& self, StateBool& state) -> bool' in ui,
    'scoped inverse conditional helper': 'fn unless(Component& self, StateBool& state) -> bool' in ui,
    'keyed component string helper': 'fn child_keyed(Component&mut self, string key, Component&mut child) -> bool' in ui,
    'keyed component integer helper': 'fn child_keyed_i64(Component&mut self, i64 key, Component&mut child) -> bool' in ui,
    'host keyed reconciliation': "const key = nodeKey(newChild)" in codegen and 'current.insertBefore(match' in codegen,
    'no virtual DOM runtime': 'virtualdom' not in codegen.lower() and 'virtual-dom' not in codegen.lower(),
}
failed = [name for name, ok in checks.items() if not ok]
if failed:
    for name in failed: print(f'web-direct-reactivity-pass100: FAIL: {name}')
    raise SystemExit(1)
print(f'web-direct-reactivity-pass100: PASS ({len(checks)} checks)')
