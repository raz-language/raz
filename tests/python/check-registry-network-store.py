#!/usr/bin/env python3
# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0

from __future__ import annotations
import argparse, os, shutil, socket, subprocess, threading
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from compiler_test_driver import build_test_compiler

def run(args,cwd,env,expect=0):
    p=subprocess.run(args,cwd=cwd,env=env,text=True,stdout=subprocess.PIPE,stderr=subprocess.PIPE)
    if p.returncode!=expect:
        raise RuntimeError(f"failed {p.returncode}: {' '.join(map(str,args))}\n{p.stdout}\n{p.stderr}")
    return p

def tree_hash(root:Path)->str:
    h=1469598103934665603; mask=(1<<64)-1
    for p in sorted((p for p in root.rglob('*') if p.is_file()),key=lambda p:p.relative_to(root).as_posix()):
        for b in p.relative_to(root).as_posix().encode()+b'\0'+p.read_bytes()+b'\xff': h=((h^b)*1099511628211)&mask
    return f'{h:016x}'

def archive(root:Path)->bytes:
    out=[b'RAZPKG1\n']
    for p in sorted((p for p in root.rglob('*') if p.is_file()),key=lambda p:p.relative_to(root).as_posix()):
        out.append(b'F '+p.relative_to(root).as_posix().encode().hex().encode()+b' '+p.read_bytes().hex().encode()+b'\n')
    return b''.join(out)

def main():
    ap=argparse.ArgumentParser();ap.add_argument('--compiler');ap.add_argument('--work',required=True);ap.add_argument('--raz');ap.add_argument('--root');ap.add_argument('--linker');ns=ap.parse_args()
    work=Path(ns.work).resolve();shutil.rmtree(work,ignore_errors=True);work.mkdir(parents=True)
    env=os.environ.copy()
    if ns.compiler: compiler=str(Path(ns.compiler).resolve())
    else:
        root=Path(ns.root).resolve(); compiler=str(build_test_compiler(root, work, ns.raz, ns.linker, env))
    pkg=work/'widget';(pkg/'src').mkdir(parents=True)
    (pkg/'raz.toml').write_text('[package]\nname = "widget"\nversion = "1.2.0"\nkind = "library"\nsource = "src"\n\n[dependencies]\n')
    (pkg/'src/widget.rz').write_text('public fn answer() -> i64 { return 42; }\n')
    digest=tree_hash(pkg)
    web=work/'web';mirror=web/'mirror';(mirror/'packages').mkdir(parents=True)
    (mirror/'packages/widget-1.2.0.dpk').write_bytes(archive(pkg));(mirror/'index.txt').write_text(f'widget 1.2.0 packages/widget-1.2.0.dpk {digest}\n')
    app=work/'app';(app/'src').mkdir(parents=True);(app/'raz.toml').write_text('[package]\nname = "app"\nversion = "0.1.0"\nkind = "executable"\nsource = "src"\nentry = "src/main.rz"\n\n[dependencies]\n');(app/'src/main.rz').write_text('fn main() -> i64 { return 0; }\n')
    class Quiet(SimpleHTTPRequestHandler):
        def log_message(self,*args): pass
    server=ThreadingHTTPServer(('127.0.0.1',0),lambda *a,**k:Quiet(*a,directory=str(web),**k));port=server.server_address[1];thread=threading.Thread(target=server.serve_forever,daemon=True);thread.start()
    online=env.copy();online['RAZ_REGISTRY_URL']=f'http://127.0.0.1:{port}/missing';online['RAZ_REGISTRY_MIRRORS']=f'http://127.0.0.1:{port}/mirror';online['RAZ_HOME']=str(work/'home')
    run([compiler,'add','widget','registry:widget@^1.0.0'],app,online)
    store=work/'home/store'/digest
    assert (store/'raz.toml').is_file() and (store/'src/widget.rz').is_file()
    assert 'widget = "^1.0.0"' in (app/'raz.toml').read_text()
    assert (app/'.raz.registry-index').is_file()
    server.shutdown();server.server_close();thread.join(timeout=5)
    offline=online.copy();offline['RAZ_OFFLINE']='1';offline.pop('RAZ_REGISTRY_URL',None);offline.pop('RAZ_REGISTRY_MIRRORS',None)
    before=(app/'raz.lock').read_bytes(); assert b'source = "registry"' in before and digest.encode() in before; run([compiler,'fetch'],app,offline);assert (app/'raz.lock').read_bytes()==before
    (store/'src/widget.rz').write_text('corrupt\n');run([compiler,'update'],app,offline,expect=63)
    print('registry network/store: PASS')
    return 0
if __name__=='__main__': raise SystemExit(main())
