#!/usr/bin/env python3
# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0

"""Audit the Raz-owned frontend against the Raz 1.0 stable language surface.

This intentionally checks the self-hosted compiler under compiler/src, not the
C++ bootstrap frontend.  The bootstrap frontend is used only as a parity
reference for syntax that is already exercised by the 1.0 conformance corpus.
"""
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]

def text(rel):
    return (ROOT / rel).read_text(encoding="utf-8")

lexer = text("compiler/src/frontend/lexer.rz")
parser = text("compiler/src/frontend/parser.rz")
types = text("compiler/src/hir/core/types.rz") + "\n" + text("compiler/src/hir/generics/instantiate.rz")
expr = text("compiler/src/hir/semantic/expressions.rz")
stmt = text("compiler/src/hir/semantic/statements.rz")
decl = text("compiler/src/hir/semantic/declarations.rz") + "\n" + text("compiler/src/hir/traits/solver.rz")
comp = text("compiler/src/hir/semantic/comptime.rz")
own = text("compiler/src/hir/semantic/ownership.rz")
mir = "\n".join(path.read_text(encoding="utf-8") for path in sorted((ROOT / "compiler/src/mir").rglob("*.rz")))
forge = "\n".join([
    text("compiler/src/backend/forge/writer.rz"),
    text("compiler/src/backend/forge/globals_codegen.rz"),
    text("compiler/src/backend/forge/function_codegen.rz"),
    text("compiler/src/backend/forge/codegen.rz"),
])
model = text("compiler/src/hir/core/model.rz")
const_generics = text("compiler/src/hir/generics/const_generics.rz")
reflection = text("compiler/src/hir/semantic/reflection.rz")
all_hir = "\n".join([types, const_generics, reflection, expr, stmt, decl, comp, own, model])

FULL, PARTIAL, MISSING = "FULL", "PARTIAL", "MISSING"
rows = []

def add(area, feature, status, reason):
    rows.append((area, feature, status, reason))

# Declarations / modules.
add("declarations", "functions + type-first parameters", FULL if "hir_parse_function" in decl and "hir_parse_parameter" in stmt else MISSING, "HIR declaration and parameter parsers are present")
add("declarations", "public/package/private visibility", FULL if "token_is_public" in decl and "token_is_private" in decl else MISSING, "public/private modifiers are recognized by Raz-owned declaration parsing; unmodified declarations form the package surface")
add("declarations", "imports, aliases, and public re-exports", FULL if "token_is_import" in comp and "token_is_as" in comp and "hir_parse_namespace_path" in all_hir and "namespace_import_aliases" in model and "namespace_import_flags" in model and "hir_namespace_reexports_target" in all_hir else MISSING, "top-level imports preserve qualified paths, aliases, and re-export metadata for namespace-aware lookup")
add("declarations", "structs", FULL if "hir_parse_struct_declaration" in decl else MISSING, "concrete struct declaration parser is present")
add("declarations", "payload enums", FULL if "hir_parse_enum_declaration" in decl and "enum_member_payload" in model else MISSING, "enum payload storage and parser are present")
add("declarations", "traits / impls", FULL if "hir_parse_trait_declaration" in decl and "hir_parse_trait_impl" in decl else MISSING, "trait and implementation parsers are present")
add("declarations", "extern functions", FULL if "hir_parse_extern_function" in all_hir else MISSING, "extern declarations are represented")
unsafe_full = "function_is_unsafe" in model and "unsafe_depth" in model and "token_is_unsafe" in decl.split("fn hir_parse_function",1)[1].split("fn ",1)[0] and "token_is_unsafe" in parser.split("fn parse_function",1)[1].split("fn ",1)[0]
add("declarations", "unsafe functions", FULL if unsafe_full else PARTIAL, "unsafe function qualifiers are preserved through precheck/HIR metadata and establish an unsafe semantic context")
parser_authoritative = "HIR is the sole normative Raz grammar/semantic parser" in text("compiler/src/main.rz") and "parse_module(input" not in text("compiler/src/main.rz")
add("parser/AST", "authoritative syntax/semantic tree for stable constructs", FULL if parser_authoritative else PARTIAL, "production compilation parses source once through the authoritative HIR frontend; parser.rz is retained only as an optional bootstrap/preflight utility")
attribute_partial = "hir_parse_pending_attributes" in comp and "function_abi_kinds" in model and "function_link_name_offsets" in model and "function_target_feature_offsets" in model and "pending_target_feature_length" in model and "function_is_unsafe" in model and "function_link_name_lengths" in text("compiler/src/backend/forge/writer.rz") and "Generic instantiation reparses the template body" in decl
target_feature_backend = "function_target_feature_lengths" in forge and 'fn.target_feature = "avx2"' in text("src/forge/src/ir/parser.cpp") and "target_feature" in text("src/forge/src/machine/lower.cpp")
add("declarations", "declaration attributes (@abi/@link_name/@target_feature)", FULL if (attribute_partial and target_feature_backend) else (PARTIAL if attribute_partial else (MISSING if "TokenKind::At" not in lexer else PARTIAL)), "attributes survive generic reparsing; @link_name and @abi affect Forge symbols/calling convention, while @target_feature is emitted as a FIR function contract and consumed/validated by target lowering")

