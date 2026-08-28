#!/usr/bin/env python3
# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0

from pathlib import Path

bundle = Path('compiler/src/raz_driver/src/web_bundle.rz').read_text()
main = Path('compiler/src/raz_driver/src/compiler_main.rz').read_text()
required = [
    'raz_rt_hash_bytes',
    'web_bundle_fingerprint',
    'web_bundle_asset_name',
    'web_bundle_minify_css',
    'web_bundle_finalize_dist',
    'asset-manifest.json',
    'raz_compiler_rt_tree_list_ascii',
    'raz_compiler_rt_remove_path_ascii',
]
for token in required:
    if token not in bundle:
        raise SystemExit(f'FAIL: missing {token}')
sig = bundle[bundle.index('public fn web_bundle_fingerprint'):bundle.index('{', bundle.index('public fn web_bundle_fingerprint'))]
if 'path' in sig.lower() or 'time' in sig.lower():
    raise SystemExit('FAIL: fingerprint depends on path/time')
if 'raz_compiler_driver::web_bundle::web_bundle_finalize_dist' not in main:
    raise SystemExit('FAIL: release finalizer is not wired into compiler_main')
# Fingerprint order is a correctness contract: wasm URL affects JS bytes, and
# generated CSS is minified before its content hash is selected.
order = [
    bundle.index('// 1. Fingerprint WASM first into the one release asset directory.'),
    bundle.index('// 2. Rewrite each canonical JS module'),
    bundle.index('// 3. Minify generated CSS'),
    bundle.index('// 4. Rewrite HTML.'),
    bundle.index('// 5. Deterministic manifest'),
    bundle.index('// 6. Canonical names are build intermediates'),
]
if order != sorted(order):
    raise SystemExit('FAIL: bundle finalization order regressed')

if 'path_join(dist, dist_length, literal_name, assets_name_length, assets, 8192)' not in bundle:
    raise SystemExit('FAIL: release assets are not centralized under dist/assets')
if 'raz_compiler_rt_create_dir_ascii(assets, assets_length)' not in bundle:
    raise SystemExit('FAIL: dist/assets is not created by the release finalizer')
if 'manifest_value' not in bundle or '"assets"' not in bundle:
    raise SystemExit('FAIL: asset manifest does not record assets/ paths')

if 'if (!release_profile) { return true; }' not in bundle:
    raise SystemExit('FAIL: debug builds must retain canonical developer-friendly names')
print('PASS: production web bundle activation (17 checks)')
