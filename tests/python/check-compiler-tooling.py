#!/usr/bin/env python3
# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0

from __future__ import annotations
import argparse, os, shutil, subprocess
from pathlib import Path


def run(args: list[str], *, cwd: Path, env: dict[str, str], expect: int = 0) -> subprocess.CompletedProcess[str]:
    p = subprocess.run(args, cwd=cwd, env=env, text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    if p.returncode != expect:
        raise RuntimeError(f"command failed ({p.returncode}, expected {expect}): {' '.join(args)}\nstdout:\n{p.stdout}\nstderr:\n{p.stderr}")
    return p


def run_must_fail(args: list[str], *, cwd: Path, env: dict[str, str]) -> subprocess.CompletedProcess[str]:
    p = subprocess.run(args, cwd=cwd, env=env, text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    if p.returncode == 0:
        raise RuntimeError(f"command unexpectedly succeeded: {' '.join(args)}\nstdout:\n{p.stdout}\nstderr:\n{p.stderr}")
    return p


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

    help_text = run([str(compiler), '--help'], cwd=work, env=env).stdout

    trailing_commas = root / 'tests' / 'format' / 'multiline_trailing_commas.rz'
    run([str(compiler), '--check', str(trailing_commas)], cwd=work, env=env)

    nested_associated = root / 'tests' / 'traits' / 'nested_associated_normalization.rz'
    run([str(compiler), '--check', str(nested_associated)], cwd=work, env=env)

    soundness = root / 'tests' / 'soundness'
    for source in ('nll_unused_borrow.rz', 'nll_last_use.rz'):
        run([str(compiler), '--check', str(soundness / source)], cwd=work, env=env)
    for source in (
        'invalid_aggregate_reference_escape.rz',
        'invalid_aggregate_reference_escape_via_local.rz',
        'invalid_reference_rebind.rz',
        'invalid_slice_rebind.rz',
    ):
        run_must_fail([str(compiler), '--check', str(soundness / source)], cwd=work, env=env)

    for word in ('fmt', 'doc', 'test_ functions'):
        if word not in help_text:
            raise RuntimeError(f'missing {word!r} from CLI help')

    source = work / 'format.rz'
    source.write_text('fn main() -> i64 {\n        i64 x = 1;   \n if (x == 1) {\n return 0;\n }\n}\n', encoding='utf-8')
    run([str(compiler), 'fmt', '--check', str(source)], cwd=work, env=env, expect=1)
    run([str(compiler), 'fmt', str(source)], cwd=work, env=env)
    expected = 'fn main() -> i64 {\n    i64 x = 1;\n    if (x == 1) {\n        return 0;\n    }\n}\n'
    if source.read_text(encoding='utf-8') != expected:
        raise RuntimeError('formatter output did not match canonical indentation')
    run([str(compiler), 'fmt', '--check', str(source)], cwd=work, env=env)
    before = source.read_bytes()
    run([str(compiler), 'fmt', str(source)], cwd=work, env=env)
    if source.read_bytes() != before:
        raise RuntimeError('formatter is not idempotent')

    fmt_tree = work / 'fmt-tree'
    (fmt_tree / 'nested').mkdir(parents=True, exist_ok=True)
    tree_a = fmt_tree / 'a.rz'
    tree_b = fmt_tree / 'nested' / 'b.rz'
    ignored = fmt_tree / 'notes.txt'
    tree_a.write_text('fn a() -> i64 {\n return 1;   \n}\n', encoding='utf-8')
    tree_b.write_text('fn b() -> i64 {\n        return 2;\n}\n', encoding='utf-8')
    ignored.write_text('do not touch\n', encoding='utf-8')
    run([str(compiler), 'fmt', '--check', str(fmt_tree)], cwd=work, env=env, expect=1)
    run([str(compiler), 'fmt', str(fmt_tree)], cwd=work, env=env)
    run([str(compiler), 'fmt', '--check', str(fmt_tree)], cwd=work, env=env)
    if tree_a.read_text(encoding='utf-8') != 'fn a() -> i64 {\n    return 1;\n}\n':
        raise RuntimeError('directory formatter did not format root .rz file')
    if tree_b.read_text(encoding='utf-8') != 'fn b() -> i64 {\n    return 2;\n}\n':
        raise RuntimeError('directory formatter did not format nested .rz file')
    if ignored.read_text(encoding='utf-8') != 'do not touch\n':
        raise RuntimeError('directory formatter modified a non-Raz file')

    current_dir_source = fmt_tree / 'current.rz'
    current_dir_source.write_text('fn current() -> i64 {\n return 3;\n}\n', encoding='utf-8')
    run([str(compiler), 'fmt', '--check'], cwd=fmt_tree, env=env, expect=1)
    run([str(compiler), 'fmt'], cwd=fmt_tree, env=env)
    run([str(compiler), 'fmt', '--check'], cwd=fmt_tree, env=env)
    if current_dir_source.read_text(encoding='utf-8') != 'fn current() -> i64 {\n    return 3;\n}\n':
        raise RuntimeError('formatter without a path did not format the current directory')

    passing = work / 'tests-pass.rz'
    passing.write_text('fn test_zero() -> i64 { return 0; }\nfn test_math() -> i64 { if (2 + 2 == 4) { return 0; } return 1; }\nfn main() -> i64 { return 99; }\n', encoding='utf-8')
    run([str(compiler), 'test', str(passing)], cwd=work, env=env)
    failing = work / 'tests-fail.rz'
    failing.write_text('fn test_failure() -> i64 { return 7; }\nfn main() -> i64 { return 0; }\n', encoding='utf-8')
    run([str(compiler), 'test', str(failing)], cwd=work, env=env, expect=7)

    documented = work / 'docs.rz'
    documented.write_text('/// Adds two signed integers.\n/// Returns the sum.\npublic fn add(i64 a, i64 b) -> i64 { return a + b; }\n\n/// A tiny counter.\npublic struct Counter { i64 value; }\n', encoding='utf-8')
    docs = work / 'API.md'
    run([str(compiler), 'doc', str(documented), str(docs)], cwd=work, env=env)
    generated = docs.read_text(encoding='utf-8')
    for needle in ('# Raz API', 'Adds two signed integers.', 'public fn add', 'public struct Counter'):
        if needle not in generated:
            raise RuntimeError(f'missing generated documentation text: {needle!r}')

    print('compiler tooling: PASS')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
