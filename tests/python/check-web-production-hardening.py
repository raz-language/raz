#!/usr/bin/env python3
# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0

"""Source gate for Raz Web release hardening and bundle-budget enforcement."""
from pathlib import Path
from web_source import web_codegen_source, web_bundle_source

ROOT = Path(__file__).resolve().parents[2]
codegen = web_codegen_source(ROOT)
runtime = (ROOT / 'src' / 'runtime' / 'web_dev.cpp').read_text(encoding='utf-8')
bootstrap = (ROOT / 'tools' / 'bootstrap.py').read_text(encoding='utf-8')
docs = (ROOT / 'docs' / 'web.md').read_text(encoding='utf-8')
fixture = (ROOT / 'tests' / 'examples' / 'web' / 'production-hardening' / 'src' / 'main.rz').read_text(encoding='utf-8')
manifest = (ROOT / 'tests' / 'examples' / 'web' / 'production-hardening' / 'raz.toml').read_text(encoding='utf-8')

checks = {
    'empty browser host object is omitted': (
        'bool browser_host_any = import_mask != 0 || import_mask_high != 0;' in codegen
        and 'if (browser_host_any) {' in codegen
        and 'WebAssembly.instantiateStreaming(fetch(wasmUrl), {})' in codegen
    ),
    'TextEncoder is no longer unconditional': (
        'bool runtime_encoder = browser_host_any || runtime_host_bytes;' in codegen
        and 'if (runtime_encoder) { codegen_writer_literal(&mut out, "const encoder = new TextEncoder();' in codegen
    ),
    'manifest budget section is parsed by finalized-dist analyzer': (
        '[web.budget]' in runtime
        and 'read_web_bundle_budget' in runtime
        and all(token in runtime for token in ('"total"', '"html"', '"css"', '"javascript"', '"wasm"', '"metadata"', '"assets"'))
    ),
    'budget values reject overflow and malformed bytes': (
        'std::numeric_limits<std::uintmax_t>::max()' in runtime
        and 'invalid [web.budget] byte value for ' in runtime
    ),
    'analysis reports and fails exceeded budgets': (
        'out << "\\nBudgets:\\n";' in runtime
        and '"EXCEEDED"' in runtime
        and 'Web bundle budget exceeded.' in runtime
        and 'return within_budget ? 0 : -2;' in runtime
    ),
    'combined production fixture exercises runtime union': all(
        token in fixture for token in ('component_scoped', 'state_i64', 'effect_i64', 'effect_request', 'web::std::fetch::get', 'route_is', 'nav_link', 'button_increment')
    ),
    'combined fixture declares release budgets': '[web.budget]' in manifest and 'javascript =' in manifest and 'wasm =' in manifest,
    'bootstrap runs bundle analysis qualification': 'check-web-bundle-analysis.py' in bootstrap,
    'bootstrap runs combined production qualification': 'check-web-production-hardening-runtime.py' in bootstrap,
    'web docs describe enforceable budgets': '[web.budget]' in docs and 'EXCEEDED' in docs,
}
failed = [name for name, ok in checks.items() if not ok]
if failed:
    for name in failed:
        print(f'web-production-hardening: FAIL: {name}')
    raise SystemExit(1)
print(f'web-production-hardening: PASS ({len(checks)} release-hardening invariants)')
