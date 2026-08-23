#!/usr/bin/env python3
# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0

"""Compare one Raz program across Forge, LLVM, and WebAssembly/WASI.

Exit status, stdout, and stderr must all match. This executable qualification
harness is intentionally opt-in because it needs an already-built toolchain and
a Node.js runtime with WASI support.
"""
from __future__ import annotations
import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile


def run(cmd: list[str], *, cwd: Path | None = None) -> subprocess.CompletedProcess[str]:
    return subprocess.run(cmd, cwd=cwd, text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)


def fail(label: str, result: subprocess.CompletedProcess[str]) -> None:
    print(result.stdout, end='')
    print(result.stderr, end='')
    raise SystemExit(f'{label} failed: {result.returncode}')


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument('source', type=Path)
    ap.add_argument('--razc', required=True, type=Path)
    ap.add_argument('--forge-run', required=True, type=Path)
    ap.add_argument('--runtime', required=True, type=Path)
    ap.add_argument('--node', default=shutil.which('node') or 'node')
    ap.add_argument('--target')
    ap.add_argument('--cpu')
    ap.add_argument('--features')
    args = ap.parse_args()

    source = args.source.resolve()
    with tempfile.TemporaryDirectory(prefix='raz-backend-diff-wasm-') as td:
        tmp = Path(td)
        fir = tmp / 'program.fir'
        native = tmp / ('program.exe' if os.name == 'nt' else 'program')
        forge_object = tmp / ('program.obj' if os.name == 'nt' else 'program.o')
        wasm = tmp / 'program.wasm'

        forge_compile = run([str(args.razc), '--backend=forge', str(source), str(fir)])
        if forge_compile.returncode != 0:
            fail('Forge compilation', forge_compile)
        if os.name == 'nt':
            forge_codegen = run([str(args.forge_run), str(fir), f'--emit-coff={forge_object}', '--abi=windows'])
        else:
            forge_codegen = run([str(args.forge_run), str(fir), f'--emit-elf={forge_object}', '--abi=sysv'])
        if forge_codegen.returncode != 0:
            fail('Forge object emission', forge_codegen)
        linker = os.environ.get('CXX') or shutil.which('clang++') or shutil.which('c++') or shutil.which('g++')
        if not linker:
            raise SystemExit('backend-differential-wasm: no C++ linker driver found')
        link_cmd = [str(linker), str(forge_object), str(args.runtime)]
        if os.name != 'nt':
            link_cmd += ['-lpthread', '-ldl']
        link_cmd += ['-o', str(native)]
        linked = run(link_cmd)
        if linked.returncode != 0:
            fail('Forge native link', linked)
        forge_exec = run([str(native)])

        llvm_cmd = [str(args.razc), '--backend=llvm', '--emit=exe']
        if args.target:
            llvm_cmd.append(f'--target={args.target}')
        if args.cpu:
            llvm_cmd.append(f'--cpu={args.cpu}')
        if args.features:
            llvm_cmd.append(f'--features={args.features}')
        llvm_cmd += [f'--runtime={args.runtime}', str(source), str(native)]
        llvm_compile = run(llvm_cmd)
        if llvm_compile.returncode != 0:
            fail('LLVM compilation/link', llvm_compile)
        llvm_exec = run([str(native)])

        wasm_compile = run([str(args.razc), '--backend=wasm', str(source), str(wasm)])
        if wasm_compile.returncode != 0:
            fail('WASM compilation', wasm_compile)

        runner = tmp / 'run-wasi.mjs'
        wasm_stdout = tmp / 'wasm.stdout'
        wasm_stderr = tmp / 'wasm.stderr'
        runner.write_text(
            "import fs from 'node:fs';\n"
            "import { WASI } from 'node:wasi';\n"
            "const path = process.argv[2];\n"
            "const outPath = process.argv[3];\n"
            "const errPath = process.argv[4];\n"
            "const stdout = fs.openSync(outPath, 'w');\n"
            "const stderr = fs.openSync(errPath, 'w');\n"
            "const wasi = new WASI({ version: 'preview1', args: [], env: {}, preopens: {}, stdout, stderr, returnOnExit: true });\n"
            "const bytes = fs.readFileSync(path);\n"
            "const module = await WebAssembly.compile(bytes);\n"
            "const instance = await WebAssembly.instantiate(module, { wasi_snapshot_preview1: wasi.wasiImport });\n"
            "let code = 0;\n"
            "if (instance.exports._start) code = wasi.start(instance);\n"
            "else if (instance.exports.main) code = Number(instance.exports.main());\n"
            "else throw new Error('module exports neither _start nor main');\n"
            "fs.closeSync(stdout);\n"
            "fs.closeSync(stderr);\n"
            "console.log(JSON.stringify({exit: Number(code)}));\n"
        )
        wasm_exec = run([str(args.node), '--no-warnings', str(runner), str(wasm), str(wasm_stdout), str(wasm_stderr)])
        if wasm_exec.returncode != 0:
            fail('WASM/WASI execution', wasm_exec)
        try:
            wasm_exit = int(json.loads(wasm_exec.stdout.strip().splitlines()[-1])['exit'])
        except Exception as exc:
            print(wasm_exec.stdout, end='')
            print(wasm_exec.stderr, end='')
            raise SystemExit(f'backend-differential-wasm: invalid WASI runner result: {exc}')

        wasm_stdout_text = wasm_stdout.read_text(errors='replace')
        wasm_stderr_text = wasm_stderr.read_text(errors='replace')
        values = {'Forge': forge_exec.returncode, 'LLVM': llvm_exec.returncode, 'WASM': wasm_exit}
        mismatches: list[str] = []
        if len(set(values.values())) != 1:
            mismatches.append('exit ' + ' '.join(f'{k}={v}' for k, v in values.items()))
        if forge_exec.stdout != llvm_exec.stdout or forge_exec.stdout != wasm_stdout_text:
            mismatches.append('stdout differs')
        if forge_exec.stderr != llvm_exec.stderr or forge_exec.stderr != wasm_stderr_text:
            mismatches.append('stderr differs')
        if mismatches:
            print('backend-differential-wasm: FAIL ' + '; '.join(mismatches))
            if 'stdout differs' in mismatches:
                print('--- Forge stdout ---'); print(forge_exec.stdout, end='')
                print('--- LLVM stdout ---'); print(llvm_exec.stdout, end='')
                print('--- WASM stdout ---'); print(wasm_stdout_text, end='')
            if 'stderr differs' in mismatches:
                print('--- Forge stderr ---'); print(forge_exec.stderr, end='')
                print('--- LLVM stderr ---'); print(llvm_exec.stderr, end='')
                print('--- WASM stderr ---'); print(wasm_stderr_text, end='')
            return 1
        print(f'backend-differential-wasm: PASS exit={wasm_exit} stdout={len(wasm_stdout_text)}B stderr={len(wasm_stderr_text)}B')
        return 0


if __name__ == '__main__':
    raise SystemExit(main())
