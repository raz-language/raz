#!/usr/bin/env python3
# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0

from __future__ import annotations
import argparse, os, shutil, subprocess, sys
from pathlib import Path


def run(args: list[str], *, cwd: Path, env: dict[str, str], expect: int = 0) -> subprocess.CompletedProcess[str]:
    p = subprocess.run(args, cwd=cwd, env=env, text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    if p.returncode != expect:
        raise RuntimeError(f"command failed ({p.returncode}): {' '.join(args)}\nstdout:\n{p.stdout}\nstderr:\n{p.stderr}")
    return p


def require(text: str, *needles: str) -> None:
    for needle in needles:
        if needle not in text:
            raise RuntimeError(f"missing expected CLI text: {needle!r}\n{text}")


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument('--raz', required=True)
    ap.add_argument('--root', required=True)
    ap.add_argument('--work', required=True)
    ap.add_argument('--linker', required=True)
    ns = ap.parse_args()
    root = Path(ns.root).resolve()
    work = Path(ns.work).resolve()
    project = work / 'compiler'
    shutil.rmtree(work, ignore_errors=True)
    shutil.copytree(root / 'compiler', project)
    env = os.environ.copy()
    env['RAZ_LINKER'] = ns.linker
    run([ns.raz, 'build', str(project), '--profile', 'debug', '--force'], cwd=root, env=env)
    compiler = project / 'target' / 'host' / 'debug' / ('raz-compiler.exe' if os.name == 'nt' else 'raz-compiler')
    if not compiler.is_file():
        raise RuntimeError(f'missing production compiler: {compiler}')

    general = run([str(compiler), '--help'], cwd=work, env=env).stdout
    require(general, 'Commands:', 'build', 'forge', 'llvm', '--backend=forge', '--backend=llvm', '--llvm', '--forge')
    llvm_help = run([str(compiler), 'llvm', '--help'], cwd=work, env=env).stdout
    require(llvm_help, '--emit=llvm', '--emit=obj', '--emit=exe', '--target=<triple>', '--cpu=<name>', '--features=<a,b,...>', '--linker=<driver>')
    backends = run([str(compiler), 'backends'], cwd=work, env=env).stdout
    require(backends, 'forge', 'llvm', 'built-in')
    targets = run([str(compiler), 'targets'], cwd=work, env=env).stdout
    require(targets, 'Forge:', 'LLVM:', 'x86_64-pc-windows-msvc')
    doctor = run([str(compiler), 'doctor'], cwd=work, env=env).stdout
    require(doctor, 'Raz compiler', 'Forge backend', 'LLVM backend', 'Host:')
    version = run([str(compiler), '--version'], cwd=work, env=env).stdout
    require(version, 'raz 1.0.0', 'Forge backend: built-in', 'LLVM IR emitter: built-in', 'LLVM toolchain: external clang/clang++')

    source = work / 'cli-smoke.rz'
    source.write_text('fn add(i64 a, i64 b) -> i64 { return a + b; }\nfn main() -> i64 { return add(20, 22) - 42; }\n', encoding='utf-8')
    run([str(compiler), 'check', str(source)], cwd=work, env=env)
    fir = work / 'cli-smoke.fir'
    run([str(compiler), 'forge', str(source), str(fir)], cwd=work, env=env)
    if not fir.is_file() or fir.stat().st_size == 0:
        raise RuntimeError('Forge CLI did not emit FIR')
    ll = work / 'cli-smoke.ll'
    run([str(compiler), 'llvm', '--emit=llvm', str(source), str(ll)], cwd=work, env=env)
    if not ll.is_file() or 'define' not in ll.read_text(encoding='utf-8', errors='replace'):
        raise RuntimeError('LLVM CLI did not emit LLVM IR')
    print('CLI capabilities: PASS')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
