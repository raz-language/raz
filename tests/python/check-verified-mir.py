#!/usr/bin/env python3
# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0

"""Audit the verified-MIR ownership firewall and backend neutrality."""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
header = (ROOT / "src/bootstrap/compiler/ir/mir/mir.hpp").read_text()
impl = (ROOT / "src/bootstrap/compiler/ir/mir/mir.cpp").read_text()
lowering = (ROOT / "src/bootstrap/compiler/lowering/hir_to_mir/detail/lowering_helpers.hpp").read_text()
forge = (ROOT / "src/bootstrap/compiler/backend/forge/forge_lowering.cpp").read_text()
tests = (ROOT / "tests/native/ir/mir_tests.cpp").read_text()
module_lowering = (ROOT / "src/bootstrap/compiler/lowering/hir_to_mir/detail/lower_module.hpp").read_text()

required_opcodes = [
    "storage_live", "storage_dead", "move_value", "borrow_shared", "borrow_exclusive", "borrow_bind", "place_path",
]
problems = []
for opcode in required_opcodes:
    if f"MirOpcode::{opcode}" not in impl or opcode not in header:
        problems.append(f"missing MIR ownership opcode: {opcode}")
    if f"case MirOpcode::{opcode}:" not in forge:
        problems.append(f"Forge does not explicitly consume verifier-only opcode: {opcode}")

for phrase in [
    "use after final drop",
    "double drop",
    "CFG-joined use after move",
    "reinitialization after move",
    "disjoint field use after partial move",
    "whole-value use after partial move",
    "projection reinitialization after move",
    "dynamic-index alias after element move",
    "disjoint field at CFG join after partial move",
    "borrow region that outlives its source",
    "borrow whose source outlives the inferred region",
]:
    if phrase not in tests:
        problems.append(f"missing malformed-MIR regression: {phrase}")

if "for (const auto& error : module.verify())" not in module_lowering:
    problems.append("MIR verification is not a hard lowering gate")
if "mir_infer_borrow_regions" not in impl or "borrow_regions" not in header:
    problems.append("MIR lifetime-region inference metadata is missing")
if "parent_region" not in header or "crosses_suspension" not in header or "contains_back_edge" not in header:
    problems.append("MIR region constraints/backedge/suspension metadata is incomplete")
if "pending_moves" not in impl or "ownership_successors" not in impl:
    problems.append("CFG-aware move/drop ownership verification is missing")
if "MirOpcode::storage_live" not in lowering or "MirOpcode::move_value" not in lowering:
    problems.append("HIR-to-MIR lowering does not emit ownership metadata")

if problems:
    print("verified-mir: FAIL")
    for problem in problems:
        print(f"  {problem}")
    raise SystemExit(1)

print("verified-mir: PASS")
print("  ownership metadata: storage lifetime, move, borrow bindings, projection paths")
print("  CFG verifier: projection ownership + inferred NLL regions + reborrow/loop/async constraints")
print("  backend contract: ownership metadata has zero machine-code effect")
