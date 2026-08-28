#!/usr/bin/env python3
# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0

from __future__ import annotations
import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[2]


def run(cmd: list[str], cwd: Path, env: dict[str, str]) -> subprocess.CompletedProcess[str]:
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
    work.parent.mkdir(parents=True, exist_ok=True)

    env = os.environ.copy()
    env["RAZ_HOME"] = str(ROOT)
    runtime = ROOT / "build/release/src/runtime/libraz_runtime.a"
    if runtime.is_file():
        env["RAZ_RUNTIME_LIBRARY"] = str(runtime)

    created = run([str(raz), "new", str(work), "--web"], work.parent, env)
    if created.returncode != 0:
        print(created.stdout)
        return 1

    public = work / "public"
    (public / "assets/images").mkdir(parents=True)
    (public / "assets/fonts").mkdir(parents=True)
    (public / "assets/css").mkdir(parents=True)
    (public / "assets/images/hero.png").write_bytes(b"raz-web-hero-v1")
    (public / "assets/fonts/site.woff2").write_bytes(b"raz-web-font-v1")
    (public / "assets/css/theme.css").write_text(
        ".hero { background: url('/assets/images/hero.png'); }\n"
        "@font-face { font-family: site; src: url('/assets/fonts/site.woff2') format('woff2'); }\n",
        encoding="utf-8",
    )
    source = work / "src/main.rz"
    text = source.read_text(encoding="utf-8")
    text = text.replace(
        'page.stylesheet("/styles.css");',
        'page.stylesheet("/styles.css");\n    page.stylesheet("/assets/css/theme.css");',
    )
    text = text.replace(
        'content.h1("Hello from Raz");',
        'content.h1("Hello from Raz");\n    page.image_lazy("/assets/images/hero.png", "Hero");',
    )
    source.write_text(text, encoding="utf-8")

    # Development builds are intentionally transparent: public assets retain
    # their logical names so editing/debugging never requires manifest lookup.
    debug = run([str(raz), "build", "--forge-native", "--forge-structured-only"], work, env)
    if debug.returncode != 0:
        print(debug.stdout)
        return 1
    if not (work / "dist/assets/images/hero.png").is_file() or not (work / "dist/assets/css/theme.css").is_file():
        print("web-assets: debug build unexpectedly fingerprinted public/assets")
        return 1
    debug_html = (work / "dist/index.html").read_text(encoding="utf-8")
    if '/assets/images/hero.png' not in debug_html or '/assets/css/theme.css' not in debug_html:
        print("web-assets: debug build rewrote logical asset URLs")
        return 1

    release = run([str(raz), "build", "--release", "--forge-native", "--forge-structured-only"], work, env)
    if release.returncode != 0:
        print(release.stdout)
        return 1

    manifest_path = work / "dist/asset-manifest.json"
    if not manifest_path.is_file():
        print("web-assets: release build did not emit asset-manifest.json")
        return 1
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    logical = {
        "assets/images/hero.png",
        "assets/fonts/site.woff2",
        "assets/css/theme.css",
    }
    if not logical.issubset(manifest):
        print("web-assets: manifest is missing public content-addressed assets")
        return 1
    for name in logical:
        shipped = work / "dist" / manifest[name]
        if not shipped.is_file() or shipped.name == Path(name).name:
            print(f"web-assets: missing fingerprinted release asset for {name}")
            return 1
        if (work / "dist" / name).exists():
            print(f"web-assets: canonical release asset still shipped: {name}")
            return 1

    html = (work / "dist/index.html").read_text(encoding="utf-8")
    if manifest["assets/images/hero.png"] not in html or manifest["assets/css/theme.css"] not in html:
        print("web-assets: HTML references were not rewritten to fingerprinted assets")
        return 1
    theme = (work / "dist" / manifest["assets/css/theme.css"]).read_text(encoding="utf-8")
    if manifest["assets/images/hero.png"] not in theme or manifest["assets/fonts/site.woff2"] not in theme:
        print("web-assets: CSS url() references were not rewritten before CSS fingerprinting")
        return 1
    if " { " in theme or " }" in theme:
        print("web-assets: release CSS asset was not compacted")
        return 1

    # A byte change must change the public URL while leaving unrelated assets
    # stable. This is the cache-busting contract users depend on for CDNs.
    before_image = manifest["assets/images/hero.png"]
    before_font = manifest["assets/fonts/site.woff2"]
    (public / "assets/images/hero.png").write_bytes(b"raz-web-hero-v2")
    rebuilt = run([str(raz), "build", "--release", "--forge-native", "--forge-structured-only"], work, env)
    if rebuilt.returncode != 0:
        print(rebuilt.stdout)
        return 1
    manifest2 = json.loads(manifest_path.read_text(encoding="utf-8"))
    if manifest2["assets/images/hero.png"] == before_image:
        print("web-assets: changed image retained stale content fingerprint")
        return 1
    if manifest2["assets/fonts/site.woff2"] != before_font:
        print("web-assets: unrelated font fingerprint changed")
        return 1

    print("web-assets: PASS (debug-stable URLs; release fingerprints + HTML/CSS rewrites + manifest + cache busting)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
