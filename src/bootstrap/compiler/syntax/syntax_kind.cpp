// Copyright 2026 Mario Vinciguerra
// SPDX-License-Identifier: Apache-2.0

#include "compiler/syntax/syntax_kind.hpp"

namespace raz::compiler {

std::string_view syntax_kind_name(SyntaxKind kind) noexcept {
#define RAZ_CASE(name) case SyntaxKind::name: return #name
  switch (kind) {
    RAZ_CASE(compilation_unit); RAZ_CASE(import_declaration);
    RAZ_CASE(namespace_declaration); RAZ_CASE(struct_declaration);
    RAZ_CASE(enum_declaration); RAZ_CASE(enum_variant); RAZ_CASE(trait_declaration);
    RAZ_CASE(associated_type_declaration); RAZ_CASE(associated_const_declaration);
    RAZ_CASE(impl_declaration); RAZ_CASE(function_declaration);
    RAZ_CASE(parameter); RAZ_CASE(field_declaration);
    RAZ_CASE(const_declaration); RAZ_CASE(variable_declaration); RAZ_CASE(block_statement);
    RAZ_CASE(empty_statement); RAZ_CASE(expression_statement);
    RAZ_CASE(return_statement); RAZ_CASE(if_statement);
    RAZ_CASE(while_statement); RAZ_CASE(for_statement);
    RAZ_CASE(break_statement); RAZ_CASE(continue_statement);
    RAZ_CASE(defer_statement); RAZ_CASE(unsafe_statement); RAZ_CASE(comptime_statement);
    RAZ_CASE(match_statement); RAZ_CASE(match_arm);
    RAZ_CASE(name_expression); RAZ_CASE(literal_expression);
    RAZ_CASE(array_expression); RAZ_CASE(tuple_expression);
    RAZ_CASE(struct_expression); RAZ_CASE(field_initializer);
    RAZ_CASE(unary_expression); RAZ_CASE(binary_expression); RAZ_CASE(cast_expression);
    RAZ_CASE(assignment_expression); RAZ_CASE(call_expression);
    RAZ_CASE(member_expression); RAZ_CASE(index_expression);
    RAZ_CASE(try_expression); RAZ_CASE(closure_expression); RAZ_CASE(parenthesized_expression); RAZ_CASE(error_node);
  }
#undef RAZ_CASE
  return "unknown";
}

}  // namespace raz::compiler