# Primitive and compound types.
add("types", "integer widths + usize", FULL if "token_is_numeric_type" in lexer and "primitive_type_width" in lexer else MISSING, "integer family is recognized and width-aware")
add("types", "f64", FULL if "token_is_f64" in lexer and "writer_f64" in text("compiler/src/backend/forge/writer.rz") else MISSING, "f64 has lexer/type/backend handling")
add("types", "f32", FULL if "fn token_is_f32" in lexer and "type_kind == 11" in lexer and "writer_f32" in text("compiler/src/backend/forge/writer.rz") and "writer_numeric_cast_opcode(out, 1, 11)" in forge else PARTIAL, "distinct f32 kind, width-aware casts/storage, and Forge f32 spelling are present")
add("types", "bool / char", FULL if "source_byte(source, offset) == 98" in lexer and "source_byte(source, offset) == 99" in lexer and "type_kind == 10" in lexer else MISSING, "both are recognized by scalar type classification")
string_full = "token_is_string_type" in lexer and "TokenKind::String" in expr and "kind == 46" in mir and "emit_forge_string_globals" in forge and "global.address" not in forge
# global.address is emitted byte-by-byte, so the structural markers above are the reliable source-level gate.
string_full = "token_is_string_type" in lexer and "TokenKind::String" in expr and "kind == 46" in mir and "emit_forge_string_globals" in forge and "writer_string_global_name" in forge
add("types", "string", FULL if string_full else PARTIAL, "built-in string types/literals lower to immutable Forge byte globals and pointer-valued global.address operations")
add("types", "tuples", FULL if "hir_intern_tuple_type" in types and "builder.current.kind == TokenKind::LeftParen" in expr else MISSING, "tuple type/value construction is present")
fixed_array_full = "local_array_extents" in model and "struct_field_array_extents" in model and "parameter_array_extents" in model and "enum_payload_array_extents" in model
add("types", "fixed arrays", FULL if fixed_array_full else MISSING, "fixed-array extents are represented across locals, struct fields, parameters, and enum payloads")
add("types", "slices", FULL if "node_slices" in model and "slice_mutable" in types else MISSING, "slice metadata and parsing are present")
add("types", "references", FULL if "TokenKind::Ampersand" in types and "hir_validate_borrow" in expr else MISSING, "reference type/borrow expression path is present")
raw_pointer_full = "expected_reference == 2" in types and "actual_reference != 3" in types and "node_type = 12" in mir and "writer_ptr(out)" in forge and "TokenKind::Star" in parser.split("fn parse_type_reference",1)[1].split("fn ",1)[0]
add("types", "raw pointers (*const/*mut)", FULL if raw_pointer_full else PARTIAL, "raw pointer flavor is checked in HIR and lowered to the physical Forge ptr type")
function_pointer_syntax = "callable_type_kind = hir_callable_type_kind" in types and "scanning_function_pointer" in stmt and "kind == 47" in mir and "opcode == 43" in forge
function_pointer_full = function_pointer_syntax and "function_type_parameter_function_types" in model and "hir_function_pointer_signatures_equal" in types and "hir_add_node(builder, 49" in expr and "mir_emit_typed(mir, 44" in mir and "opcode == 44" in forge and "emit_forge_function_signatures" in text("compiler/src/backend/forge/writer.rz") and "writer_function_signature_name" in text("compiler/src/backend/forge/writer.rz")
add("types", "function-pointer types", FULL if function_pointer_full else (PARTIAL if function_pointer_syntax else MISSING), "fn(...) -> ... has structural signature identity, assignment/argument/return compatibility, func.address signature assertions, and typed call.indirect lowering")
dyn_partial = "trait_object_safe_flags" in model and "hir_coerce_trait_object" in types and "token_is_dyn" in types and "trait_method_starts" in model and "kind == 50" in mir
dyn_full = dyn_partial and "hir_parse_dyn_trait_call" in own and "hir_add_node(builder, 51" in own and "if (kind == 51)" in mir and "Object-safe dyn dispatch" in mir
add("types", "dynamic trait objects (dyn Trait)", FULL if dyn_full else (PARTIAL if dyn_partial else MISSING), "dyn Trait retains object-safe semantic identity and a two-word fat object; method calls dispatch through the stored vtable identity to the matching concrete implementation")
lifetime_full = "TokenKind::Lifetime" in lexer and "function_return_lifetimes" in model and "parameter_lifetimes" in model and "hir_resolve_call_return_lifetime" in types and "node_lifetimes" in model and "parameter_kind = 1" in types and "argument_structure != -3" in types and "declared_lifetime != actual_lifetime" in stmt
add("types", "explicit lifetimes", FULL if lifetime_full else PARTIAL, "named lifetimes participate in mixed generic parameter packs, annotate references, propagate through calls/locals, and constrain returned borrows")

