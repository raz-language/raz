#!/usr/bin/env python3
# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0

from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
UI = '\n'.join(p.read_text(encoding='utf-8') for p in sorted((ROOT / 'library' / 'web' / 'ui').glob('*.rz')))
EXAMPLE = (ROOT / 'tests' / 'examples' / 'web' / 'component-authoring' / 'src' / 'main.rz').read_text(encoding='utf-8')

checks = {
    'fragment tag': 'Fragment,' in UI,
    'fragment factory': 'public fn fragment() -> Component' in UI,
    'wrapper free fragments': 'if (tag == Tag::Fragment)' in UI and 'buffer_append_buffer(output, children)' in UI,
    'semantic component constructors': 'public fn main_component()' in UI and 'public fn article_component()' in UI,
    'safe root attr': 'fn attr(Component&mut self' in UI,
    'aria label helper': 'fn aria_label(Component&mut self' in UI,
    'role helper': 'fn role(Component&mut self' in UI,
    'typed input constructor': 'public fn email_input() -> Element' in UI,
    'label-for helper': 'public fn label_for(string control_id, string text) -> Element' in UI,
    'option helper': 'public fn option_value(string option_value, string text) -> Element' in UI,
    'component element composition': 'fn element(Component&mut self, Element&mut child) -> bool' in UI,
    'component keyed element composition': 'fn element_keyed(Component&mut self, string key, Element&mut child) -> bool' in UI,
    'generic root aria helper': 'fn aria(Component&mut self, string name, string value) -> bool' in UI,
    'root data helper': 'fn data(Component&mut self, string name, string value) -> bool' in UI,
    'aria expanded helper': 'fn aria_expanded(Element&mut self, bool value) -> bool' in UI,
    'textarea rows helper': 'fn rows(Element&mut self, i64 value) -> bool' in UI,
    'conditional child helper': 'fn child_when(Component&mut self, StateBool& state, Component&mut child) -> bool' in UI,
    'conditional keyed child helper': 'fn child_when_keyed(Component&mut self, StateBool& state, string key, Component&mut child) -> bool' in UI,
    'typed props example': 'fn feature(string title, string copy) -> Component' in EXAMPLE,
    'fragment example': 'Component group = fragment();' in EXAMPLE,
}
failed = [name for name, ok in checks.items() if not ok]
if failed:
    for name in failed:
        print(f'web-component-authoring: FAIL: {name}')
    raise SystemExit(1)
print(f'web-component-authoring: PASS ({len(checks)} checks)')
