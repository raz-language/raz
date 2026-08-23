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
    compiler = project / 'target' / 'debug' / ('raz-compiler.exe' if os.name == 'nt' else 'raz-compiler')
    if not compiler.is_file():
        raise RuntimeError(f'missing production compiler: {compiler}')

    general = run([str(compiler), '--help'], cwd=work, env=env).stdout
    require(general, 'Commands:', 'build', 'forge', 'llvm', '--backend=forge', '--backend=llvm', '--llvm', '--forge')
    llvm_help = run([str(compiler), 'llvm', '--help'], cwd=work, env=env).stdout
    require(llvm_help, '--emit=llvm', '--emit=obj', '--emit=exe', '--target=<triple>', '--cpu=<name>', '--features=<a,b,...>', '--linker=<driver>')
    backends = run([str(compiler), 'backends'], cwd=work, env=env).stdout
    require(backends, 'forge', 'llvm', 'built-in')
    targets = run([str(compiler), 'targets'], cwd=work, env=env).stdout
    require(targets, 'Forge native:', 'LLVM native:', 'x86_64-pc-windows-msvc', 'aarch64-unknown-linux-gnu', 'arm64-apple-macos')
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

    clang = shutil.which('clang')
    if clang:
        linux_arm = work / 'cli-smoke-aarch64-linux.o'
        run([str(compiler), 'llvm', '--emit=obj', '--target=aarch64-unknown-linux-gnu', str(source), str(linux_arm)], cwd=work, env=env)
        linux_bytes = linux_arm.read_bytes()
        if len(linux_bytes) < 20 or linux_bytes[:4] != b'\x7fELF' or int.from_bytes(linux_bytes[18:20], 'little') != 183:
            raise RuntimeError('LLVM AArch64/Linux output is not an ELF64 AArch64 object')

        mac_arm = work / 'cli-smoke-arm64-macos.o'
        run([str(compiler), 'llvm', '--emit=obj', '--target=arm64-apple-macos', str(source), str(mac_arm)], cwd=work, env=env)
        mac_bytes = mac_arm.read_bytes()
        if len(mac_bytes) < 8 or mac_bytes[:4] != b'\xcf\xfa\xed\xfe' or int.from_bytes(mac_bytes[4:8], 'little') != 0x0100000C:
            raise RuntimeError('LLVM macOS arm64 output is not a Mach-O arm64 object')

        cross_exe = work / 'cli-smoke-aarch64-linux'
        rejected = run(
            [str(compiler), 'llvm', '--emit=exe', '--target=aarch64-unknown-linux-gnu', str(source), str(cross_exe)],
            cwd=work,
            env=env,
            expect=2,
        )
        require(rejected.stderr, 'cross-target LLVM executable emission requires --runtime=<target-runtime>')
    else:
        print('CLI capabilities: clang unavailable; cross-object execution checks skipped')
    print('CLI capabilities: PASS')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
