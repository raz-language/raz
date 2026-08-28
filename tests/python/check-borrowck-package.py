#!/usr/bin/env python3
# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0
"""Verify the canonical borrow-checker package boundary and dependency direction."""
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[2]
SRC = ROOT / "compiler" / "src"
BORROW = SRC / "raz_borrowck"
MIR = SRC / "raz_mir"
MIR_OPT = SRC / "raz_mir_opt"
DRIVER = SRC / "raz_driver"

problems: list[str] = []
manifest = (BORROW / "raz.toml").read_text(encoding="utf-8") if (BORROW / "raz.toml").is_file() else ""
if 'name = "raz-borrowck"' not in manifest:
    problems.append("raz_borrowck package manifest is missing or misnamed")
if 'mir = "../raz_mir"' not in manifest:
    problems.append("raz_borrowck must depend on raz_mir")
for forbidden in ('hir = "../raz_hir"', 'frontend = "../raz_parser"'):
    if forbidden in manifest:
        problems.append(f"raz_borrowck has unnecessary semantic dependency: {forbidden}")

analysis_modules = (
    "borrows", "drops", "initialization", "loan_regions", "move_state", "moves",
    "partial_moves", "paths", "places", "reborrows", "semantics",
)
for module in analysis_modules:
    if not (BORROW / "src" / "borrowck" / f"{module}.rz").is_file():
        problems.append(f"missing borrowck analysis module: {module}")
    if (MIR / "src" / "mir" / "ownership" / f"{module}.rz").exists():
        problems.append(f"borrowck analysis leaked back into raz_mir: {module}")
for module in ("context", "lowering_paths"):
    if not (MIR / "src" / "mir" / "ownership" / f"{module}.rz").is_file():
        problems.append(f"MIR ownership representation/lowering module moved incorrectly: {module}")

mir_manifest = (MIR / "raz.toml").read_text(encoding="utf-8")
mir_source = "\n".join(p.read_text(encoding="utf-8") for p in (MIR / "src").rglob("*.rz"))
if "raz_borrowck" in mir_manifest or "borrowck::" in mir_source:
    problems.append("raz_mir depends back on raz_borrowck")

semantics = (BORROW / "src" / "borrowck" / "semantics.rz").read_text(encoding="utf-8")
if "public fn verify_mir_ownership_semantics" not in semantics:
    problems.append("borrowck does not expose its ownership legality entry point")

driver_manifest = (DRIVER / "raz.toml").read_text(encoding="utf-8")
driver_main = (DRIVER / "src" / "compiler_main.rz").read_text(encoding="utf-8")
if 'borrowck = "../raz_borrowck"' not in driver_manifest:
    problems.append("raz_driver does not depend on raz_borrowck")
if driver_main.count("verify_mir_ownership_semantics(&mir)") < 2:
    problems.append("driver must run borrowck before and after MIR optimization")

pipeline = (MIR_OPT / "src" / "mir_opt" / "transform" / "pipeline.rz").read_text(encoding="utf-8")
if "verify_mir_ownership_semantics" in pipeline:
    problems.append("MIR transform pipeline still owns borrow-checking semantics")

cfg = (MIR / "src" / "mir" / "analysis" / "cfg.rz").read_text(encoding="utf-8")
liveness = (MIR / "src" / "mir" / "analysis" / "liveness.rz").read_text(encoding="utf-8")
for api in ("public fn build_mir_cfg", "public fn destroy_mir_cfg", "public fn mir_cfg_block_for_instruction"):
    if api not in cfg:
        problems.append(f"MIR does not expose borrowck-facing CFG API: {api}")
if "public fn mir_register_operand_mask" not in liveness:
    problems.append("MIR does not expose its canonical register-operand classification")

if problems:
    print("borrowck-package: FAIL")
    for problem in problems:
        print("  " + problem)
    sys.exit(1)

print("borrowck-package: PASS")
print("  raz_mir owns ownership facts and exposes the minimal analysis view")
print("  raz_borrowck owns move/loan/reborrow/drop legality with no MIR reverse dependency")
print("  raz_driver runs ownership verification before and after MIR optimization")
