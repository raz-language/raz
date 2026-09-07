#!/usr/bin/env python3
# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0

from pathlib import Path
from web_source import web_codegen_source, web_bundle_source

ROOT = Path(__file__).resolve().parents[2]
UI = '\n'.join(p.read_text(encoding='utf-8') for p in sorted((ROOT / 'library' / 'web' / 'ui').glob('*.rz')))
CODEGEN = web_codegen_source(ROOT)

checks = {
    'derived dependency metadata': 'data-raz-derived-left' in UI and 'data-raz-derived-right' in UI,
    'derived dependency records left source': 'data-raz-derived-left' in UI and 'raw_i64_get(dependency_record, 8)' in UI,
    'derived dependency records right source': 'data-raz-derived-right' in UI and 'raw_i64_get(dependency_record, 16)' in UI,
    'browser avoids extra derived dependency ABI': 'instance.exports.raz_web_derived_depends_on' not in CODEGEN,
    'browser filters computed updates by changed slot': "node.getAttribute('data-raz-derived-left') !== String(changedSlot)" in CODEGEN and "node.getAttribute('data-raz-derived-right') !== String(changedSlot)" in CODEGEN,
    'scoped conditional helper': 'fn when(Component& self, StateBool& state) -> bool' in UI,
    'scoped inverse conditional helper': 'fn unless(Component& self, StateBool& state) -> bool' in UI,
    'keyed component string helper': 'fn child_keyed(Component&mut self, string key, Component&mut child) -> bool' in UI,
    'keyed component integer helper': 'fn child_keyed_i64(Component&mut self, i64 key, Component&mut child) -> bool' in UI,
    'host keyed reconciliation': "const key = nodeKey(newChild)" in CODEGEN and 'current.insertBefore(match' in CODEGEN,
    'no virtual DOM runtime': 'virtualdom' not in CODEGEN.lower() and 'virtual-dom' not in CODEGEN.lower(),
}
failed = [name for name, ok in checks.items() if not ok]
if failed:
    for name in failed:
        print(f'web-direct-reactivity: FAIL: {name}')
    raise SystemExit(1)
print(f'web-direct-reactivity: PASS ({len(checks)} checks)')
