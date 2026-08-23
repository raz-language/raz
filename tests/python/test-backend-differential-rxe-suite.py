#!/usr/bin/env python3
# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0

"""Release-level RXE semantic execution qualification.

RXE's canonical reference VM lives inside the Raz compiler and is deliberately
used as the semantic oracle against MIR. This gate runs the RXE backend,
round-trip decoder, hostile-module verifier, and deterministic MIR-vs-RXE
reference-execution contracts together so releases cannot qualify an RXE writer
that merely emits structurally valid but semantically divergent bytecode.
"""
from __future__ import annotations
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
CHECKS = [
    'check-rxe-backend.py',
    'check-rxe-roundtrip.py',
    'check-rxe-malformed.py',
    'check-rxe-compatibility.py',
]


def main() -> int:
    failures = 0
    for name in CHECKS:
        result = subprocess.run([sys.executable, str(ROOT / 'tests/python' / name)], cwd=ROOT, text=True)
        if result.returncode:
            failures += 1
            print(f'backend-differential-rxe-suite: FAIL {name}')
    if failures:
        return 1
    fixtures = sorted((ROOT / 'tests/examples/backends/rxe').glob('*.rz'))
    if len(fixtures) < 20:
        print(f'backend-differential-rxe-suite: FAIL expected >=20 RXE semantic fixtures, found {len(fixtures)}')
        return 1
    print(f'backend-differential-rxe-suite: PASS ({len(fixtures)} fixtures, MIR↔RXE reference semantics + binary validation)')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
