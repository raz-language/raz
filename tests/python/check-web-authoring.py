#!/usr/bin/env python3
# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0

from __future__ import annotations
import argparse
import os
from pathlib import Path
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
FIXTURE = ROOT / "tests/examples/web/authoring"


def run(cmd, cwd, env):
    return subprocess.run(cmd, cwd=cwd, env=env, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--raz", required=True)
    ap.add_argument("--work-root", required=True)
    args = ap.parse_args()

    raz = Path(args.raz).resolve()
    work = Path(args.work_root).resolve()
    if work.exists():
        shutil.rmtree(work)
    shutil.copytree(FIXTURE, work)

    env = os.environ.copy()
    env["RAZ_HOME"] = str(ROOT)
    runtime = ROOT / "build/release/src/runtime/libraz_runtime.a"
    if runtime.is_file():
        env["RAZ_RUNTIME_LIBRARY"] = str(runtime)

    result = run([str(raz), "build", "--release", "--forge-native", "--forge-structured-only"], work, env)
    if result.returncode != 0:
        print(result.stdout)
        return 1

    index = work / "dist/index.html"
    if not index.is_file():
        print("web-authoring: missing dist/index.html")
        return 1
    html = index.read_text(encoding="utf-8")
    required = [
        '<html lang="en-US">',
        '<meta name="author" content="Raz">',
        '<meta name="theme-color" content="#111111">',
        '<link rel="canonical" href="https://example.test/docs">',
        '<meta property="og:title" content="Raz Web Authoring">',
        '<link rel="alternate" href="/es/docs" hreflang="es">',
        '<link rel="preload" href="/fonts/raz.woff2" as="font">',
        '<link rel="manifest" href="/site.webmanifest">',
        '<article class="article">',
        '<h4>Semantic headings work</h4>',
        'Raz &lt;Web&gt; &amp; HTML',
        'Text is escaped by default: &lt;&gt;&amp;&quot;',
        '<p data-kind="example">Safe custom attributes</p>',
        '<p data-kind="multi" aria-label="Two attributes">Multiple safe attributes</p>',
        '<span data-a="1" data-b="2" data-c="3">Three attributes</span>',
        '<time datetime="2026-08-26">August 26, 2026</time>',
        '<abbr title="WebAssembly">Wasm</abbr>',
        '<aside id="toc" class="toc" role="complementary" aria-label="On this page">',
        '<dl class="terms">',
        '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" class="icon">',
        '<path d="M4 12h16"></path>',
        '<img src="/hero.png" alt="Raz web output" loading="lazy" decoding="async">',
        '<iframe src="/embed.html" title="Embedded Raz example"></iframe>',
        '<section data-raz-scope="docs">',
        '<details class="details">',
        '<textarea name="message" placeholder="Message">Hello</textarea>',
        '<table class="data">',
        '<tfoot>',
        'target="_blank" rel="noopener noreferrer"',
    ]
    missing = [item for item in required if item not in html]
    if missing:
        print("web-authoring: missing generated markup:")
        for item in missing:
            print("  ", item)
        return 1
    css_candidates = sorted((work / "dist" / "assets").glob("app.*.css"))
    if len(css_candidates) != 1:
        print("web-authoring: missing fingerprinted extracted CSS")
        return 1
    css_path = css_candidates[0]
    css = css_path.read_text(encoding="utf-8")
    css_required = [
        ":root{color-scheme:light dark;}",
        ".article{max-width:70ch;margin:0 auto;}",
        "#email{min-width:18rem;}",
        '[data-raz-scope="docs"] p{line-height:1.6;}',
        "@media (max-width:640px){.article{padding:1rem;}}",
    ]
    missing_css = [item for item in css_required if item not in css]
    if missing_css:
        print("web-authoring: missing extracted CSS:")
        for item in missing_css:
            print("  ", item)
        return 1
    if css.count(".article{max-width:70ch;margin:0 auto;}") != 1:
        print("web-authoring: duplicate structured CSS rule was not deduplicated")
        return 1
    if f'<link rel="stylesheet" href="./assets/{css_path.name}">' not in html:
        print("web-authoring: fingerprinted CSS link missing from generated HTML")
        return 1
    if list((work / "dist").glob("app*.js")) or list((work / "dist").glob("app*.wasm")):
        print("web-authoring: styled static authoring unexpectedly emitted browser runtime")
        return 1
    print("web-authoring: PASS (complete semantic HTML + metadata + accessibility + SVG remain runtime-free)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
