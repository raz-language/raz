#!/usr/bin/env python3
# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0

from __future__ import annotations
import argparse, os, shutil, subprocess, threading
from http.server import ThreadingHTTPServer, BaseHTTPRequestHandler
from pathlib import Path
from compiler_test_driver import build_test_compiler

TOKEN='raz-test-token'
SIGNATURE='raz-test-signature'

def run(args,cwd,env,expect=0):
    p=subprocess.run(args,cwd=cwd,env=env,text=True,stdout=subprocess.PIPE,stderr=subprocess.PIPE)
    if p.returncode!=expect:
        raise RuntimeError(f"failed {p.returncode}: {' '.join(map(str,args))}\n{p.stdout}\n{p.stderr}")
    return p

class RegistryHandler(BaseHTTPRequestHandler):
    root: Path
    def log_message(self,*args): pass
    def _authorized(self):
        return self.headers.get('Authorization')==f'Bearer {TOKEN}' and self.headers.get('X-Raz-Signature')==SIGNATURE
    def do_PUT(self):
        if not self._authorized():
            self.send_response(401); self.end_headers(); return
        n=int(self.headers.get('Content-Length','0')); data=self.rfile.read(n)
        rel=self.path.lstrip('/'); dest=self.root/rel; dest.parent.mkdir(parents=True,exist_ok=True); dest.write_bytes(data)
        if rel.startswith('entries/'):
            lines=[]
            for p in sorted((self.root/'entries').glob('*/*')):
                text=p.read_text().strip()
                if text: lines.append(text)
            (self.root/'index.txt').write_text('\n'.join(lines)+'\n')
        self.send_response(201); self.end_headers()
    def do_GET(self):
        p=self.root/self.path.lstrip('/')
        if not p.is_file(): self.send_response(404); self.end_headers(); return
        data=p.read_bytes(); self.send_response(200); self.send_header('Content-Length',str(len(data))); self.end_headers(); self.wfile.write(data)

def main():
    ap=argparse.ArgumentParser();ap.add_argument('--compiler');ap.add_argument('--work',required=True);ap.add_argument('--raz');ap.add_argument('--root');ap.add_argument('--linker');ns=ap.parse_args()
    work=Path(ns.work).resolve();shutil.rmtree(work,ignore_errors=True);work.mkdir(parents=True)
    env=os.environ.copy()
    if ns.compiler: compiler=str(Path(ns.compiler).resolve())
    else:
        root=Path(ns.root).resolve(); compiler=str(build_test_compiler(root, work, ns.raz, ns.linker, env))

    invalid=work/'invalid'; (invalid/'src').mkdir(parents=True)
    (invalid/'raz.toml').write_text('[package]\nname = "invalid"\nversion = "1.0.0"\nkind = "library"\n\n[dependencies]\n')
    (invalid/'src/lib.rz').write_text('public fn value() -> i64 { return 1; }\n')
    invalid_env=env.copy()
    for key in ('RAZ_REGISTRY_URL','RAZ_REGISTRY_PUBLISH_DIR'):
        invalid_env.pop(key,None)
    rejected=subprocess.run([compiler,'publish'],cwd=invalid,env=invalid_env,stdout=subprocess.PIPE,stderr=subprocess.PIPE)
    assert rejected.returncode!=0, 'official registry accepted package without required metadata/docs'

    publisher=work/'publisher'; (publisher/'src').mkdir(parents=True)
    (publisher/'raz.toml').write_text('[package]\nname = "widget"\nversion = "1.4.0"\nkind = "library"\ndescription = "Registry publishing fixture."\nlicense = "Apache-2.0"\n\n[dependencies]\n')
    (publisher/'README.md').write_text('# Widget\n')
    (publisher/'LICENSE').write_text('Apache-2.0 test fixture\n')
    (publisher/'src/widget.rz').write_text('public fn widget_value() -> i64 { return 140; }\n')
    run([compiler,'pack','first.dpk'],publisher,env); run([compiler,'pack','second.dpk'],publisher,env)
    assert (publisher/'first.dpk').read_bytes()==(publisher/'second.dpk').read_bytes(), 'pack output is not deterministic'

    github_env=env.copy()
    for key in ('RAZ_REGISTRY_URL','RAZ_REGISTRY_PUBLISH_DIR','RAZ_REGISTRY_TOKEN','RAZ_REGISTRY_SIGNATURE'):
        github_env.pop(key,None)
    first=run([compiler,'publish'],publisher,github_env)
    assert first.stdout.startswith('Prepared widget@1.4.0 '), first.stdout
    staged=publisher/'.raz-publish/packages/widget/1.4.0.dpk'
    staged_index=publisher/'.raz-publish/index.txt'
    assert staged.is_file() and staged_index.is_file()
    first_bytes=staged.read_bytes(); first_index=staged_index.read_bytes()
    second=run([compiler,'publish'],publisher,github_env)
    assert second.stdout==first.stdout, (first.stdout,second.stdout)
    assert staged.read_bytes()==first_bytes, 'repeated GitHub submission changed package archive'
    assert staged_index.read_bytes()==first_index, 'repeated GitHub submission changed index metadata'

    registry=work/'registry'; registry.mkdir()
    RegistryHandler.root=registry
    server=ThreadingHTTPServer(('127.0.0.1',0),RegistryHandler); port=server.server_address[1]
    thread=threading.Thread(target=server.serve_forever,daemon=True); thread.start()
    base=f'http://127.0.0.1:{port}'
    unauth=env.copy(); unauth['RAZ_REGISTRY_URL']=base
    p=subprocess.run([compiler,'publish'],cwd=publisher,env=unauth,stdout=subprocess.PIPE,stderr=subprocess.PIPE)
    assert p.returncode!=0, 'private registry accepted unauthenticated publish'
    auth=unauth.copy(); auth['RAZ_REGISTRY_TOKEN']=TOKEN; auth['RAZ_REGISTRY_SIGNATURE']=SIGNATURE
    run([compiler,'publish'],publisher,auth); run([compiler,'publish'],publisher,auth)
    lines=(registry/'index.txt').read_text().strip().splitlines(); assert len(lines)==1 and lines[0].startswith('widget 1.4.0 packages/widget/1.4.0.dpk ')
    assert (registry/'packages/widget/1.4.0.dpk').is_file(); assert (registry/'entries/widget/1.4.0').is_file()

    consumer=work/'consumer'; (consumer/'src').mkdir(parents=True)
    (consumer/'raz.toml').write_text('[package]\nname = "consumer"\nversion = "0.1.0"\nkind = "executable"\n\n[dependencies]\n')
    (consumer/'src/main.rz').write_text('fn main() -> i64 { return 0; }\n')
    fetch=env.copy(); fetch['RAZ_REGISTRY_URL']=base; fetch['RAZ_HOME']=str(work/'home')
    run([compiler,'add','widget','registry:widget@^1.0.0'],consumer,fetch)
    assert 'widget = "' in (consumer/'raz.toml').read_text(); stores=list((work/'home/store').glob('*/raz.toml')); assert len(stores)==1

    server.shutdown();server.server_close();thread.join(timeout=5)
    offline=fetch.copy(); offline['RAZ_OFFLINE']='1'; offline.pop('RAZ_REGISTRY_URL',None)
    before=(consumer/'raz.lock').read_bytes(); run([compiler,'update'],consumer,offline); assert (consumer/'raz.lock').read_bytes()==before
    print('registry publishing: PASS')
    return 0
if __name__=='__main__': raise SystemExit(main())
