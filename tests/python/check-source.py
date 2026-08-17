#!/usr/bin/env python3
# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0

from __future__ import annotations

import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]

CHECKS = (
    "check-layout.py",
    "check-repository-hygiene.py",
    "check-doc-links.py",
    "check-test-layout.py",
    "check-raz-formatter-layout.py",
    "check-license-headers.py",
    "check-raz-rebrand.py",
    "check-compiler-source-set.py",
    "check-compiler-semantic-modules.py",
    "check-host-compiler-contract.py",
    "check-native-boundary.py",
    "check-compiler-runtime-declarations.py",
    "check-forge-backend-integrity.py",
    "check-mir-dce-storage.py",
    "check-mir-remap.py",
    "check-mir-transform-storage.py",
    "check-mir-copy-propagation.py",
    "check-toolchain-completion.py",
    "check-cli-package-status.py",
    "check-qualified-types-and-diagnostics.py",
    "check-stdlib-format.py",
    "check-forge-package.py",
    "check-llvm-backend-parity.py",
    "check-llvm-production-backend.py",
    "check-rxe-compatibility.py",
    "check-wasm-abi-compatibility.py",
    "check-registry-windows-tls.py",
    "check-registry-add-lock.py",
    "check-package-semantics.py",
    "check-official-registry.py",
    "check-razup.py",
)

def main() -> int:
    for name in CHECKS:
        print(f"[source] {name}", flush=True)
        result = subprocess.run([sys.executable, str(ROOT / "tests" / "python" / name)], cwd=ROOT)
        if result.returncode != 0:
            return result.returncode
    print(f"source qualification: PASS ({len(CHECKS)} checks)")
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
