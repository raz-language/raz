#!/usr/bin/env python3
# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0

from __future__ import annotations
import argparse, os, shutil, subprocess, threading, time
from http.server import ThreadingHTTPServer, SimpleHTTPRequestHandler
from pathlib import Path
from compiler_test_driver import build_test_compiler


def run(args, cwd, env, expect=0):
    p=subprocess.run(args,cwd=cwd,env=env,text=True,stdout=subprocess.PIPE,stderr=subprocess.PIPE)
    if p.returncode!=expect:
        raise RuntimeError(f"failed {p.returncode}: {' '.join(map(str,args))}\n{p.stdout}\n{p.stderr}")
    return p

class SlowRegistry(SimpleHTTPRequestHandler):
    root: Path
    lock=threading.Lock(); inflight=0; max_inflight=0
    def log_message(self,*args): pass
    def translate_path(self,path): return str(self.root/path.lstrip('/'))
    def do_GET(self):
        with self.lock:
            type(self).inflight += 1
            type(self).max_inflight=max(type(self).max_inflight,type(self).inflight)
        try:
            time.sleep(0.35)
            super().do_GET()
        finally:
            with self.lock: type(self).inflight -= 1

def make_package(root:Path,name:str,version='1.0.0'):
    (root/'src').mkdir(parents=True)
    (root/'raz.toml').write_text(f'[package]\nname = "{name}"\nversion = "{version}"\nkind = "library"\nsource = "src"\nentry = "src/main.rz"\n\n[dependencies]\n')
    (root/'src/main.rz').write_text(f'public fn {name}_value() -> i64 {{ return 1; }}\n')

def make_consumer(root:Path):
    (root/'src').mkdir(parents=True)
    (root/'raz.toml').write_text('[package]\nname = "consumer"\nversion = "1.0.0"\nkind = "executable"\nsource = "src"\nentry = "src/main.rz"\n\n[dependencies]\n')
    (root/'src/main.rz').write_text('fn main() -> i64 { return 0; }\n')

def main():
    ap=argparse.ArgumentParser();ap.add_argument('--compiler');ap.add_argument('--work',required=True);ap.add_argument('--raz');ap.add_argument('--root');ap.add_argument('--linker');ns=ap.parse_args()
    work=Path(ns.work).resolve();shutil.rmtree(work,ignore_errors=True);work.mkdir(parents=True);env=os.environ.copy()
    if ns.compiler: compiler=str(Path(ns.compiler).resolve())
    else:
        root=Path(ns.root).resolve(); compiler=str(build_test_compiler(root, work, ns.raz, ns.linker, env))

    # Real Ed25519 signing/trust policy.
    keys=work/'keys'; keys.mkdir(); run([compiler,'keygen','registry'],keys,env)
    signed=work/'signed'; make_package(signed,'widget','1.2.3'); registry=work/'signed-registry'; registry.mkdir(); home=work/'signed-home'
    pubenv=env.copy(); pubenv['RAZ_SIGNING_KEY']=str(keys/'registry.private.key'); pubenv['RAZ_REGISTRY_PUBLISH_DIR']=str(registry); pubenv['RAZ_HOME']=str(home)
    run([compiler,'publish'],signed,pubenv)
    tokens=(registry/'index.txt').read_text().split(); assert len(tokens)==6, tokens
    trusted=work/'trusted.keys'; trusted.write_text(tokens[4]+' '+(keys/'registry.public.key').read_text().strip()+'\n')
    consumer=work/'signed-consumer'; make_consumer(consumer)
    trenv=env.copy(); trenv['RAZ_REGISTRY_INDEX']=str(registry/'index.txt'); trenv['RAZ_TRUSTED_KEYS']=str(trusted); trenv['RAZ_REQUIRE_SIGNATURES']='1'; trenv['RAZ_HOME']=str(home)
    run([compiler,'add','widget','registry:widget@^1.0.0'],consumer,trenv)
    assert 'widget = "' in (consumer/'raz.toml').read_text()
    bad=work/'bad-index.txt'; bad_tokens=tokens[:]; bad_tokens[5]=('0' if bad_tokens[5][0]!='0' else '1')+bad_tokens[5][1:]; bad.write_text(' '.join(bad_tokens)+'\n')
    bad_cons=work/'bad-consumer'; make_consumer(bad_cons); badenv=trenv.copy(); badenv['RAZ_REGISTRY_INDEX']=str(bad)
    p=subprocess.run([compiler,'add','widget','registry:widget@^1.0.0'],cwd=bad_cons,env=badenv); assert p.returncode==68, p.returncode

    # Parallel fetch: two independent tracked dependencies must overlap requests.
    source_reg=work/'source-reg'; source_reg.mkdir(); parallel_home=work/'parallel-home'
    for name in ('alpha','beta'):
        pkg=work/name; make_package(pkg,name)
        e=env.copy(); e['RAZ_REGISTRY_PUBLISH_DIR']=str(source_reg); e['RAZ_HOME']=str(parallel_home); run([compiler,'publish'],pkg,e)
    remote=work/'remote'; (remote/'packages').mkdir(parents=True)
    lines=[]
    for line in (source_reg/'index.txt').read_text().strip().splitlines():
        parts=line.split(); name,version,src,checksum=parts[:4]; dest=remote/'packages'/name; dest.mkdir(parents=True,exist_ok=True); archive=dest/f'{version}.dpk'; shutil.copy2(src,archive); lines.append(f'{name} {version} packages/{name}/{version}.dpk {checksum}')
    (remote/'index.txt').write_text('\n'.join(lines)+'\n')
    SlowRegistry.root=remote; SlowRegistry.inflight=0; SlowRegistry.max_inflight=0
    server=ThreadingHTTPServer(('127.0.0.1',0),SlowRegistry); thread=threading.Thread(target=server.serve_forever,daemon=True); thread.start(); base=f'http://127.0.0.1:{server.server_address[1]}'
    par=work/'parallel-consumer'; make_consumer(par); (par/'.raz.registry').write_text('alpha = "alpha@^1.0.0"\nbeta = "beta@^1.0.0"\n')
    penv=env.copy(); penv['RAZ_REGISTRY_URL']=base; penv['RAZ_HOME']=str(work/'parallel-client-home')
    run([compiler,'fetch'],par,penv)
    server.shutdown();server.server_close();thread.join(timeout=5)
    manifest=(par/'raz.toml').read_text(); assert 'alpha = "' in manifest and 'beta = "' in manifest
    assert SlowRegistry.max_inflight>=2, f'expected overlapping downloads, max inflight={SlowRegistry.max_inflight}'
    print(f'registry signing/parallel fetch: PASS (max concurrent requests={SlowRegistry.max_inflight})')
    return 0
if __name__=='__main__': raise SystemExit(main())
