#!/usr/bin/env python3
# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0
import argparse, os, shutil, subprocess
from pathlib import Path

def read_u32(data, pos):
    value=0; shift=0
    while True:
        b=data[pos]; pos+=1; value |= (b & 0x7f) << shift
        if not (b & 0x80): return value,pos
        shift += 7

def wasm_exports(data):
    if data[:4] != b"\0asm": return set()
    pos=8
    while pos < len(data):
        section=data[pos]; pos+=1
        size,pos=read_u32(data,pos); end=pos+size
        if section==7:
            count,pos=read_u32(data,pos); names=set()
            for _ in range(count):
                n,pos=read_u32(data,pos); name=data[pos:pos+n].decode("utf-8"); pos+=n
                pos+=1  # export kind
                _,pos=read_u32(data,pos)
                names.add(name)
            return names
        pos=end
    return set()


def run(cmd,cwd,env): return subprocess.run(cmd,cwd=cwd,env=env,text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
def main():
    ap=argparse.ArgumentParser(); ap.add_argument("--raz",required=True); ap.add_argument("--work-root",required=True); a=ap.parse_args()
    root=Path(__file__).resolve().parents[2]; work=Path(a.work_root); shutil.rmtree(work,ignore_errors=True); shutil.copytree(root/"tests/examples/web/wasm-chunks",work)
    env=os.environ.copy(); env["RAZ_HOME"]=str(root); r=run([a.raz,"build","--release","--forge-native","--forge-structured-only"],work,env)
    if r.returncode: print(r.stdout); return 1
    dist=work/"dist"; assets=dist/"assets"; chunks=assets/"chunks"
    if list(dist.glob("app.wasm")) or list(assets.glob("app.*.wasm")): print("web-wasm-chunks: lazy-only page unexpectedly emitted main app.wasm"); return 1
    editor=list(chunks.glob("editor.*.wasm")); analytics=list(chunks.glob("analytics.*.wasm")); js=list(assets.glob("app.*.js"))
    if len(editor)!=1 or len(analytics)!=1 or len(js)!=1: print("web-wasm-chunks: expected one fingerprinted JS host and two independent WASM chunks"); return 1
    if (chunks/"editor.wasm").exists() or (chunks/"analytics.wasm").exists(): print("web-wasm-chunks: canonical chunk intermediate was not removed"); return 1
    editor_bytes=editor[0].read_bytes(); analytics_bytes=analytics[0].read_bytes(); text=js[0].read_text()
    if editor_bytes[:4]!=b"\0asm" or analytics_bytes[:4]!=b"\0asm": print("web-wasm-chunks: invalid WASM chunk"); return 1
    editor_exports=wasm_exports(editor_bytes); analytics_exports=wasm_exports(analytics_bytes)
    if not {"open_editor","close_editor"}.issubset(editor_exports) or "run_report" in editor_exports: print("web-wasm-chunks: editor export graph was not isolated", sorted(editor_exports)); return 1
    if "run_report" not in analytics_exports or "open_editor" in analytics_exports or "close_editor" in analytics_exports: print("web-wasm-chunks: analytics export graph was not isolated", sorted(analytics_exports)); return 1
    if f"/assets/chunks/{editor[0].name}" not in text or f"/assets/chunks/{analytics[0].name}" not in text: print("web-wasm-chunks: loaders were not rewritten to fingerprinted chunks"); return 1
    manifest=(dist/"asset-manifest.json").read_text()
    if "assets/chunks/editor.wasm" not in manifest or "assets/chunks/analytics.wasm" not in manifest: print("web-wasm-chunks: chunk mappings missing from manifest"); return 1
    print(f"web-wasm-chunks: PASS ({editor[0].stat().st_size}B editor + {analytics[0].stat().st_size}B analytics, no main app.wasm)"); return 0
if __name__=="__main__": raise SystemExit(main())
