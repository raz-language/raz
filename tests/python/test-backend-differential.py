#!/usr/bin/env python3
# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0

"""Run the same Raz source through Forge and LLVM and compare observable behavior.

The harness compares exit status, stdout, and stderr. It remains opt-in for
lightweight source validation because it requires executable toolchain artifacts.
"""
from __future__ import annotations
import argparse
from pathlib import Path
import subprocess
import tempfile
import os
import shutil
import sys


def run(cmd: list[str]) -> subprocess.CompletedProcess[str]:
    return subprocess.run(cmd, text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)




def runtime_link_dependencies(runtime: Path) -> list[str]:
    """Mirror the installed toolchain's self-describing runtime link layout."""
    configured = os.environ.get('RAZ_RUNTIME_LINK_DEPS', '')
    if configured:
        values = configured.replace(os.pathsep, ';').split(';')
        return [value for value in values if value and Path(value).is_file()]
    directory = runtime.parent
    if os.name == 'nt':
        names = ['raz_runtime_ssl.lib', 'raz_runtime_crypto.lib', 'raz_runtime_ssl.a', 'raz_runtime_crypto.a']
    else:
        names = ['libraz_runtime_ssl.so', 'libraz_runtime_crypto.so', 'libraz_runtime_ssl.a', 'libraz_runtime_crypto.a']
    return [str(directory / item) for item in names if (directory / item).is_file()]

def resolve_forge_cli(forge_run: Path) -> Path:
    """Resolve Forge's object-emission CLI from the execution-tool path.

    Release-gate callers historically pass --forge-run because the differential
    harness also used the old combined Forge tool. Forge 2.0 splits compilation
    into `forge` and execution into `forge-run`. Preserve the public harness
    argument while selecting the sibling compiler when the split tools are used.
    """
    name = forge_run.name.lower()
    if name in {'forge-run', 'forge-run.exe'}:
        sibling = forge_run.with_name('forge.exe' if name.endswith('.exe') else 'forge')
        if sibling.is_file():
            return sibling
        raise SystemExit(
            f'backend-differential: {forge_run} is the execution CLI, but sibling Forge compiler {sibling} is missing'
        )
    return forge_run


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument('source', type=Path)
    ap.add_argument('--razc', required=True, type=Path)
    ap.add_argument('--forge-run', required=True, type=Path)
    ap.add_argument('--runtime', type=Path)
    ap.add_argument('--forge-structured', action='store_true',
                    help='prefer Raz production structured Forge object emission, falling back to textual FIR when unsupported')
    ap.add_argument('--target')
    ap.add_argument('--cpu')
    ap.add_argument('--features')
    args = ap.parse_args()

    args.source = args.source.resolve()
    args.razc = args.razc.resolve()
    args.forge_run = args.forge_run.resolve()
    if args.runtime is not None:
        args.runtime = args.runtime.resolve()

    with tempfile.TemporaryDirectory(prefix='raz-backend-diff-') as tmp:
        tmp = Path(tmp)
        fir = tmp / 'program.fir'
        exe = tmp / ('program.exe' if subprocess.os.name == 'nt' else 'program')

        if args.runtime is None:
            raise SystemExit('backend-differential: --runtime is required for executable Forge comparison')
        forge_object = tmp / ('program.obj' if os.name == 'nt' else 'program.o')
        structured_ready = False
        if args.forge_structured:
            forge_compile = run([
                str(args.razc), '--backend=forge', '--forge-native', '--forge-structured-only',
                '--opt=3', str(args.source), str(fir),
            ])
            structured_ready = forge_compile.returncode == 0 and forge_object.is_file()
            if not structured_ready and forge_object.exists():
                forge_object.unlink()
        if not structured_ready:
            forge_compile = run([str(args.razc), '--backend=forge', str(args.source), str(fir)])
            if forge_compile.returncode != 0:
                print(forge_compile.stdout, end='')
                print(forge_compile.stderr, end='')
                raise SystemExit(f'Forge compilation failed: {forge_compile.returncode}')
            if os.name == 'nt':
                object_format = 'coff'
            elif sys.platform == 'darwin':
                object_format = 'macho'
            else:
                object_format = 'elf'
            forge_cli = resolve_forge_cli(args.forge_run)
            forge_codegen = run([
                str(forge_cli), 'compile', str(fir),
                f'--format={object_format}', '-o', str(forge_object),
            ])
            if forge_codegen.returncode != 0:
                print(forge_codegen.stdout, end='')
                print(forge_codegen.stderr, end='')
                raise SystemExit(f'Forge object emission failed: {forge_codegen.returncode}')
        linker = os.environ.get('CXX') or shutil.which('clang++') or shutil.which('c++') or shutil.which('g++')
        if not linker:
            raise SystemExit('backend-differential: no C++ linker driver found')
        forge_link_cmd = [str(linker), str(forge_object), str(args.runtime), *runtime_link_dependencies(args.runtime)]
        if os.name != 'nt':
            forge_link_cmd += ['-lpthread', '-ldl']
        else:
            # The runtime archive pulls Windows sockets/crypto members, matching
            # what the Forge and bootstrap link drivers pass.
            forge_link_cmd += ['-lws2_32', '-lbcrypt', '-lcrypt32']
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

        mismatches: list[str] = []
        if forge_exec.returncode != llvm_exec.returncode:
            mismatches.append(f'exit Forge={forge_exec.returncode} LLVM={llvm_exec.returncode}')
        if forge_exec.stdout != llvm_exec.stdout:
            mismatches.append('stdout differs')
        if forge_exec.stderr != llvm_exec.stderr:
            mismatches.append('stderr differs')
        if mismatches:
            print('backend-differential: FAIL ' + '; '.join(mismatches))
            if forge_exec.stdout != llvm_exec.stdout:
                print('--- Forge stdout ---')
                print(forge_exec.stdout, end='')
                print('--- LLVM stdout ---')
                print(llvm_exec.stdout, end='')
            if forge_exec.stderr != llvm_exec.stderr:
                print('--- Forge stderr ---')
                print(forge_exec.stderr, end='')
                print('--- LLVM stderr ---')
                print(llvm_exec.stderr, end='')
            return 1
        print(f'backend-differential: PASS exit={llvm_exec.returncode} stdout={len(llvm_exec.stdout)}B stderr={len(llvm_exec.stderr)}B')
        return 0


if __name__ == '__main__':
    raise SystemExit(main())
