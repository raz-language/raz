#!/usr/bin/env python3
# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0

"""Run the same Raz source through Forge and LLVM and compare exit status.

This is an opt-in executable differential harness for a locally built self-host
compiler. It intentionally does not run during lightweight source validation.
"""
from __future__ import annotations
import argparse
from pathlib import Path
import subprocess
import tempfile
import os
import shutil


def run(cmd: list[str]) -> subprocess.CompletedProcess[str]:
    return subprocess.run(cmd, text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument('source', type=Path)
    ap.add_argument('--razc', required=True, type=Path)
    ap.add_argument('--forge-run', required=True, type=Path)
    ap.add_argument('--runtime', type=Path)
    ap.add_argument('--target')
    ap.add_argument('--cpu')
    ap.add_argument('--features')
    args = ap.parse_args()

    with tempfile.TemporaryDirectory(prefix='raz-backend-diff-') as tmp:
        tmp = Path(tmp)
        fir = tmp / 'program.fir'
        exe = tmp / ('program.exe' if subprocess.os.name == 'nt' else 'program')

        forge_compile = run([str(args.razc), '--backend=forge', str(args.source), str(fir)])
        if forge_compile.returncode != 0:
            print(forge_compile.stdout, end='')
            print(forge_compile.stderr, end='')
            raise SystemExit(f'Forge compilation failed: {forge_compile.returncode}')
        if args.runtime is None:
            raise SystemExit('backend-differential: --runtime is required for executable Forge comparison')
        forge_object = tmp / ('program.obj' if os.name == 'nt' else 'program.o')
        if os.name == 'nt':
            forge_codegen = run([str(args.forge_run), str(fir), f'--emit-coff={forge_object}', '--abi=windows'])
        else:
            forge_codegen = run([str(args.forge_run), str(fir), f'--emit-elf={forge_object}', '--abi=sysv'])
        if forge_codegen.returncode != 0:
            print(forge_codegen.stdout, end='')
            print(forge_codegen.stderr, end='')
            raise SystemExit(f'Forge object emission failed: {forge_codegen.returncode}')
        linker = os.environ.get('CXX') or shutil.which('clang++') or shutil.which('c++') or shutil.which('g++')
        if not linker:
            raise SystemExit('backend-differential: no C++ linker driver found')
        forge_link_cmd = [str(linker), str(forge_object), str(args.runtime)]
        if os.name != 'nt':
            forge_link_cmd += ['-lpthread', '-ldl']
        forge_link_cmd += ['-o', str(exe)]
        forge_link = run(forge_link_cmd)
        if forge_link.returncode != 0:
            print(forge_link.stdout, end='')
            print(forge_link.stderr, end='')
            raise SystemExit(f'Forge native link failed: {forge_link.returncode}')
        forge_exec = run([str(exe)])

        llvm_cmd = [str(args.razc), '--backend=llvm', '--emit=exe']
        if args.target:
            llvm_cmd.append(f'--target={args.target}')
        if args.cpu:
            llvm_cmd.append(f'--cpu={args.cpu}')
        if args.features:
            llvm_cmd.append(f'--features={args.features}')
        if args.runtime:
            llvm_cmd.append(f'--runtime={args.runtime}')
        llvm_cmd += [str(args.source), str(exe)]
        llvm_compile = run(llvm_cmd)
        if llvm_compile.returncode != 0:
            print(llvm_compile.stdout, end='')
            print(llvm_compile.stderr, end='')
            raise SystemExit(f'LLVM compilation/link failed: {llvm_compile.returncode}')
        llvm_exec = run([str(exe)])

        if forge_exec.returncode != llvm_exec.returncode:
            print(f'backend-differential: FAIL Forge={forge_exec.returncode} LLVM={llvm_exec.returncode}')
            return 1
        print(f'backend-differential: PASS exit={llvm_exec.returncode}')
        return 0


if __name__ == '__main__':
    raise SystemExit(main())
