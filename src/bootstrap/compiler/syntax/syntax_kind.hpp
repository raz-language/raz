// Copyright 2026 Mario Vinciguerra
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdint>
#include <string_view>

namespace raz::compiler {

enum class SyntaxKind : std::uint16_t {
  compilation_unit,
  import_declaration,
  namespace_declaration,
  struct_declaration,
  enum_declaration,
  enum_variant,
  trait_declaration,
  associated_type_declaration,
  associated_const_declaration,
  impl_declaration,
  function_declaration,
  parameter,
  field_declaration,
  const_declaration,
  variable_declaration,
  block_statement,
  empty_statement,
  expression_statement,
  return_statement,
  if_statement,
  while_statement,
  for_statement,
  break_statement,
  continue_statement,
  defer_statement,
  unsafe_statement,
  comptime_statement,
  match_statement,
  match_arm,
  name_expression,
  literal_expression,
  array_expression,
  tuple_expression,
  struct_expression,
  field_initializer,
  unary_expression,
  binary_expression,
  cast_expression,
  assignment_expression,
  call_expression,
  member_expression,
  index_expression,
  try_expression,
  closure_expression,
  parenthesized_expression,
  error_node,
};

[[nodiscard]] std::string_view syntax_kind_name(SyntaxKind kind) noexcept;

}  // namespace raz::compiler
