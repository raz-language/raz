#!/usr/bin/env python3
# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0

from __future__ import annotations
import argparse, hashlib, os, shutil, subprocess
from pathlib import Path

def run(args,cwd,env,expect=0):
    p=subprocess.run(args,cwd=cwd,env=env,text=True,stdout=subprocess.PIPE,stderr=subprocess.PIPE)
    if p.returncode!=expect: raise RuntimeError(f"failed {p.returncode}: {' '.join(map(str,args))}\n{p.stdout}\n{p.stderr}")
    return p

def tree_hash(root:Path)->str:
    h=1469598103934665603; mask=(1<<64)-1
    files=sorted((p for p in root.rglob('*') if p.is_file()), key=lambda p:p.relative_to(root).as_posix())
    for p in files:
        for b in p.relative_to(root).as_posix().encode()+b'\0'+p.read_bytes()+b'\xff': h=((h^b)*1099511628211)&mask
    return f'{h:016x}'

def make_pkg(root:Path,ver:str):
    d=root/f'widget-{ver.replace(".","_")}'; (d/'src').mkdir(parents=True)
    text=f'[package]\nname = "widget"\nversion = "{ver}"\nkind = "library"\nsource = "src"\nentry = "src/main.rz"\n\n[dependencies]\n'
    (d/'raz.toml').write_text(text); (d/'src/main.rz').write_text('public fn value() -> i64 { return 1; }\n')
    return f'widget {ver} {d} {tree_hash(d)}'

def main():
    ap=argparse.ArgumentParser();ap.add_argument('--compiler');ap.add_argument('--work',required=True);ap.add_argument('--raz');ap.add_argument('--root');ap.add_argument('--linker');ns=ap.parse_args()
    work=Path(ns.work).resolve();shutil.rmtree(work,ignore_errors=True);work.mkdir(parents=True)
    env=os.environ.copy()
    if ns.compiler:
        c=str(Path(ns.compiler).resolve())
    else:
        root=Path(ns.root).resolve(); project=work/'compiler'; shutil.copytree(root/'compiler',project); env['RAZ_LINKER']=ns.linker
        run([ns.raz,'build',str(project),'--target','host','--profile','debug','--force'],root,env)
        c=str(project/'target'/'host'/'debug'/('raz-compiler.exe' if os.name=='nt' else 'raz-compiler'))
    reg=work/'registry';reg.mkdir(parents=True);app=work/'app';(app/'src').mkdir(parents=True)
    entries=[make_pkg(reg,v) for v in ('1.2.0','1.7.3','2.0.0')]; index=reg/'index';index.write_text('\n'.join(entries)+'\n')
    (app/'raz.toml').write_text('[package]\nname = "app"\nversion = "0.1.0"\nkind = "executable"\nsource = "src"\nentry = "src/main.rz"\n\n[dependencies]\n');(app/'src/main.rz').write_text('fn main() -> i64 { return 0; }\n')
    env['RAZ_REGISTRY_INDEX']=str(index); env['RAZ_HOME']=str(work/'home')
    assert 'widget@1.7.3' in run([c,'registry','widget','^1.2.0'],app,env).stdout
    assert 'widget@1.2.0' in run([c,'registry','widget','~1.2.0'],app,env).stdout
    assert 'widget@2.0.0' in run([c,'registry','widget','>=1.2.0'],app,env).stdout
    run([c,'add','widget','registry:widget@^1.2.0'],app,env)
    assert 'widget@^1.2.0' in (app/'.raz.registry').read_text(); assert '/store/' in (app/'raz.toml').read_text().replace('\\','/'); assert '1.7.3' in (app/'.raz.cache').read_text(); assert any((work/'home/store').glob('*/raz.toml'))
    with index.open('a') as f:f.write(make_pkg(reg,'1.9.0')+'\n')
    run([c,'update'],app,env); assert '/store/' in (app/'raz.toml').read_text().replace('\\','/'); lock=(app/'raz.lock').read_bytes(); assert b'version = "1.9.0"' in lock
    stable=hashlib.sha256(lock).hexdigest();run([c,'update'],app,env);assert hashlib.sha256((app/'raz.lock').read_bytes()).hexdigest()==stable
    run([c,'remove','widget'],app,env); assert 'widget@^1.2.0' not in (app/'.raz.registry').read_text(); before=(app/'raz.toml').read_text();run([c,'update'],app,env);assert (app/'raz.toml').read_text()==before
    run([c,'add','widget@^1.2.0'],app,env)
    assert 'widget@^1.2.0' in (app/'.raz.registry').read_text(), 'official registry shorthand was not tracked'
    # Stored packages remain usable offline even if the registry source disappears.
    manifest_text=(app/'raz.toml').read_text(); dep_line=next(line for line in manifest_text.splitlines() if line.strip().startswith('widget =')); selected_rel=dep_line.split('=',1)[1].strip().strip(chr(34)); selected_store=Path(selected_rel).resolve()
    shutil.rmtree(reg/'widget-1_9_0', ignore_errors=True)
    offline=env.copy(); offline['RAZ_OFFLINE']='1'
    run([c,'update'],app,offline)
    (selected_store/'raz.toml').write_text((selected_store/'raz.toml').read_text()+'# corrupt\n')
    run([c,'update'],app,offline,expect=63)
    shutil.rmtree(work/'home/store', ignore_errors=True)
    run([c,'update'],app,offline,expect=64)
    print('registry package store: PASS')
    return 0
if __name__=='__main__': raise SystemExit(main())
