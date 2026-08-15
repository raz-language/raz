# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0

#!/usr/bin/env python3
import argparse, pathlib, subprocess, statistics, re, json, sys
root=pathlib.Path(__file__).resolve().parents[2]
p=argparse.ArgumentParser()
p.add_argument('--build',default='build/release-strict')
p.add_argument('--samples',type=int,default=5)
p.add_argument('--cc',default='clang')
p.add_argument('--opt-level',choices=['O2','O3'],default='O2')
p.add_argument('--check',action='store_true',help='fail when any kernel exceeds its independent ratio threshold')
p.add_argument('--thresholds',default='benchmarks/broad/thresholds.json')
a=p.parse_args()
out=root/'build'/'broad-bench'; out.mkdir(parents=True,exist_ok=True)
forge=root/a.build/'forge'
def run(cmd): subprocess.run([str(x) for x in cmd],check=True,cwd=root)
def text_size(path):
    text=subprocess.check_output(['size','-A',str(path)],text=True)
    for line in text.splitlines():
        fields=line.split()
        if fields and fields[0]=='.text': return int(fields[1])
    return None
opt='-'+a.opt_level
run([forge,'compile',root/'benchmarks/broad/kernels.fir','--format=elf',opt,'-o',out/'forge.o'])
run([a.cc,opt,'-c',root/'benchmarks/broad/reference.c','-o',out/'llvm.o'])
run([a.cc,opt,root/'benchmarks/broad/harness.c',out/'forge.o',out/'llvm.o','-lm','-o',out/'broad-bench'])
rows={}
for _ in range(a.samples):
    text=subprocess.check_output([out/'broad-bench'],text=True)
    for line in text.splitlines()[1:]:
        m=re.match(r'(\S+)\s+([0-9.]+)\s+([0-9.]+)\s+([0-9.]+)',line)
        if m: rows.setdefault(m.group(1),[]).append(tuple(map(float,m.groups()[1:])))
print(f'optimization: {opt}')
print('kernel              forge_ns    llvm_ns    ratio')
report={}
for k,v in rows.items():
    f=statistics.median(x[0] for x in v); l=statistics.median(x[1] for x in v); ratio=f/l
    report[k]={'forge_ns':f,'llvm_ns':l,'ratio':ratio}
    print(f'{k:<18} {f:10.3f} {l:10.3f} {ratio:8.3f}')
report['_metadata']={
    'optimization': opt,
    'samples': a.samples,
    'forge_text_bytes': text_size(out/'forge.o'),
    'llvm_text_bytes': text_size(out/'llvm.o')
}
print(f"text bytes: Forge={report['_metadata']['forge_text_bytes']} LLVM={report['_metadata']['llvm_text_bytes']}")
(out/f'results-{a.opt_level.lower()}.json').write_text(json.dumps(report,indent=2)+'\n')
if a.check:
    thresholds=json.loads((root/a.thresholds).read_text())
    failures=[]
    for kernel,limit in thresholds.items():
        if kernel not in report:
            failures.append(f'{kernel}: missing result')
        elif report[kernel]['ratio'] > float(limit):
            failures.append(f"{kernel}: ratio {report[kernel]['ratio']:.3f} exceeds {float(limit):.3f}")
    if failures:
        print('broad performance gate failed:',file=sys.stderr)
        for failure in failures: print('  '+failure,file=sys.stderr)
        raise SystemExit(4)
    print('broad performance gate: PASS')