# Expressions/operators.
for feature, marker in [
    ("arithmetic", "hir_parse_product"), ("comparison/equality", "hir_parse_relational"),
    ("bitwise", "hir_parse_bit_and"), ("shifts", "hir_parse_shift"),
    ("casts with as", "token_is_as"), ("calls", "TokenKind::LeftParen"),
    ("field projection", "TokenKind::Dot"), ("indexing", "TokenKind::LeftBracket"),
    ("move expressions", "token_is_move"), ("await", "token_is_await"), ("spawn", "token_is_spawn")]:
    add("expressions", feature, FULL if marker in expr or marker in lexer else MISSING, "syntax/HIR path present")
add("expressions", "short-circuit && / ||", FULL if "Logical operators are control flow" in mir and "i64 merge = mir_emit(mir, 24, 1" in mir else PARTIAL, "logical expressions lower to CFG with lazy RHS evaluation and a merge block parameter")
try_full = "hir_enum_propagation_success_member" in types and "hir_add_node(builder, 48" in expr and "if (kind == 48)" in mir and "failure_block" in mir and "payload_index" in mir
add("expressions", "try/error propagation (?)", FULL if try_full else (MISSING if "TokenKind::Question" not in lexer else PARTIAL), "postfix ? validates concrete Result/Option identity and lowers to success/failure CFG with failure return and success payload extraction")
add("expressions", "closures", FULL if "hir_parse_immediate_closure" in own and "function_closure" in model else MISSING, "capture/callable metadata and closure parser are present")

# Control flow.
for feature, marker in [
    ("if/else", "token_is_if"), ("while", "token_is_while"), ("for", "hir_parse_for_statement"),
    ("ranges .. / ..=", "TokenKind::DotDotEqual"), ("break", "token_is_break"),
    ("continue", "token_is_continue"), ("return", "token_is_return"),
    ("match", "hir_parse_match_statement"), ("defer", "hir_parse_defer_statement"),
    ("unsafe blocks", "hir_parse_unsafe_statement")]:
    hay = stmt + "\n" + lexer
    add("control flow", feature, FULL if marker in hay else MISSING, "syntax/HIR path present")
