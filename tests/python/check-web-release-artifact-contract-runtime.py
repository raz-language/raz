#!/usr/bin/env python3
# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0

from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import re
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[2]


def run(cmd: list[str], cwd: Path, env: dict[str, str]) -> subprocess.CompletedProcess[str]:
    return subprocess.run(cmd, cwd=cwd, env=env, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument('--raz', required=True)
    ap.add_argument('--work-root', required=True)
    args = ap.parse_args()

    raz = Path(args.raz).resolve()
    work = Path(args.work_root).resolve()
    if work.exists():
        shutil.rmtree(work)
    project = work / 'release-artifact-contract'
    (project / 'src').mkdir(parents=True)
    (project / 'public' / 'assets').mkdir(parents=True)

    (project / 'raz.toml').write_text('''# Copyright 2026 Mario Vinciguerra\n# SPDX-License-Identifier: Apache-2.0\n\n[package]\nname = "raz-web-release-artifact-contract"\nversion = "0.1.0"\nkind = "executable"\nsource = "src"\nentry = "src/main.rz"\n\n[build]\ntarget = "web"\n\n[dependencies]\nweb = "raz:web"\n\n[web]\ntitle = "Release artifact contract"\n''', encoding='utf-8')

    (project / 'src' / 'main.rz').write_text('''// Copyright 2026 Mario Vinciguerra\n// SPDX-License-Identifier: Apache-2.0\n\nimport web;\n\nglobal mut i64 count = 0;\n\nfn increment() {\n    count += 1;\n    web::set_text_i64("count", count);\n}\n\nfn lazy_handler() {\n    web::set_text_i64("count", 99);\n}\n\nfn main() -> i64 {\n    if (!web::prepare_dist()) { return 1; }\n\n    Page home = Page::new("Home");\n    home.p("Static home");\n    if (!home.write_route("/")) { return 2; }\n\n    Page nested = Page::new("Nested interactive route");\n    nested.css_class("panel", "padding: 1rem;");\n    nested.p_id("count", "0");\n    nested.button_id("inc", "Increment");\n    nested.button_id("lazy", "Lazy");\n    nested.raw("<p>https://cdn.example/app.js https://cdn.example/assets/app.js Keep /app.css and /assets/app.css literal.</p>");\n    if (!nested.on_click("inc", "increment")) { return 3; }\n    if (!nested.lazy_wasm_on_click("lazy", "editor", "lazy_handler")) { return 4; }\n    if (!nested.write_route("/docs/install")) { return 5; }\n    return 0;\n}\n''', encoding='utf-8')

    # These names intentionally end in app.js/app.css but are not the reserved
    # generated basename. Release finalization must treat them as public assets.
    (project / 'public' / 'assets' / 'myapp.js').write_text("globalThis.__keepMyApp = 'myapp.js';\n", encoding='utf-8')
    (project / 'public' / 'assets' / 'myapp.css').write_text(".myapp { content: 'myapp.css'; }\n", encoding='utf-8')

    env = os.environ.copy()
    env['RAZ_HOME'] = str(ROOT)
    runtime = ROOT / 'build/release/src/runtime/libraz_runtime.a'
    if runtime.is_file():
        env['RAZ_RUNTIME_LIBRARY'] = str(runtime)

    built = run([str(raz), 'build', '--release', '--forge-native', '--forge-structured-only'], project, env)
    if built.returncode != 0:
        print(built.stdout)
        return 1

    dist = project / 'dist'
    nested_html_path = dist / 'docs' / 'install' / 'index.html'
    if not nested_html_path.is_file():
        print('web-release-artifact-contract: nested route missing')
        return 1

    js_files = sorted((dist / 'assets').glob('app.*.js'))
    css_files = sorted((dist / 'assets').glob('app.*.css'))
    wasm_files = sorted((dist / 'assets').glob('app.*.wasm'))
    chunk_files = sorted((dist / 'assets' / 'chunks').glob('editor.*.wasm'))
    if len(js_files) != 1 or len(css_files) != 1 or len(wasm_files) != 1 or len(chunk_files) != 1:
        print('web-release-artifact-contract: expected one fingerprinted nested JS/CSS/main-Wasm/chunk')
        print(' js=', [p.name for p in js_files])
        print(' css=', [p.name for p in css_files])
        print(' wasm=', [p.name for p in wasm_files])
        print(' chunks=', [p.name for p in chunk_files])
        return 1

    html = nested_html_path.read_text(encoding='utf-8')
    expected_js = f'../../assets/{js_files[0].name}'
    expected_css = f'../../assets/{css_files[0].name}'
    if expected_js not in html or expected_css not in html:
        print('web-release-artifact-contract: nested HTML does not use dist-root-relative fingerprinted assets')
        print(html)
        return 1
    if 'src="./assets/app.' in html or 'href="./assets/app.' in html or 'src="/assets/app.' in html or 'href="/assets/app.' in html:
        print('web-release-artifact-contract: nested HTML retained a route-wrong/origin-root generated asset URL')
        return 1
    if 'https://cdn.example/app.js' not in html or 'https://cdn.example/assets/app.js' not in html or 'Keep /app.css and /assets/app.css literal.' not in html:
        print('web-release-artifact-contract: generated asset rewrites corrupted user-authored HTML content')
        print(html)
        return 1

    js = js_files[0].read_text(encoding='utf-8')
    if f'./{wasm_files[0].name}' not in js or f"rzLoadWasm('./chunks/{chunk_files[0].name}')" not in js:
        print('web-release-artifact-contract: fingerprinted JS does not use sibling-relative main/chunk Wasm URLs')
        print(js)
        return 1
    if '/app.wasm' in js or "rzLoadWasm('/assets/chunks/" in js or '/*raz-web-import:' in js:
        print('web-release-artifact-contract: private/origin-root browser glue leaked into release JS')
        return 1

    public_js = sorted((dist / 'assets').glob('myapp.*.js'))
    public_css = sorted((dist / 'assets').glob('myapp.*.css'))
    if len(public_js) != 1 or len(public_css) != 1:
        print('web-release-artifact-contract: suffix-collision public assets were not fingerprinted normally')
        return 1
    if (dist / 'assets' / 'myapp.js').exists() or (dist / 'assets' / 'myapp.css').exists():
        print('web-release-artifact-contract: canonical public assets were not removed')
        return 1
    if "myapp.js" not in public_js[0].read_text(encoding='utf-8'):
        print('web-release-artifact-contract: public myapp.js content was corrupted')
        return 1

    manifest_path = dist / 'asset-manifest.json'
    if not manifest_path.is_file():
        print('web-release-artifact-contract: asset manifest missing')
        return 1
    manifest = json.loads(manifest_path.read_text(encoding='utf-8'))
    if 'assets/myapp.js' not in manifest or 'assets/myapp.css' not in manifest:
        print('web-release-artifact-contract: suffix-collision public assets missing from manifest')
        return 1

    # Reactive builds must recreate dist as well. Build once with a public file,
    # remove it, inject a fake old fingerprinted asset, and rebuild. Neither may
    # survive the second build.
    reactive = work / 'reactive-clean-dist'
    shutil.copytree(ROOT / 'tests/examples/web/direct-reactivity', reactive)
    reactive_manifest = reactive / 'raz.toml'
    reactive_manifest.write_text(
        reactive_manifest.read_text(encoding='utf-8').replace(
            'title = "Raz Direct Reactivity"',
            'title = "Raz Direct Reactivity"\njavascript = "extra.js"',
        ),
        encoding='utf-8',
    )
    (reactive / 'extra.js').write_text("globalThis.__literalWasm = '/app.wasm';\n", encoding='utf-8')
    (reactive / 'public').mkdir(exist_ok=True)
    (reactive / 'public' / 'old.txt').write_text('old\n', encoding='utf-8')
    first = run([str(raz), 'build', '--release', '--forge-native', '--forge-structured-only'], reactive, env)
    if first.returncode != 0:
        print(first.stdout)
        return 1
    if not (reactive / 'dist' / 'old.txt').is_file():
        print('web-release-artifact-contract: first reactive build did not copy public asset')
        return 1
    (reactive / 'public' / 'old.txt').unlink()
    stale = reactive / 'dist' / 'assets' / 'app.stale.js'
    stale.write_text('stale\n', encoding='utf-8')
    second = run([str(raz), 'build', '--release', '--forge-native', '--forge-structured-only'], reactive, env)
    if second.returncode != 0:
        print(second.stdout)
        return 1
    if (reactive / 'dist' / 'old.txt').exists() or stale.exists():
        print('web-release-artifact-contract: reactive dist retained stale files across rebuild')
        return 1
    reactive_js_files = list((reactive / 'dist' / 'assets').glob('app.*.js'))
    if len(reactive_js_files) != 1:
        print('web-release-artifact-contract: reactive rebuild retained multiple fingerprinted JS assets')
        return 1
    reactive_js = reactive_js_files[0].read_text(encoding='utf-8')
    if "globalThis.__literalWasm = '/app.wasm';" not in reactive_js:
        print('web-release-artifact-contract: release Wasm rewrite corrupted user-authored JavaScript')
        return 1

    print('web-release-artifact-contract: PASS (nested release paths + syntax-anchored rewrites + subpath-safe Wasm + suffix-safe public assets + clean reactive dist)')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
