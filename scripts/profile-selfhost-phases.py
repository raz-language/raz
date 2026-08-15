#!/usr/bin/env python3
# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0

import argparse, os, pathlib, shutil, subprocess, time

def main():
    ap=argparse.ArgumentParser(description="Profile Raz self-host compiler phase boundaries without modifying the compiler being measured.")
    ap.add_argument("compiler")
    ap.add_argument("source_root")
    ap.add_argument("workdir")
    ap.add_argument("--output", default="stage-profile.o")
    ap.add_argument("--opt", default="0")
    args=ap.parse_args()
    src=pathlib.Path(args.source_root).resolve(); work=pathlib.Path(args.workdir).resolve()
    shutil.rmtree(work, ignore_errors=True); (work/'src').mkdir(parents=True)
    for f in (src/'src').iterdir():
        if f.is_file(): shutil.copy2(f, work/'src'/f.name)
    shutil.copy2(src/'bootstrap-source-order.txt', work/'stage-input.txt')
    diag=work/'stage1-diagnostic.txt'; forge_profile=work/'forge-phase-profile.txt'
    env=os.environ.copy(); env['RAZ_FORGE_PHASE_PROFILE']=str(forge_profile)
    cmd=[str(pathlib.Path(args.compiler).resolve()), '--forge-structured-only', f'--opt={args.opt}', 'stage-input.txt', args.output]
    start=time.perf_counter(); proc=subprocess.Popen(cmd,cwd=work,env=env); last=''; seen={}
    while proc.poll() is None:
        try:
            raw=diag.read_text().strip()
            if raw and raw != last:
                elapsed=time.perf_counter()-start; phase=int(raw.split()[0]); seen.setdefault(phase,elapsed)
                print(f"phase {phase}: {elapsed:.6f}s ({raw})", flush=True); last=raw
        except (OSError,ValueError): pass
        time.sleep(0.003)
    rc=proc.wait(); total=time.perf_counter()-start
    print(f"exit={rc} total={total:.6f}s")
    if 92 in seen and 93 in seen: print(f"hir~={seen[93]-seen[92]:.6f}s")
    if 93 in seen and 94 in seen: print(f"mir~={seen[94]-seen[93]:.6f}s")
    if forge_profile.exists(): print(forge_profile.read_text(), end='')
    raise SystemExit(rc)
if __name__ == '__main__': main()
