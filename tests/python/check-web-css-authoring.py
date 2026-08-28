#!/usr/bin/env python3
# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0

from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
ui = (ROOT / "library/web/ui/ui.rz").read_text()
page = (ROOT / "library/web/src/lib.rz").read_text()
fixture = (ROOT / "tests/examples/web/css-authoring/src/main.rz").read_text()
checks = {
    "component pseudo class": "fn css_pseudo(Component&mut self" in ui,
    "component pseudo element": "fn css_pseudo_element(Component&mut self" in ui,
    "component media": "fn css_media(Component&mut self" in ui,
    "component container": "fn css_container(Component&mut self" in ui,
    "component variable": "fn css_variable(Component&mut self" in ui,
    "component keyframes": "fn css_keyframes(Component&mut self" in ui,
    "component structured dedupe": "fn append_unique_rule(StyleSheet&mut self" in ui,
    "page pseudo class": "public fn css_pseudo(Page&mut self" in page,
    "page pseudo element": "public fn css_pseudo_element(Page&mut self" in page,
    "page media": "public fn css_media(Page&mut self" in page,
    "page container": "public fn css_container(Page&mut self" in page,
    "page variable": "public fn css_variable(Page&mut self" in page,
    "page keyframes": "public fn css_keyframes(Page&mut self" in page,
    "fixture duplicates rule": fixture.count('page.css_class("pulse"') == 2,
}
failed=[name for name, ok in checks.items() if not ok]
if failed:
    for name in failed: print(f"web-css-authoring: FAIL: {name}")
    raise SystemExit(1)
print(f"web-css-authoring: PASS ({len(checks)}/{len(checks)})")
