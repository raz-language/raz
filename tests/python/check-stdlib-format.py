#!/usr/bin/env python3
# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0

from pathlib import Path
import importlib.util
import sys

ROOT = Path(__file__).resolve().parents[2]
FORMATTER = ROOT / "tools" / "format-raz.py"
STDLIB = ROOT / "library" / "std"

spec = importlib.util.spec_from_file_location("raz_formatter", FORMATTER)
formatter = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = formatter
assert spec.loader is not None
spec.loader.exec_module(formatter)

files = sorted(STDLIB.rglob("*.rz"))
if not files:
    raise SystemExit("FAIL: no standard-library Raz sources found")

bad = []
for path in files:
    source = path.read_text(encoding="utf-8")
    formatted = formatter.format_text(source)
    if source != formatted:
        bad.append(str(path.relative_to(ROOT)))

if bad:
    for path in bad:
        print(f"FAIL: standard-library source needs formatting: {path}")
    raise SystemExit(1)

# Guard the canonical module-header shape explicitly: exactly one blank line
# after the SPDX header and namespace, with no runs of blank lines anywhere.
crc = (STDLIB / "encoding" / "checksum" / "crc32.rz").read_text(encoding="utf-8")
if "SPDX-License-Identifier: Apache-2.0\n\nnamespace std::encoding::checksum;\n\n// CRC" not in crc:
    raise SystemExit("FAIL: canonical stdlib module-header spacing regressed")
if "\n\n\n" in crc:
    raise SystemExit("FAIL: standard-library source contains repeated blank lines")
if ";\n/// Begins an incremental" in crc:
    raise SystemExit("FAIL: top-level doc block is not separated from previous declaration")

print(f"standard-library formatting: PASS ({len(files)} files)")
