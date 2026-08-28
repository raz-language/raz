#!/usr/bin/env python3
# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0

from pathlib import Path
ROOT = Path(__file__).resolve().parents[2]
text = (ROOT / 'tools' / 'bootstrap.py').read_text(encoding='utf-8')
checks = {
    'canonical modular compiler is named explicitly': 'qualified_compiler = compiler_package_layout / "target" / "release" / "bin" / f"raz-compiler{EXE}"' in text,
    'canonical modular compiler existence is enforced': 'Canonical modular compiler is missing' in text,
    'canonical modular compiler is version-validated': 'Validate canonical modular compiler' in text,
    'web qualification uses canonical modular compiler': 'str(qualified_compiler),\n                "--work-root"' in text,
    'bootstrap generation remains separate self-host proof': 'Validate Raz self-host compiler' in text and 'str(self_host_compiler), "--version"' in text,
}
failed=[name for name,ok in checks.items() if not ok]
if failed:
    for name in failed: print(f'bootstrap-modular-web-qualification: FAIL: {name}')
    raise SystemExit(1)
print(f'bootstrap-modular-web-qualification: PASS ({len(checks)} checks)')
