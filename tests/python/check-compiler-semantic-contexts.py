#!/usr/bin/env python3
# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0

from pathlib import Path
import re
import sys

root = Path(__file__).resolve().parents[2]
hir_model = (root / "compiler/src/raz_hir/src/hir/core/model.rz").read_text(encoding="utf-8")
hir_context = (root / "compiler/src/raz_query/src/query/context.rz").read_text(encoding="utf-8")
mir_model = (root / "compiler/src/raz_mir/src/mir/core/model.rz").read_text(encoding="utf-8")
mir_context = (root / "compiler/src/raz_mir/src/mir/ownership/context.rz").read_text(encoding="utf-8")
hir_src = "\n".join(p.read_text(encoding="utf-8") for p in (root / "compiler/src/raz_hir/src").rglob("*.rz"))
mir_src = "\n".join(p.read_text(encoding="utf-8") for p in (root / "compiler/src/raz_mir/src").rglob("*.rz"))
borrowck_src = "\n".join(p.read_text(encoding="utf-8") for p in (root / "compiler/src/raz_borrowck/src").rglob("*.rz"))

checks = {
    "raz_query owns the explicit query context used by HIR": "public struct HirQueryContext {" in hir_context and "HirQueryContext queries;" in hir_model,
    "query arenas are not flat HirBuilder fields": not re.search(r"^    i64 query_", hir_model.split("public struct HirBuilder {", 1)[1], re.MULTILINE),
    "query users address the owned context": "builder.queries.query_cache_hashes" in hir_src and "builder.query_cache_hashes" not in hir_src,
    "MIR owns one explicit ownership context": "public struct MirOwnershipContext {" in mir_context and "MirOwnershipContext ownership;" in mir_model,
    "ownership arenas are not flat MirModule fields": not re.search(r"^    i64 ownership_", mir_model.split("public struct MirModule {", 1)[1], re.MULTILINE),
    "MIR lowering/transforms address the owned context": "out.ownership.ownership_event_count" in mir_src and "mir.ownership.ownership_event_instructions" in mir_src,
    "borrowck consumes MIR ownership facts without owning MIR storage": "mir.ownership.ownership_event_count" in borrowck_src and "public fn verify_mir_ownership_semantics" in borrowck_src,
}

failed = [name for name, ok in checks.items() if not ok]
if failed:
    for name in failed:
        print(f"compiler-semantic-contexts: FAIL: {name}")
    sys.exit(1)

print("compiler-semantic-contexts: PASS")
print("  raz_query owns query state; MIR owns ownership facts; raz_borrowck owns legality analysis")
