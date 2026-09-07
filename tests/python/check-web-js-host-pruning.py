#!/usr/bin/env python3
# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0

"""Source gate for usage-driven Raz Web JavaScript host generation."""
from __future__ import annotations

import re
from pathlib import Path
from web_source import web_codegen_source, web_bundle_source

ROOT = Path(__file__).resolve().parents[2]
WASM = (ROOT / 'compiler/src/raz_codegen_wasm/src/wasm/codegen.rz').read_text(encoding='utf-8')
BROWSER = (ROOT / 'compiler/src/raz_codegen_wasm/src/wasm/browser_host.rz').read_text(encoding='utf-8')
WEB = web_codegen_source(ROOT)
STATIC = (ROOT / 'library/web/src/client_host.rz').read_text(encoding='utf-8')
DRIVER = (ROOT / 'compiler/src/raz_driver/src/driver/compiler_main.rz').read_text(encoding='utf-8')
BUNDLE = web_bundle_source(ROOT)

abi = {
    int(number): name
    for number, name in re.findall(
        r'if \(wasm_browser_import_enabled\((\d+)\)\) \{\s*'
        r'wasm_browser_emit_import_literal\(section, "([^"]+)"',
        BROWSER,
    )
}
reactive = {
    int(number)
    for number in re.findall(
        r'web_write_browser_host_binding\((?:&mut )?out, import_mask, import_mask_high, (\d+),',
        WEB,
    )
}
static_markers = {
    int(number)
    for number in re.findall(r'/\*raz-web-import:(\d+)\*/', STATIC)
}

checks = {
    'browser ABI has 77 logical imports': len(abi) == 77 and set(abi) == set(range(24, 101)),
    'Wasm emitter can return exact import masks': all(token in WASM for token in [
        'emit_web_wasm_module_imports(',
        'emit_web_wasm_module_roots_imports(',
        '*out_import_mask = wasm_browser_import_mask;',
        '*out_import_mask_high = wasm_browser_import_mask_high;',
    ]),
    'reactive loader gates every host binding': reactive == set(abi),
    'static host marks every host binding independently': static_markers == set(abi),
    'static markers are one-per-binding': len(re.findall(r'/\*raz-web-import:\d+\*/', STATIC)) == 77,
    'multi-chunk driver unions host requirements': all(token in DRIVER for token in [
        'chunk_import_mask',
        '*import_mask = *import_mask | chunk_import_mask;',
        '*import_mask_high = *import_mask_high | chunk_import_mask_high;',
        '&mut web_browser_import_mask, &mut web_browser_import_mask_high',
    ]),
    'static app host pruned before bundling': DRIVER.find('web_bundle_prune_browser_host(') < DRIVER.find('web_bundle_finalize_dist('),
    'pruner strips private host markers': all(token in BUNDLE for token in [
        'public fn web_bundle_prune_browser_host(',
        'string marker = "/*raz-web-import:";',
        'copy_start = p + 2;',
        'web_browser_import_enabled(import_mask, import_mask_high, logical_import)',
    ]),
    'static pruner reuses canonical browser bitmap helper': (
        'import web::browser_host_js;' in BUNDLE
        and 'web_browser_import_enabled(import_mask, import_mask_high, logical_import)' in BUNDLE
        and 'fn web_bundle_browser_import_enabled' not in BUNDLE
    ),
    'import bitmap layout matches Wasm 48+29 split': all(token in WEB for token in [
        'if (bit < 48)',
        'bit -= 48;',
    ]),
}

failed = [name for name, ok in checks.items() if not ok]
for name, ok in checks.items():
    print(('PASS' if ok else 'FAIL') + ': ' + name)
if failed:
    raise SystemExit(1)
print(f'web-js-host-pruning: PASS ({len(checks)} invariants; 77/77 host bindings usage-driven)')
