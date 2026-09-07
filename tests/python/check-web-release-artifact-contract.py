#!/usr/bin/env python3
# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0

from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
bundle = (ROOT / 'compiler/src/raz_driver/src/driver/web_bundle.rz').read_text(encoding='utf-8')
finalize = (ROOT / 'compiler/src/raz_driver/src/driver/web_bundle_finalize.rz').read_text(encoding='utf-8')
prune = (ROOT / 'compiler/src/raz_driver/src/driver/web_host_prune.rz').read_text(encoding='utf-8')
bootstrap = (ROOT / 'tools/bootstrap.py').read_text(encoding='utf-8')
main = (ROOT / 'compiler/src/raz_driver/src/driver/compiler_main.rz').read_text(encoding='utf-8')
manifest = (ROOT / 'compiler/src/raz_driver/src/driver/project_web_manifest.rz').read_text(encoding='utf-8')

checks = {
    'generated assets use exact basename matching': 'public fn web_bundle_line_has_basename' in bundle and finalize.count('web_bundle_line_has_basename') >= 8,
    'suffix collisions are boundary checked': 'return boundary == 47 || boundary == 92;' in bundle,
    'nested HTML computes a dist-root prefix': 'public fn web_bundle_html_root_prefix' in bundle and 'web_bundle_html_root_prefix(relative, line_length, replacement, 512)' in finalize,
    'HTML runtime rewrites are attribute-anchored': 'web_bundle_wrap_reference' in finalize and 'src=\\"./app.js\\"' in finalize and 'href=\\"./app.css\\"' in finalize,
    'HTML does not rewrite bare app.js substrings': 'web_bundle_rewrite_file(full, fl, "app.js"' not in finalize and 'web_bundle_rewrite_file(full, fl, "./app.js"' not in finalize,
    'HTML does not rewrite bare app.css substrings': 'web_bundle_rewrite_file(full, fl, "app.css"' not in finalize and 'web_bundle_rewrite_file(full, fl, "./app.css"' not in finalize,
    'main Wasm rewrites are loader-expression anchored': "new URL('./app.wasm', import.meta.url)" in finalize and "rzLoadWasm('/app.wasm')" in finalize and 'web_bundle_rewrite_file(full, fl, "/app.wasm"' not in finalize,
    'static main Wasm becomes sibling-relative in release': 'static generators use /app.wasm in debug' in finalize.lower() and '"rzLoadWasm(\'"' in finalize,
    'lazy Wasm chunk URL becomes sibling-relative in release': '"rzLoadWasm(\'/assets/chunks/"' in finalize and '"rzLoadWasm(\'./chunks/"' in finalize,
    'host pruning walks the dist tree': 'raz_compiler_rt_tree_list_ascii(dist_path, dist_path_length' in prune,
    'host pruning uses exact app.js basename': 'web_bundle_line_has_basename(files, line_start, line_length, "app.js")' in prune,
    'reactive builds recreate dist before emission': 'if (web_application) {' in main and 'web_build_error("could not prepare reactive dist output")' in main and 'project_web_copy_public' not in main and 'project_web_copy_public' not in manifest,
    'reactive classification is root-package scoped': 'compiler_web_function_is_root_package' in main and 'hir.incremental_module_fingerprint_count - 1' in main and 'compiler_web_function_is_root_package(hir, function_index)' in main,
    'reactive classification validates the web Component shape': 'compiler_web_structure_is_component' in main and all(token in main for token in ('"root"', '"styles"', '"scripts"', '"component_scope"', '"failed"', '"Element"', '"StyleSheet"', '"Buffer"')),
    'nested release runtime qualification is wired': 'check-web-release-artifact-contract-runtime.py' in bootstrap,
}

failed = [name for name, ok in checks.items() if not ok]
if failed:
    for name in failed:
        print(f'web-release-artifact-contract: FAIL: {name}')
    raise SystemExit(1)
print(f'web-release-artifact-contract: PASS ({len(checks)} release-path invariants)')
