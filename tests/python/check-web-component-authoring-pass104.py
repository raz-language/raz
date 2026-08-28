#!/usr/bin/env python3
# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0

from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
UI = (ROOT / 'library/web/ui/ui.rz').read_text()
checks = {
    'typed input constructor': 'public fn email_input() -> Element',
    'label-for helper': 'public fn label_for(string control_id, string text) -> Element',
    'option helper': 'public fn option_value(string option_value, string text) -> Element',
    'component element composition': 'fn element(Component&mut self, Element&mut child) -> bool',
    'component keyed element composition': 'fn element_keyed(Component&mut self, string key, Element&mut child) -> bool',
    'generic root aria helper': 'fn aria(Component&mut self, string name, string value) -> bool',
    'root data helper': 'fn data(Component&mut self, string name, string value) -> bool',
    'aria expanded helper': 'fn aria_expanded(Element&mut self, bool value) -> bool',
    'textarea rows helper': 'fn rows(Element&mut self, i64 value) -> bool',
    'conditional child helper': 'fn child_when(Component&mut self, StateBool& state, Component&mut child) -> bool',
    'conditional keyed child helper': 'fn child_when_keyed(Component&mut self, StateBool& state, string key, Component&mut child) -> bool',
}
failed=[]
for name, needle in checks.items():
    if needle not in UI:
        failed.append(name)
    else:
        print(f'PASS: {name}')
if failed:
    for name in failed: print(f'FAIL: {name}')
    raise SystemExit(1)
print(f'PASS: {len(checks)}/{len(checks)} Pass 104 authoring contracts')
