#!/usr/bin/env python3
# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0

from pathlib import Path
import sys
root=Path(__file__).resolve().parents[2]
ui=(root/'library/web/ui/ui.rz').read_text()
ex=(root/'tests/examples/web/component-authoring/src/main.rz').read_text()
checks={
 'fragment tag':'Fragment,' in ui,
 'fragment factory':'public fn fragment() -> Component' in ui,
 'wrapper free':'if (tag == Tag::Fragment)' in ui and 'buffer_append_buffer(output, children)' in ui,
 'semantic constructors':'public fn main_component()' in ui and 'public fn article_component()' in ui,
 'safe root attr':'fn attr(Component&mut self' in ui,
 'aria helper':'fn aria_label(Component&mut self' in ui,
 'role helper':'fn role(Component&mut self' in ui,
 'typed props':'fn feature(string title, string copy) -> Component' in ex,
 'fragment example':'Component group = fragment();' in ex,
}
bad=[k for k,v in checks.items() if not v]
for k,v in checks.items(): print(('PASS' if v else 'FAIL')+': '+k)
if bad: sys.exit(1)
