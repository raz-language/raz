#!/usr/bin/env python3
# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0

from pathlib import Path
import hashlib, json, os, subprocess, sys, tempfile
ROOT=Path(__file__).resolve().parents[2]
EXE='.exe' if os.name=='nt' else ''
RETAINED=ROOT/'target'/'bootstrap'/'release'/'bin'/f'raz{EXE}'
LEGACY=ROOT/'compiler'/'target'/'release'/'bin'/f'raz-compiler{EXE}'
BIN=RETAINED if RETAINED.is_file() else LEGACY
def run(*args, cwd=None): return subprocess.run([str(BIN),*args],cwd=cwd or ROOT,text=True,capture_output=True)
def ok(name, cond, detail=''):
    print(('PASS' if cond else 'FAIL'),name + (f': {detail}' if detail else ''))
    if not cond: raise SystemExit(1)

p=run('--help'); ok('clean general help',p.returncode==0 and '\\n' not in p.stdout and 'loocally' not in p.stdout)
p=run('doctor','--color','never'); ok('doctor global color',p.returncode==0 and 'Raz toolchain is ready' in p.stdout)
p=run('diagnostics','--json');
try: cat=json.loads(p.stdout)
except Exception: cat={}
ok('diagnostic catalog JSON',p.returncode==0 and cat.get('schema')=='raz-diagnostic-catalog-v1')
for sh in ('bash','zsh','fish','powershell'):
    p=run('completions',sh); ok(f'{sh} completions',p.returncode==0 and 'build' in p.stdout and 'doctor' in p.stdout)
with tempfile.TemporaryDirectory() as td:
    root=Path(td)/'existing'; root.mkdir(); (root/'README.md').write_text('keep\n')
    p=run('init',str(root),'--color','never'); ok('init existing directory',p.returncode==0 and (root/'raz.toml').is_file() and (root/'README.md').read_text()=='keep\n')
    p=run('doctor',str(root),'--color','never'); report=root/'target'/'doctor'/'doctor.json'; ok('project doctor report',p.returncode==0 and report.is_file() and json.loads(report.read_text()).get('project')=='ready')
    created=Path(td)/'derived-package-name'; p=run('new',str(created),'--color','never'); ok('new derives package name',p.returncode==0 and 'name = \"derived-package-name\"' in (created/'raz.toml').read_text())
    bad=root/'src'/'bad.rz'; bad.write_text('fn bad() -> i64 { return missing; }\n')
    p=run('check','--diagnostic-format','short',str(bad)); ok('short diagnostics',p.returncode!=0 and f'{bad}:' in p.stderr and 'error[D2008]' in p.stderr and '  --> ' not in p.stderr)
    p=run('check','--diagnostic-format=json',str(bad));
    lines=[x for x in p.stdout.splitlines() if x.startswith('{')]
    data=json.loads(lines[-1]) if lines else {}
    ok('JSON diagnostics',p.returncode!=0 and data.get('schema')=='raz-diagnostics-v1' and data.get('diagnostics',[{}])[0].get('file')==str(bad))
print('PASS Pass 114 CLI productization')
