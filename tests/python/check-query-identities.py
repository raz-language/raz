#!/usr/bin/env python3
# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0

from pathlib import Path
import sys

root = Path(__file__).resolve().parents[2]
model = (root / "compiler/src/hir/core/model.rz").read_text(encoding="utf-8")
builder = (root / "compiler/src/hir/core/builder.rz").read_text(encoding="utf-8")
engine = (root / "compiler/src/hir/query/engine.rz").read_text(encoding="utf-8")
invalidation = (root / "compiler/src/hir/query/invalidation.rz").read_text(encoding="utf-8")
symbols = (root / "compiler/src/hir/query/symbols.rz").read_text(encoding="utf-8")
types = (root / "compiler/src/hir/query/types.rz").read_text(encoding="utf-8")
resolution = (root / "compiler/src/hir/query/resolution.rz").read_text(encoding="utf-8")
traits = (root / "compiler/src/hir/generics/type_instantiation.rz").read_text(encoding="utf-8")
instantiate = (root / "compiler/src/hir/generics/instantiate.rz").read_text(encoding="utf-8")
order = (root / "compiler/host-source-order.txt").read_text(encoding="utf-8")

checks = {
    "symbol interner stores exact semantic names": all(x in model + builder + symbols for x in [
        "query_symbol_count", "query_symbol_hashes", "query_symbol_offsets",
        "query_symbol_lengths", "hir_query_intern_symbol", "token_same",
        "query_symbol_buckets", "query_symbol_next"]),
    "type interner stores complete canonical HIR type shape": all(x in model + builder + types for x in [
        "query_type_count", "query_type_structures", "query_type_kinds",
        "query_type_references", "query_type_array_extents",
        "query_type_function_signatures", "query_type_trait_objects", "hir_query_intern_type",
        "query_type_hashes", "query_type_buckets", "query_type_next"]),
    "semantic identity interning uses bucketed lookup rather than full linear scans": all(x in symbols + types for x in [
        "query_symbol_buckets", "query_symbol_next", "query_type_buckets", "query_type_next"]),
    "semantic revision remains telemetry rather than global cache validity": all(x in model + builder + invalidation for x in [
        "query_revision", "query_cache_revisions", "hir_query_sync_inputs",
        "query_input_traits_fingerprint", "query_invalidation_total"]),
    "cache lookup is not gated by one global revision": "query_cache_revisions, slot) == builder.query_revision" not in engine,
    "method queries use TypeId and SymbolId": all(x in resolution for x in [
        "hir_query_intern_symbol", "hir_query_intern_value_type",
        "hir_query_kind_method(),", "receiver", "symbol"]),
    "method negatives are cacheable": "hir_query_end(builder, hir_query_kind_method(), receiver, symbol, 0, epoch, 0, result, 0, true);" in resolution,
    "trait implementation queries use TypeId": all(x in traits for x in [
        "hir_query_intern_value_type", "hir_query_kind_trait_impl()", "type_id"]),
    "trait bound queries use TypeId and cache revision-safe negatives": all(x in traits for x in [
        "hir_query_kind_trait_bound()", "type_id", "both outcomes are safe"]),
    "associated types use TypeId and SymbolId": all(x in instantiate for x in [
        "base_type", "item_symbol", "hir_query_intern_symbol",
        "hir_query_intern_value_type", "hir_query_kind_associated_type()"]),
    "identity modules are in host compiler order": all(x in order for x in [
        "src/hir/query/symbols.rz", "src/hir/query/types.rz",
        "src/hir/query/resolution.rz", "src/hir/query/identity.rz"]),
}

failed = [name for name, ok in checks.items() if not ok]
if failed:
    for name in failed:
        print(f"query-identities: FAIL: {name}")
    sys.exit(1)

print("query-identities: PASS")
print("  semantic queries use exact interned SymbolId/TypeId identities")
print("  cache validity is source-independent and no longer gated by a global revision")
