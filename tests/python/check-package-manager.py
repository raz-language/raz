#!/usr/bin/env python3
# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0

from __future__ import annotations
import argparse, hashlib, os, shutil, subprocess
from pathlib import Path
from compiler_test_driver import build_test_compiler

def run(args, cwd, env, expect=0):
    p=subprocess.run(args,cwd=cwd,env=env,text=True,stdout=subprocess.PIPE,stderr=subprocess.PIPE)
    if p.returncode!=expect:
        raise RuntimeError(f"command failed {p.returncode} expected {expect}: {' '.join(map(str,args))}\nstdout:\n{p.stdout}\nstderr:\n{p.stderr}")
    return p

def write_pkg(root:Path,name:str,version:str,kind:str='library',deps:str=''):
    (root/'src').mkdir(parents=True,exist_ok=True)
    (root/'raz.toml').write_text(f'''[package]\nname = "{name}"\nversion = "{version}"\nkind = "{kind}"\nsource = "src"\nentry = "src/main.rz"\n\n[dependencies]\n{deps}''',encoding='utf-8')
    (root/'src'/'main.rz').write_text('public fn value() -> i64 { return 0; }\n' if kind=='library' else 'fn main() -> i64 { return 0; }\n',encoding='utf-8')

def main():
    ap=argparse.ArgumentParser(); ap.add_argument('--raz',required=True);ap.add_argument('--root',required=True);ap.add_argument('--work',required=True);ap.add_argument('--linker',required=True);ns=ap.parse_args()
    root=Path(ns.root).resolve(); work=Path(ns.work).resolve(); shutil.rmtree(work,ignore_errors=True); work.mkdir(parents=True)
    env=os.environ.copy()
    compiler=build_test_compiler(root, work, ns.raz, ns.linker, env)
    rootpkg=work/'root'; dep1=work/'dep'; dep2=work/'dep2'
    write_pkg(rootpkg,'root-app','1.2.3','executable');write_pkg(dep1,'dep-one','0.4.0');write_pkg(dep2,'dep-two','2.0.0',deps='one = "../dep"\n')
    run([str(compiler),'add','two','../dep2'],rootpkg,env)
    manifest=(rootpkg/'raz.toml').read_text(); assert 'two = "../dep2"' in manifest
    graph=run([str(compiler),'graph'],rootpkg,env).stdout
    for text in ('root-app@1.2.3','dep-two@2.0.0','dep-one@0.4.0'): assert text in graph
    metadata=run([str(compiler),'metadata'],rootpkg,env).stdout
    for text in ('package=root-app','dependency=two=../dep2','package=dep-two','dependency=one=../dep','package=dep-one'): assert text in metadata
    lock=rootpkg/'raz.lock'; first=lock.read_bytes(); first_hash=hashlib.sha256(first).hexdigest()
    for text in (b'name = "root-app"',b'name = "dep-two"',b'name = "dep-one"',b'manifest_hash = "'): assert text in first
    run([str(compiler),'update'],rootpkg,env); assert hashlib.sha256(lock.read_bytes()).hexdigest()==first_hash
    run([str(compiler),'lock'],rootpkg,env); assert hashlib.sha256(lock.read_bytes()).hexdigest()==first_hash
    run([str(compiler),'remove','two'],rootpkg,env); assert 'two = "../dep2"' not in (rootpkg/'raz.toml').read_text(); assert b'dep-two' not in lock.read_bytes()
    missing=run([str(compiler),'remove','missing'],rootpkg,env,expect=59)
    assert 'dependency alias was not found' in missing.stderr

    conflict=work/'conflict'; shared1=work/'shared-v1'; shared2=work/'shared-v2'
    write_pkg(conflict,'conflict-app','0.1.0','executable',deps='left = "../shared-v1"\nright = "../shared-v2"\n')
    write_pkg(shared1,'shared','1.0.0')
    write_pkg(shared2,'shared','2.0.0')
    conflict_result=run([str(compiler),'lock'],conflict,env,expect=73)
    assert 'conflicting package identities' in conflict_result.stderr
    assert not (conflict/'raz.lock').exists()

    print('package manager: PASS')
    return 0
if __name__=='__main__': raise SystemExit(main())