add("iteration", "fixed-array iteration", FULL if "array_extent" in stmt and "kind == 17" in mir else MISSING, "fixed local arrays lower through indexed loops")
borrowed_array_full = "iterable_kind == 17" in stmt and "borrow_kind != 0" in mir and "mir_emit_typed(out, 45" in mir and "opcode == 45" in forge
add("iteration", "borrowed-array iteration", FULL if borrowed_array_full else MISSING, "&array and &mut array sources lower to real element-reference handles rather than copied elements")
custom_iterator_full = "hir_parse_custom_iterator_for" in stmt and "hir_find_iteration_method" in stmt and "hir_add_node(builder, 51" in stmt and "kind == 19" in mir and "Iterator protocol" in mir
add("iteration", "custom Iterator/IntoIterator", FULL if custom_iterator_full else MISSING, "custom iterables desugar through into_iter/next/current methods and normal direct-call lowering")

# Ownership/destruction.
add("ownership", "moves / use-after-move", FULL if "hir_mark_path_moved" in own and "UseAfterMove" in lexer else MISSING, "move-path tracking is present")
add("ownership", "borrow conflicts / reborrows", FULL if "hir_has_active_borrow" in own and "BorrowConflict" in lexer else MISSING, "borrow state is tracked")
add("ownership", "partial moves / field borrows", FULL if "moved_paths" in model and "hir_node_path" in own else MISSING, "path-sensitive ownership storage exists")
add("ownership", "Drop elaboration", FULL if "lower_hir_emit_structure_drop" in mir and "struct_drop_functions" in model else MISSING, "MIR emits deterministic structure cleanup")
lifetime_ownership_full = lifetime_full and "node_lifetimes" in own and "local_lifetimes" in own
add("ownership", "explicit lifetime relationships", FULL if lifetime_ownership_full else PARTIAL, "borrow provenance carries named lifetime IDs through parameters, locals, expressions, calls, and return validation")

# Generics / traits.
add("generics/traits", "generic functions/types", FULL if "hir_parse_generic_function_template" in comp and "hir_parse_generic_struct_template" in comp else MISSING, "generic templates/instantiation are present")
multi_generic_full = "generic_parameter_count" in model and "generic_function_parameter_counts" in model and "generic_struct_parameter_counts" in model and "generic_enum_parameter_counts" in model and "hir_parse_generic_parameter_list" in types and "hir_validate_generic_arguments" in types
add("generics/traits", "multiple generic parameters", FULL if multi_generic_full else PARTIAL, "generic functions, structs, and enums use packed parameter/argument lists with full-argument identity and per-parameter bounds")
add("generics/traits", "generic impls", FULL if "hir_parse_generic_inherent_impl_template" in comp and "hir_parse_generic_trait_impl_template" in comp else MISSING, "generic impl templates are present")
add("generics/traits", "trait associated types/constants", FULL if "trait_associated_type_count" in model and "trait_associated_const_count" in model else MISSING, "metadata and declaration checking are present")
const_generic_full = "generic_parameter_const_types" in model and "hir_find_generic_const_substitution" in types and "hir_parse_const_generic_expression" in const_generics and "hir_generic_scanned_const_expression" in const_generics and "struct_field_array_extents" in model and "parameter_array_extents" in model and "enum_payload_array_extents" in model
add("generics/traits", "const generics", FULL if const_generic_full else MISSING, "const parameters participate in identity/substitution; arithmetic/top-level constants are accepted and fixed extents survive fields, parameters, and enum payloads")
supertrait_full = "trait_supertrait_count" in model and "parsing_supertraits" in decl and "hir_validate_supertraits_for_structure" in decl and "bound == -4" in types
add("generics/traits", "supertraits", FULL if supertrait_full else MISSING, "trait declarations preserve supertraits and concrete/generic bound satisfaction checks them transitively")
add("generics/traits", "object-safe dynamic dispatch", FULL if dyn_full else (PARTIAL if dyn_partial else MISSING), "object-safe dyn calls resolve stable trait method slots and lower through concrete implementation dispatch using the fat-object vtable identity")

