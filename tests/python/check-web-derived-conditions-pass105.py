# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0

from pathlib import Path
ROOT = Path(__file__).resolve().parents[2]
ui = (ROOT / 'library/web/ui/ui.rz').read_text(encoding='utf-8')
checks = {
    'derived bool type': 'public struct DerivedBool' in ui,
    'state equality': 'public fn derived_eq(StateI64& left, StateI64& right) -> DerivedBool' in ui,
    'state greater-equal': 'public fn derived_gte(StateI64& left, StateI64& right) -> DerivedBool' in ui,
    'constant equality': 'public fn derived_eq_const(StateI64& source, i64 value) -> DerivedBool' in ui,
    'constant greater-equal': 'public fn derived_gte_const(StateI64& source, i64 value) -> DerivedBool' in ui,
    'computed add constant': 'public fn derived_add_const(StateI64& source, i64 value) -> DerivedI64' in ui,
    'computed subtract constant': 'public fn derived_sub_const(StateI64& source, i64 value) -> DerivedI64' in ui,
    'scoped derived read': 'fn read_derived_bool(Component& self, DerivedBool& value) -> bool' in ui,
    'derived conditional': 'fn child_when_derived(Component&mut self, DerivedBool& value, Component&mut child) -> bool' in ui,
    'derived keyed conditional': 'fn child_when_derived_keyed(Component&mut self, DerivedBool& value, string key, Component&mut child) -> bool' in ui,
    'source scope propagation': 'derived_mark_structural_scope(value.id, self.component_scope)' in ui,
    'resource scoped loading': 'fn loading_in(Resource<T>& self, Component& owner) -> bool' in ui,
    'resource scoped ready': 'fn ready_in(Resource<T>& self, Component& owner) -> bool' in ui,
    'resource scoped failure': 'fn failed_in(Resource<T>& self, Component& owner) -> bool' in ui,
}
failed = [name for name, ok in checks.items() if not ok]
if failed:
    for name in failed: print(f'web-derived-conditions-pass105: FAIL: {name}')
    raise SystemExit(1)
print(f'web-derived-conditions-pass105: PASS ({len(checks)} checks)')
