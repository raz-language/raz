#!/usr/bin/env python3
# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0

from pathlib import Path
from web_source import web_codegen_source
root = Path(__file__).resolve().parents[2]
required = {
    'library/web/std/dom.rz': ['raz_web_dom_set_attribute','raz_web_dom_focus','public fn add_class','public fn set_value','public fn exists','public fn click'],
    'library/web/std/storage.rz': ['public fn set','public fn get','raz_web_storage_value_write'],
    'library/web/std/location.rz': ['public fn href','public fn assign','public fn reload'],
    'library/web/std/history.rz': ['public fn push','public fn back','public fn forward'],
    'library/web/std/clipboard.rz': ['public fn write_text','raz_web_clipboard_write'],
    'library/web/std/events.rz': ['public struct InputEvent','public struct KeyboardEvent','public struct PointerEvent','EventKind::PointerDown','raz_web_event_stop_propagation'],
    'library/web/std/timers.rz': ['public fn set_timeout','public fn clear_timeout','raz_web_timer_set_timeout'],
    'compiler/src/raz_codegen_wasm/src/wasm/writer.rz': ['wasm_browser_import_mask','wasm_browser_import_mask_high','wasm_browser_import_index','return wasm_browser_import_count_cached;'],
    'compiler/src/raz_codegen_wasm/src/wasm/codegen.rz': ['browser_type_count = 5','Raz Web single pointer/scalar primitive','Raz Web three-argument','Raz Web four-argument'],
    'compiler/src/raz_codegen_wasm/src/wasm/browser_host.rz': ['dom_set_attribute", base + 3','storage_value_write", base + 3','clipboard_write", base + 2','event_code_length", base + 1','timer_set_timeout", base','wasm_browser_emit_import_wrapper1','wasm_browser_emit_import_wrapper3','wasm_browser_emit_import_wrapper4','dom_attribute_write", base + 4'],
    'library/web/src/client_host.rz': ['storage_value_length','location_href_length','history_push','clipboard_write','event_code_length','timer_set_timeout'],
    'library/web/src/page_interactivity.rz': ['browser_export'],
}
checks=0
for rel, needles in required.items():
    text=(root/rel).read_text()
    for needle in needles:
        assert needle in text, f'{rel}: missing {needle}'
        checks += 1
web = web_codegen_source(root)
for needle in ['raz_web: razWeb','storage_value_length','history_push','clipboard_write','event_code_length','timer_set_timeout','dom_set_value']:
    assert needle in web, f'raz_codegen_web package: missing {needle}'
    checks += 1
print(f'PASS: browser platform ABI regression ({checks} checks)')
