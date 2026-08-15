// Copyright 2026 Mario Vinciguerra
// SPDX-License-Identifier: Apache-2.0

#include "compiler/diagnostics/diagnostic_engine.hpp"
#include "compiler/lexer/lexer.hpp"
#include "compiler/lexer/token_kind.hpp"
#include "compiler/source/source_manager.hpp"

#include <cstdlib>
#include <iostream>
#include <string_view>
#include <vector>

namespace {

void require(bool condition, std::string_view message) {
  if (!condition) {
    std::cerr << "FAILED: " << message << '\n';
    std::exit(1);
  }
}

std::vector<raz::compiler::TokenKind> lex(std::string source,
                                           raz::compiler::DiagnosticEngine& diagnostics,
                                           raz::compiler::SourceManager& sources) {
  const auto file = sources.add_virtual_file("test.rz", std::move(source));
  raz::compiler::Lexer lexer(sources, diagnostics, file);
  std::vector<raz::compiler::TokenKind> kinds;
  for (const auto& token : lexer.lex_all()) kinds.push_back(token.kind);
  return kinds;
}

void test_basic_tokens() {
  raz::compiler::SourceManager sources;
  raz::compiler::DiagnosticEngine diagnostics;
  const auto kinds = lex("fn main() { i64 value = 42; return; }", diagnostics, sources);
  const std::vector expected{
      raz::compiler::TokenKind::kw_fn,
      raz::compiler::TokenKind::identifier,
      raz::compiler::TokenKind::left_paren,
      raz::compiler::TokenKind::right_paren,
      raz::compiler::TokenKind::left_brace,
      raz::compiler::TokenKind::identifier,
      raz::compiler::TokenKind::identifier,
      raz::compiler::TokenKind::equal,
      raz::compiler::TokenKind::integer_literal,
      raz::compiler::TokenKind::semicolon,
      raz::compiler::TokenKind::kw_return,
      raz::compiler::TokenKind::semicolon,
      raz::compiler::TokenKind::right_brace,
      raz::compiler::TokenKind::end_of_file};
  require(kinds == expected, "basic token sequence");
  require(!diagnostics.has_errors(), "valid source has no diagnostics");
}

void test_literals_and_comments() {
  raz::compiler::SourceManager sources;
  raz::compiler::DiagnosticEngine diagnostics;
  const auto kinds = lex(
      "/* outer /* nested */ done */ 0xff_u32 0b1010 12.5e-2 \"text\\n\" 'x' '\\n'",
      diagnostics, sources);
  require(kinds.size() == 7, "literal token count including EOF");
  require(kinds[0] == raz::compiler::TokenKind::integer_literal, "hex literal");
  require(kinds[2] == raz::compiler::TokenKind::float_literal, "float literal");
  require(kinds[3] == raz::compiler::TokenKind::string_literal, "string literal");
  require(kinds[4] == raz::compiler::TokenKind::character_literal, "character literal");
  require(kinds[5] == raz::compiler::TokenKind::character_literal, "escaped character literal");
  require(!diagnostics.has_errors(), "valid literals have no diagnostics");
}

void test_diagnostics_and_locations() {
  raz::compiler::SourceManager sources;
  raz::compiler::DiagnosticEngine diagnostics;
  const auto file = sources.add_virtual_file("bad.rz", "fn main() {\n  \"broken\n}\n");
  raz::compiler::Lexer lexer(sources, diagnostics, file);
  (void)lexer.lex_all();
  require(diagnostics.error_count() == 1, "unterminated string diagnostic");
  const auto location = sources.line_column(diagnostics.diagnostics()[0].labels[0].range.begin);
  require(location.line == 2 && location.column == 3, "diagnostic line and column");
}

}  // namespace

int main() {
  test_basic_tokens();
  test_literals_and_comments();
  test_diagnostics_and_locations();
  std::cout << "frontend tests passed\n";
  return 0;
}
