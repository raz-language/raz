#!/usr/bin/env python3
# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0

from pathlib import Path
root = Path(__file__).resolve().parents[2]
required = {
    'library/web/std/dom.rz': ['raz_web_dom_set_attribute','raz_web_dom_focus','public fn add_class','public fn set_value','public fn exists','public fn click'],
    'library/web/std/storage.rz': ['public fn set','public fn get','raz_web_storage_value_write'],
    'library/web/std/location.rz': ['public fn href','public fn assign','public fn reload'],
    'library/web/std/history.rz': ['public fn push','public fn back','public fn forward'],
    'library/web/std/clipboard.rz': ['public fn write_text','raz_web_clipboard_write'],
    'library/web/std/events.rz': ['public struct InputEvent','public struct KeyboardEvent','public struct PointerEvent','EventKind::PointerDown','raz_web_event_stop_propagation'],
    'library/web/std/timers.rz': ['public fn set_timeout','public fn clear_timeout','raz_web_timer_set_timeout'],
    'compiler/src/raz_codegen_wasm/src/wasm/writer.rz': ['wasm_browser_import_mask','wasm_browser_import_index','24 + wasm_browser_import_count_cached'],
    'compiler/src/raz_codegen_wasm/src/wasm/codegen.rz': ['browser_type_count = 4','Raz Web single pointer/scalar primitive','Raz Web three-argument'],
    'compiler/src/raz_codegen_wasm/src/wasm/wasi.rz': ['dom_set_attribute", base + 15','storage_value_write", base + 15','clipboard_write", base + 14','event_code_length", base + 13','timer_set_timeout", base + 12','wasm_browser_emit_import_wrapper1','wasm_browser_emit_import_wrapper3'],
    'compiler/src/raz_codegen_web/src/web/codegen.rz': ['raz_web: razWeb','storage_value_length','history_push','clipboard_write','event_code_length','timer_set_timeout','dom_set_value'],
    'library/web/src/lib.rz': ['storage_value_length','location_href_length','history_push','clipboard_write','event_code_length','timer_set_timeout','browser_export'],
}
checks=0
for rel, needles in required.items():
    text=(root/rel).read_text()
    for needle in needles:
        assert needle in text, f'{rel}: missing {needle}'
        checks += 1
print(f'PASS: browser platform ABI regression ({checks} checks)')
