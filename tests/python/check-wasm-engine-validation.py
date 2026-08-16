#!/usr/bin/env python3
# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0

"""Require an independent WebAssembly engine to accept required features and reject malformed modules."""
from __future__ import annotations
import json
from pathlib import Path
import shutil
import subprocess
import tempfile


def section(kind: int, payload: bytes) -> bytes:
    assert len(payload) < 128
    return bytes([kind, len(payload)]) + payload


HEADER = b'\x00asm\x01\x00\x00\x00'
TYPE_VOID = section(1, b'\x01\x60\x00\x00')
FUNC_ONE = section(3, b'\x01\x00')

# Empty valid core module.
minimal = HEADER

# One memory, one function using memory.copy (bulk-memory proposal, standardized).
memory = section(5, b'\x01\x00\x01')
bulk_instr = b'\x41\x00\x41\x00\x41\x00\xfc\x0a\x00\x00\x0b'
bulk_body = b'\x00' + bulk_instr
bulk = HEADER + TYPE_VOID + FUNC_ONE + memory + section(10, b'\x01' + bytes([len(bulk_body)]) + bulk_body)

# One function using v128.const followed by drop.
simd_instr = b'\xfd\x0c' + bytes(16) + b'\x1a\x0b'
simd_body = b'\x00' + simd_instr
simd = HEADER + TYPE_VOID + FUNC_ONE + section(10, b'\x01' + bytes([len(simd_body)]) + simd_body)

cases = {
    'minimal': minimal,
    'bulk': bulk,
    'simd': simd,
    'bad_magic': b'BAD!' + HEADER[4:],
    'truncated': HEADER[:-1],
    'bad_section_length': HEADER + b'\x01\x7f\x00',
}
expected = {'minimal': True, 'bulk': True, 'simd': True, 'bad_magic': False, 'truncated': False, 'bad_section_length': False}

node = shutil.which('node')
if not node:
    raise SystemExit('wasm-engine-validation: FAIL: node not found')

with tempfile.TemporaryDirectory(prefix='raz-wasm-engine-') as td:
    td = Path(td)
    paths = {}
    for name, data in cases.items():
        p = td / f'{name}.wasm'
        p.write_bytes(data)
        paths[name] = str(p)
    js = td / 'validate.mjs'
    js.write_text(
        "import fs from 'node:fs';\n"
        "const files = JSON.parse(process.argv[2]);\n"
        "const out = {};\n"
        "for (const [name,path] of Object.entries(files)) out[name] = WebAssembly.validate(fs.readFileSync(path));\n"
        "console.log(JSON.stringify(out));\n"
    )
    proc = subprocess.run([node, str(js), json.dumps(paths)], text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    if proc.returncode:
        print(proc.stdout, end='')
        print(proc.stderr, end='')
        raise SystemExit(f'wasm-engine-validation: FAIL: node exited {proc.returncode}')
    actual = json.loads(proc.stdout.strip().splitlines()[-1])

failures = [name for name, want in expected.items() if bool(actual.get(name)) != want]
if failures:
    raise SystemExit('wasm-engine-validation: FAIL: ' + ', '.join(f'{name}={actual.get(name)} expected {expected[name]}' for name in failures))
print('wasm-engine-validation: PASS (core + bulk-memory + SIMD accepted; malformed modules rejected)')
