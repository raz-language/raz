#!/usr/bin/env python3
# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0

from __future__ import annotations
import argparse, hashlib, os, shutil, subprocess
from pathlib import Path
from compiler_test_driver import build_test_compiler

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

def make_named_pkg(root:Path,name:str,ver:str):
    d=root/f'{name}-{ver.replace(".","_")}'; (d/'src').mkdir(parents=True)
    text=f'[package]\nname = "{name}"\nversion = "{ver}"\nkind = "library"\nsource = "src"\nentry = "src/main.rz"\n\n[dependencies]\n'
    (d/'raz.toml').write_text(text); (d/'src/main.rz').write_text('public fn value() -> i64 { return 1; }\n')
    return f'{name} {ver} {d} {tree_hash(d)}'

def make_pkg(root:Path,ver:str):
    return make_named_pkg(root,'widget',ver)

def main():
    ap=argparse.ArgumentParser();ap.add_argument('--compiler');ap.add_argument('--work',required=True);ap.add_argument('--raz');ap.add_argument('--root');ap.add_argument('--linker');ns=ap.parse_args()
    work=Path(ns.work).resolve();shutil.rmtree(work,ignore_errors=True);work.mkdir(parents=True)
    env=os.environ.copy()
    if ns.compiler:
        c=str(Path(ns.compiler).resolve())
    else:
        root=Path(ns.root).resolve(); c=str(build_test_compiler(root, work, ns.raz, ns.linker, env))
    reg=work/'registry';reg.mkdir(parents=True);app=work/'app';(app/'src').mkdir(parents=True)
    entries=[make_pkg(reg,v) for v in ('1.2.0','1.7.3','2.0.0','1.10.0-alpha.1')]; index=reg/'index';index.write_text('\n'.join(entries)+'\n')
    (app/'raz.toml').write_text('[package]\nname = "app"\nversion = "0.1.0"\nkind = "executable"\nsource = "src"\nentry = "src/main.rz"\n\n[dependencies]\n');(app/'src/main.rz').write_text('fn main() -> i64 { return 0; }\n')
    env['RAZ_REGISTRY_INDEX']=str(index); env['RAZ_HOME']=str(work/'home')
    # Stable constraints must not accidentally select a numerically newer prerelease.
    assert 'widget@1.7.3' in run([c,'registry','widget','^1.2.0'],app,env).stdout
    assert 'widget@1.2.0' in run([c,'registry','widget','~1.2.0'],app,env).stdout
    assert 'widget@2.0.0' in run([c,'registry','widget','>=1.2.0'],app,env).stdout
    assert 'widget@1.10.0-alpha.1' in run([c,'registry','widget','1.10.0-alpha.1'],app,env).stdout
    run([c,'add','widget','registry:widget@^1.2.0'],app,env)
    assert 'widget@^1.2.0' in (app/'.raz.registry').read_text(); assert 'widget = "^1.2.0"' in (app/'raz.toml').read_text(); assert '1.7.3' in (app/'.raz.cache').read_text(); assert any((work/'home/store').glob('*/raz.toml'))
    helper_entry=make_named_pkg(reg,'helper','0.3.0')
    with index.open('a') as f:f.write(helper_entry+'\n')
    run([c,'add','helper','registry:helper@^0.3.0'],app,env)

    # A registry package can carry a portable lockfile for its own registry
    # dependencies. Resolving the parent must hydrate those exact pins too.
    transitive_entry=make_named_pkg(reg,'transitive','0.4.0')
    transitive_parts=transitive_entry.split()
    transitive_dir=Path(transitive_parts[2])
    transitive_checksum=transitive_parts[3]
    parent_dir=reg/'parent-1_0_0'; (parent_dir/'src').mkdir(parents=True)
    (parent_dir/'raz.toml').write_text(
        '[package]\nname = "parent"\nversion = "1.0.0"\nkind = "library"\nsource = "src"\nentry = "src/main.rz"\n\n'
        f'[dependencies]\ntransitive = "registry:{transitive_checksum}"\n'
    )
    (parent_dir/'src/main.rz').write_text('public fn parent_value() -> i64 { return 2; }\n')
    (parent_dir/'raz.lock').write_text(
        'version = 1\n\n[[package]]\n'
        'name = "transitive"\n'
        'version = "0.4.0"\n'
        f'path = "registry:{transitive_checksum}"\n'
        'source = "registry"\n'
        f'checksum = "{transitive_checksum}"\n'
    )
    parent_entry=f'parent 1.0.0 {parent_dir} {tree_hash(parent_dir)}'
    with index.open('a') as f:f.write(transitive_entry+'\n'+parent_entry+'\n')
    transitive_store=work/'home/store'/transitive_checksum
    shutil.rmtree(transitive_store,ignore_errors=True)
    run([c,'add','parent','registry:parent@^1.0.0'],app,env)
    assert (transitive_store/'raz.toml').is_file(), 'transitive registry lock was not hydrated'

    with index.open('a') as f:f.write(make_named_pkg(reg,'devtool','0.1.0')+'\n')
    run([c,'add','devtool','registry:devtool@^0.1.0','--dev'],app,env)
    tracking=(app/'.raz.registry').read_text()
    assert 'devtool@^0.1.0|3' in tracking, tracking
    manifest=(app/'raz.toml').read_text(); assert '[dev-dependencies]' in manifest and 'devtool = ' in manifest
    cache=(app/'.raz.cache').read_text()
    assert 'widget widget 1.7.3 ' in cache and 'helper helper 0.3.0 ' in cache and 'devtool devtool 0.1.0 ' in cache, cache
    with index.open('a') as f:f.write(make_pkg(reg,'1.9.0')+'\n')
    with index.open('a') as f:f.write(make_named_pkg(reg,'devtool','0.1.5')+'\n')
    run([c,'update'],app,env); manifest=(app/'raz.toml').read_text(); assert 'widget = "^1.2.0"' in manifest; assert '[dev-dependencies]' in manifest and 'devtool = ' in manifest; lock=(app/'raz.lock').read_bytes(); assert b'version = "1.9.0"' in lock and b'version = "0.1.5"' in lock; assert b'source = "registry"' in lock and b'checksum = "' in lock; assert str(work/'home/store').encode() not in lock
    stable=hashlib.sha256(lock).hexdigest()
    with index.open('a') as f:f.write(make_pkg(reg,'1.9.5')+'\n')
    run([c,'fetch'],app,env); assert hashlib.sha256((app/'raz.lock').read_bytes()).hexdigest()==stable; assert '1.9.0' in (app/'.raz.cache').read_text()
    tree=run([c,'tree'],app,env).stdout; assert 'app' in tree and 'widget' in tree
    run([c,'update'],app,env); assert b'version = "1.9.5"' in (app/'raz.lock').read_bytes()
    run([c,'remove','widget'],app,env); assert 'widget@^1.2.0' not in (app/'.raz.registry').read_text(); before=(app/'raz.toml').read_text();run([c,'update'],app,env);assert (app/'raz.toml').read_text()==before
    run([c,'add','widget@^1.2.0'],app,env)
    assert 'widget@^1.2.0' in (app/'.raz.registry').read_text(), 'official registry shorthand was not tracked'
    # Stored packages remain usable offline even if the registry source disappears.
    cache_row=next(line for line in (app/'.raz.cache').read_text().splitlines() if line.startswith('widget ')); selected_store=Path(cache_row.split()[3]).resolve()
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
