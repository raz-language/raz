# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0

from pathlib import Path
ui = Path('raz/library/web/ui/ui.rz').read_text()
fixture = Path('raz/tests/examples/web/effects-lifecycle/src/main.rz').read_text()
checks = {
    'effect record storage': 'WEB_EFFECT_RECORD_BYTES' in ui and 'web_effect_records' in ui,
    'render generation': 'web_render_generation += 1;' in ui and 'fn render_generation(Component& self) -> i64' in ui,
    'first render lifecycle': 'fn first_render(Component& self) -> bool' in ui,
    'i64 effect': 'fn effect_i64(Component& self, StateI64& state) -> bool' in ui,
    'bool effect': 'fn effect_bool(Component& self, StateBool& state) -> bool' in ui,
    'string effect': 'fn effect_string(Component& self, StateString& state) -> bool' in ui,
    'http effect': 'fn effect_request(Component& self, HttpRequest& request) -> bool' in ui,
    'resource effect': 'fn changed_in(Resource<T>& self, Component& owner) -> bool' in ui,
    'effect marks scoped state structural dependency': 'state_mark_structural_scope(state.slot, self.component_scope)' in ui,
    'effect marks scoped http structural dependency': 'http_mark_structural_scope(request.slot, self.component_scope)' in ui,
    'no callback scheduler': 'WEB_EFFECT_CALLBACK' not in ui and 'effect_callback' not in ui,
    'fixture lifecycle': 'page.first_render()' in fixture,
    'fixture state effect': 'page.effect_i64(&mut count)' in fixture,
    'fixture string effect': 'page.effect_string(&mut name)' in fixture,
}
failed=[name for name, ok in checks.items() if not ok]
if failed:
    for name in failed: print(f'web-effects-pass106: FAIL: {name}')
    raise SystemExit(1)
print(f'web-effects-pass106: PASS ({len(checks)} checks)')
