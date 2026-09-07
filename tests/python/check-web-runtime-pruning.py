#!/usr/bin/env python3
# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0

"""Source gate for reachability-driven Raz Web reactive runtime pruning."""
from pathlib import Path
from web_source import web_codegen_source, web_bundle_source

ROOT = Path(__file__).resolve().parents[2]
codegen = web_codegen_source(ROOT)

capability_tokens = [
    'WEB_RUNTIME_EVENTS = 1',
    'WEB_RUNTIME_BINDINGS = 2',
    'WEB_RUNTIME_ROUTING = 4',
    'WEB_RUNTIME_HTTP = 8',
    'WEB_RUNTIME_SCOPED_COMPONENTS = 16',
    'WEB_RUNTIME_RAW_JS = 32',
    'WEB_RUNTIME_ALL = 63',
]
classifier_tokens = [
    '"append_event_attribute"',
    '"state_mark_bound"',
    '"text_derived_i64_bound"',
    '"route_pattern_matches"',
    '"route_matches"',
    '"route_match"',
    '"route_is"',
    '"route_prefix"',
    '"route_param"',
    '"route_splat"',
    '"child_nav_link"',
    '"navigate"',
    '"http_find_or_create"',
    '"resource_refresh"',
    '"component_scoped_with_scope"',
    '"raw_js"',
]

checks = {
    'six independent runtime capabilities': all(token in codegen for token in capability_tokens),
    'capabilities discovered from main call graph': (
        'fn web_runtime_main_function' in codegen
        and 'fn web_runtime_capabilities' in codegen
        and 'queue_head < queue_tail' in codegen
        and 'opcode == 8 || opcode == 43 || opcode == 50' in codegen
        and 'target = raz_compiler_rt_arena_get(mir.op_a, ip)' in codegen
    ),
    'allocation/no-main fallback is conservative': codegen.count('return WEB_RUNTIME_ALL;') >= 2,
    'feature classifier uses runtime convergence points': all(token in codegen for token in classifier_tokens),
    'loader receives analyzed capability mask': (
        'i64 runtime_capabilities = 63;' in codegen
        and 'runtime_capabilities = web_runtime_capabilities(source, hir, mir);' in codegen
        and 'browser_import_mask_high,\n            runtime_capabilities' in codegen
    ),
    'rerender patcher only retained for rerender sources': (
        'bool runtime_patch = runtime_events || runtime_routing || runtime_http;' in codegen
        and 'if (runtime_patch) {' in codegen
        and 'const patchChildren = ' in codegen
    ),
    'binding index independently gated': (
        'bool runtime_bindings = runtime_patch && web_runtime_has_bindings(runtime_capabilities);' in codegen
        and 'if (runtime_bindings) {' in codegen
        and 'let bindingIndex = new Map()' in codegen
    ),
    'scoped component synchronizer independently gated': (
        'bool runtime_scoped = runtime_patch && web_runtime_has_scoped_components(runtime_capabilities);' in codegen
        and 'if (runtime_scoped) {' in codegen
        and 'const renderOwnedScope = ' in codegen
    ),
    'routing helpers independently gated': (
        'if (runtime_routing) {' in codegen
        and 'const syncRouteChange = ' in codegen
        and "addEventListener('popstate'" in codegen
    ),
    'HTTP pump independently gated': 'if (runtime_http) {' in codegen and 'const pumpHttp = async () =>' in codegen,
    'event delegation independently gated': 'if (runtime_events) {' in codegen and 'const dispatchNode = ' in codegen,
    'empty browser host namespace is omitted': (
        'bool browser_host_any = import_mask != 0 || import_mask_high != 0;' in codegen
        and 'if (browser_host_any) {' in codegen
        and 'WebAssembly.instantiateStreaming(fetch(wasmUrl), {})' in codegen
        and 'if (browser_host_any) { web_write_browser_host(&mut out, import_mask, import_mask_high); }' in codegen
    ),
    'TextEncoder is capability-driven': (
        'bool runtime_encoder = browser_host_any || runtime_host_bytes;' in codegen
        and 'if (runtime_encoder) { codegen_writer_literal(&mut out, "const encoder = new TextEncoder();' in codegen
    ),
    'raw JavaScript bookkeeping independently gated': (
        'if (runtime_raw_js) { codegen_writer_literal(&mut out, "let renderedRawJs = null;' in codegen
        and "const rawJs = exportedText('raz_web_js')" in codegen
    ),
    'plain component uses direct initial publish': (
        "const syncView = () => { root.innerHTML = exportedText('raz_web_html')" in codegen
        and 'runtimeStyle.textContent = exportedText' in codegen
    ),
    'patch revision bookkeeping omitted from plain runtime': (
        'if (runtime_patch) { codegen_writer_literal(&mut out, "let renderedRevision = null;' in codegen
        and 'if (runtime_bindings) { codegen_writer_literal(&mut out, "let renderedStructureRevision = null;' in codegen
    ),
    'event/routing listener combinations stay independent': (
        'if (runtime_events && runtime_routing)' in codegen
        and '} else if (runtime_events) {' in codegen
        and '} else if (runtime_routing) {' in codegen
    ),
    'startup route and HTTP work are capability gated': (
        'if (runtime_routing) { codegen_writer_literal(&mut out, "setRoute(browserRoute());' in codegen
        and 'if (runtime_http) { codegen_writer_literal(&mut out, "await pumpHttp();' in codegen
    ),
    'public Raz route API only emitted with routing': (
        'if (runtime_routing) {' in codegen
        and 'route: (value) => { history.pushState' in codegen
    ),
}

failed = [name for name, ok in checks.items() if not ok]
if failed:
    for name in failed:
        print(f'web-runtime-pruning: FAIL: {name}')
    raise SystemExit(1)
print(f'web-runtime-pruning: PASS ({len(checks)} reachability/emission invariants)')
