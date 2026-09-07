#!/usr/bin/env python3
# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0

"""Structural closure gate for the production Raz Web compiler pipeline."""
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[2]
WEB = ROOT / 'compiler' / 'src' / 'raz_codegen_web' / 'src' / 'web'
DRIVER = ROOT / 'compiler' / 'src' / 'raz_driver' / 'src' / 'driver'

web_modules = {
    name: (WEB / name).read_text(encoding='utf-8')
    for name in ('support.rz', 'runtime_analysis.rz', 'browser_host_js.rz', 'runtime_js.rz', 'codegen.rz')
}
driver_modules = {
    name: (DRIVER / name).read_text(encoding='utf-8')
    for name in ('web_bundle.rz', 'web_bundle_finalize.rz', 'web_host_prune.rz', 'web_diagnostics.rz')
}
lib = (ROOT / 'compiler/src/raz_codegen_web/src/lib.rz').read_text(encoding='utf-8')
driver_lib = (DRIVER.parent / 'lib.rz').read_text(encoding='utf-8')
main = (DRIVER / 'compiler_main.rz').read_text(encoding='utf-8')

checks = {
    'reactive compiler is responsibility-split': all(
        f'public import raz_codegen_web::{name.removesuffix(".rz")};' in lib
        for name in ('support.rz', 'runtime_analysis.rz', 'browser_host_js.rz', 'runtime_js.rz', 'codegen.rz')
    ),
    'reactive compiler modules stay below 32 KiB': all(len(text.encode('utf-8')) < 32768 for text in web_modules.values()),
    'codegen module is orchestration-only': (
        len(re.findall(r'(?m)^(?:public\s+)?fn\s+', web_modules['codegen.rz'])) == 1
        and 'public fn emit_web_application(' in web_modules['codegen.rz']
        and 'web_write_loader(' in web_modules['codegen.rz']
        and 'web_runtime_capabilities(' in web_modules['codegen.rz']
    ),
    'browser host owns all 77 host bindings': len(re.findall(r'web_write_browser_host_binding\(out, import_mask, import_mask_high, \d+,', web_modules['browser_host_js.rz'])) == 77,
    'runtime analysis is separate from JS emission': (
        'public fn web_runtime_capabilities(' in web_modules['runtime_analysis.rz']
        and 'const patchChildren = ' not in web_modules['runtime_analysis.rz']
        and 'WEB_RUNTIME_ALL = 63' in web_modules['runtime_analysis.rz']
    ),
    'runtime JS delegates browser host emission': (
        'web_write_browser_host(&mut out, import_mask, import_mask_high)' in web_modules['runtime_js.rz']
        and 'web_write_browser_host_binding(' not in web_modules['runtime_js.rz']
    ),
    'bundle finalization is separated': (
        'public import raz_driver::web_bundle_finalize;' in driver_lib
        and 'public fn web_bundle_finalize_dist(' in driver_modules['web_bundle_finalize.rz']
        and 'public fn web_bundle_finalize_dist(' not in driver_modules['web_bundle.rz']
    ),
    'bundle modules stay below 32 KiB': all(len(text.encode('utf-8')) < 32768 for text in driver_modules.values()),
    'static host pruning is separated': (
        'public import raz_driver::web_host_prune;' in driver_lib
        and 'public fn web_bundle_prune_browser_host(' in driver_modules['web_host_prune.rz']
        and 'public fn web_bundle_prune_browser_host(' not in driver_modules['web_bundle.rz']
    ),
    'static pruner shares canonical browser bitmap logic': (
        'import web::browser_host_js;' in driver_modules['web_host_prune.rz']
        and 'web_browser_import_enabled(import_mask, import_mask_high, logical_import)' in driver_modules['web_host_prune.rz']
        and 'fn web_bundle_browser_import_enabled' not in driver_modules['web_host_prune.rz']
    ),
    'web failures use one stage diagnostic helper': (
        'public fn web_build_error(string detail)' in driver_modules['web_diagnostics.rz']
        and main.count('web_build_error(') >= 10
        and all(stage in main for stage in (
            'could not prepare dist output',
            'browser-Wasm emission failed',
            'lazy browser-Wasm chunk emission failed',
            'browser host pruning failed',
            'dist finalization failed',
            'bundle analysis or budget validation failed',
            'reactive application emission failed',
        ))
    ),
    'driver imports explicit web pipeline stages': all(
        token in main for token in (
            'import raz_driver::web_bundle_finalize;',
            'import raz_driver::web_host_prune;',
            'import raz_driver::web_diagnostics;',
        )
    ),
}

failed = [name for name, ok in checks.items() if not ok]
for name, ok in checks.items():
    print(('PASS' if ok else 'FAIL') + ': ' + name)
if failed:
    raise SystemExit(1)
print(f'web-backend-closure: PASS ({len(checks)} structural/diagnostic invariants)')