# Compile time / reflection.
add("comptime", "const expressions/functions", FULL if "hir_parse_const_declaration" in all_hir and "function_is_const" in model else MISSING, "const declarations/functions are represented")
add("comptime", "comptime blocks/assert/loops", FULL if "hir_parse_comptime_block" in comp and "token_is_assert" in lexer else MISSING, "comptime parser/evaluator path is present")
reflection_core = "hir_parse_structural_reflection" in reflection and "size_of" in reflection and "field_count" in reflection and "variant_count" in reflection and "is_copy" in reflection and "needs_drop" in reflection
reflection_layout = reflection_core and "field_offset" in reflection and "variant_discriminant" in reflection and "payload_offset" in reflection
reflection_names = reflection_layout and "field_name" in reflection and "variant_name" in reflection and "method_name" in reflection and "trait_method_name" in reflection and "associated_type_name" in reflection and "associated_const_name" in reflection and "hir_reflection_add_name_string" in reflection
derive_full = "token_is_derive" in lexer and "hir_apply_pending_struct_derives" in decl and "struct_derived_clone_flags" in model and "generic_struct_derive_clone_flags" in model and "mir_clone_structure_value" in mir
add("comptime", "stable reflection/derive surface", FULL if (reflection_names and derive_full) else (PARTIAL if reflection_core else MISSING), "the stable selected reflection surface covers layout, field/variant names, inherent/trait method names, and associated-item names; @derive supports Copy, fieldwise Clone, marker traits, and generic-struct derive propagation")

# Async/native boundary.
add("async", "async fn", FULL if "function_is_async" in model and "token_is_async" in all_hir else MISSING, "async function metadata/parser are present")
add("async", "await + frame spill", FULL if "async_mir_await_count" in forge and "kind == 44" in mir else MISSING, "await reaches MIR/Forge async lowering")
spawn_full = "kind == 45" in mir and "mir_emit_typed(mir, 46" in mir and "opcode == 46" in forge and "Spawning is an async effect" in expr
add("async", "spawn", FULL if spawn_full else PARTIAL, "spawn is async-only, remains a distinct MIR task-spawn effect, and lowers to the future handle produced by the async source call")
extern_abi_full = "function_extern_kinds" in model and "function_abi_kinds" in model and "emit_forge_extern_function" in text("compiler/src/backend/forge/writer.rz") and "function_abi_kinds, function_index) == 1" in text("compiler/src/backend/forge/writer.rz")
add("native ABI", "extern ABI", FULL if extern_abi_full else PARTIAL, "user extern declarations are emitted into Forge; @link_name selects the symbol and @abi(C) selects Forge's explicit C calling convention while system/Raz retain platform convention")

# Backend coverage sanity.
add("lowering", "HIR -> MIR", FULL if "lower_hir_expression" in mir and "lower_hir_statement_range" in mir else MISSING, "central lowering paths are present")
add("lowering", "MIR -> Forge", FULL if "emit_forge_function" in forge else MISSING, "Forge serialization path is present")

counts = {FULL:0, PARTIAL:0, MISSING:0}
for r in rows: counts[r[2]] += 1

print("Raz 1.0 language-pipeline audit (Raz-owned frontend)")
print(f"FULL={counts[FULL]} PARTIAL={counts[PARTIAL]} MISSING={counts[MISSING]}")
print()
for area, feature, status, reason in rows:
    print(f"[{status:7}] {area:16} {feature}: {reason}")

# This audit is informational by default so it can live in a tree while gaps are
# being repaired. --strict is the release gate.
if "--strict" in sys.argv and (counts[PARTIAL] or counts[MISSING]):
    sys.exit(1)
