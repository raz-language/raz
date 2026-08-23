#!/usr/bin/env python3
# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0

from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
release = ROOT / 'tests/python/release-gate.py'
native = ROOT / 'tests/python/test-backend-differential.py'
wasm = ROOT / 'tests/python/test-backend-differential-wasm.py'
full = ROOT / 'tests/python/test-backend-full-corpus.py'
rxe = ROOT / 'tests/python/test-backend-differential-rxe-suite.py'
for path in (release, native, wasm, full, rxe):
    assert path.is_file(), f'missing Phase-1 qualification file: {path.relative_to(ROOT)}'

release_text = release.read_text()
for token in [
    'check-source.py',
    'test-backend-differential-suite.py',
    'test-backend-differential-wasm-suite.py',
    'test-backend-differential-rxe-suite.py',
    'test-backend-full-corpus.py',
    'test-stdlib-execution.py',
    "choices=['source', 'runtime', 'full']",
]:
    assert token in release_text, f'release gate missing {token}'

native_text = native.read_text()
assert 'forge_exec.stdout != llvm_exec.stdout' in native_text
assert 'forge_exec.stderr != llvm_exec.stderr' in native_text
wasm_text = wasm.read_text()
assert 'wasm_stdout_text' in wasm_text and 'wasm_stderr_text' in wasm_text
assert 'forge_exec.stdout != llvm_exec.stdout or forge_exec.stdout != wasm_stdout_text' in wasm_text
full_text = full.read_text()
assert 'razc' in full_text and "'check'" in full_text and 'MAIN_RE' in full_text
rxe_text = rxe.read_text()
assert 'check-rxe-roundtrip.py' in rxe_text and 'check-rxe-compatibility.py' in rxe_text
print('release-gate-source: PASS (canonical tiers + full observable backend parity contracts)')
