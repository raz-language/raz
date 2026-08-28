#!/usr/bin/env python3
# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0

from pathlib import Path

root = Path(__file__).resolve().parents[2]
project = (root / "compiler/src/raz_driver/src/project.rz").read_text(encoding="utf-8")
host = (root / "src/bootstrap/compiler/project/project.hpp").read_text(encoding="utf-8")

expected = "src/main.rz"
if f'string default_entry = "{expected}";' not in project:
    raise SystemExit("production manifest parser must visibly default entry to src/main.rz")
if f'entry = "{expected}"' not in host:
    raise SystemExit("Stage-0 manifest parser must default entry to src/main.rz")
print("manifest default entry: src/main.rz")
