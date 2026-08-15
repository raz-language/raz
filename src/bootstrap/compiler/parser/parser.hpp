// Copyright 2026 Mario Vinciguerra
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "compiler/lexer/token.hpp"
#include "compiler/syntax/syntax_tree.hpp"

#include <cstddef>
#include <initializer_list>
#include <string>
#include <string_view>
#include <vector>

namespace raz::compiler {

class DiagnosticEngine;
class SourceManager;

class Parser final {
 public:
  Parser(const SourceManager& sources, DiagnosticEngine& diagnostics,
         std::vector<Token> tokens);
  [[nodiscard]] SyntaxTree parse();

 private:
  [[nodiscard]] const Token& current() const noexcept;
  [[nodiscard]] const Token& previous() const noexcept;
  [[nodiscard]] const Token& peek(std::size_t distance) const noexcept;
  [[nodiscard]] bool at(TokenKind kind) const noexcept;
  [[nodiscard]] bool at_any(std::initializer_list<TokenKind> kinds) const noexcept;
  const Token& advance() noexcept;
  bool consume(TokenKind kind) noexcept;
  Token expect(TokenKind kind, std::string_view message);
  void report(const Token& token, std::string message);
  void synchronize_declaration();
  void synchronize_statement();
  [[nodiscard]] std::string token_text(const Token& token) const;
  [[nodiscard]] SourceRange range_from(SourceLocation begin) const noexcept;

  [[nodiscard]] std::string parse_attributes();
  SyntaxNode parse_declaration();
  SyntaxNode parse_import();
  SyntaxNode parse_namespace();
  SyntaxNode parse_struct_like(SyntaxKind kind);
  SyntaxNode parse_enum();
  SyntaxNode parse_impl();
  SyntaxNode parse_function();
  SyntaxNode parse_const_declaration();
  SyntaxNode parse_comptime();
  SyntaxNode parse_associated_type();
  SyntaxNode parse_associated_const();
  SyntaxNode parse_field();
  SyntaxNode parse_parameter();
  [[nodiscard]] std::string parse_type_name();
  [[nodiscard]] std::string parse_generic_parameter_list();
  [[nodiscard]] std::string parse_const_expression_text(TokenKind closing);
  SyntaxNode parse_statement();
  SyntaxNode parse_block();
  SyntaxNode parse_if();
  SyntaxNode parse_while();
  SyntaxNode parse_for();
  SyntaxNode parse_return();
  SyntaxNode parse_defer();
  SyntaxNode parse_unsafe();
  SyntaxNode parse_match();
  SyntaxNode parse_variable_or_expression_statement();
  SyntaxNode parse_expression(unsigned minimum_precedence = 0);
  SyntaxNode parse_prefix();
  SyntaxNode parse_postfix(SyntaxNode expression);
  [[nodiscard]] unsigned binary_precedence(TokenKind kind) const noexcept;
  [[nodiscard]] bool is_assignment(TokenKind kind) const noexcept;
  [[nodiscard]] bool looks_like_variable_declaration() const noexcept;

  const SourceManager& sources_;
  DiagnosticEngine& diagnostics_;
  std::vector<Token> tokens_;
  std::size_t position_ = 0;
  bool allow_struct_literal_ = true;
};

}  // namespace raz::compiler
