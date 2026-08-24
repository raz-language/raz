// Copyright 2026 Mario Vinciguerra
// SPDX-License-Identifier: Apache-2.0

#include "compiler/diagnostics/diagnostic_engine.hpp"
#include "compiler/lexer/lexer.hpp"
#include "compiler/parser/parser.hpp"
#include "compiler/source/source_manager.hpp"
#include "compiler/syntax/syntax_kind.hpp"

#include <cstdlib>
#include <iostream>
#include <string_view>

namespace {

void require(bool condition, std::string_view message) {
  if (!condition) {
    std::cerr << "FAILED: " << message << '\n';
    std::exit(1);
  }
}

raz::compiler::SyntaxTree parse(std::string source,
                                 raz::compiler::DiagnosticEngine& diagnostics,
                                 raz::compiler::SourceManager& sources) {
  const auto file = sources.add_virtual_file("test.rz", std::move(source));
  raz::compiler::Lexer lexer(sources, diagnostics, file);
  raz::compiler::Parser parser(sources, diagnostics, lexer.lex_all());
  return parser.parse();
}

void test_program_structure() {
  raz::compiler::SourceManager sources;
  raz::compiler::DiagnosticEngine diagnostics;
  const auto tree = parse(R"(
import core::option;
struct Point { f64 x; f64 y; }
fn add(i64 left, i64 right) -> i64 {
  i64 total = left + right * 2;
  if (total > 10) { return total; } else { return 10; }
}
)", diagnostics, sources);
  require(!diagnostics.has_errors(), "valid program parses");
  require(tree.root().children.size() == 3, "three top-level declarations");
  require(tree.root().children[0].kind == raz::compiler::SyntaxKind::import_declaration,
          "import declaration");
  require(tree.root().children[1].kind == raz::compiler::SyntaxKind::struct_declaration,
          "struct declaration");
  require(tree.root().children[2].kind == raz::compiler::SyntaxKind::function_declaration,
          "function declaration");
}

void test_postfix_and_assignment() {
  raz::compiler::SourceManager sources;
  raz::compiler::DiagnosticEngine diagnostics;
  const auto tree = parse(R"(
fn main() {
  i64 value = compute(1, 2).field[0];
  value += 4;
}
)", diagnostics, sources);
  require(!diagnostics.has_errors(), "postfix and assignment parse");
  const auto& body = tree.root().children[0].children.back();
  require(body.children.size() == 2, "two statements in body");
  require(body.children[1].children[0].kind == raz::compiler::SyntaxKind::assignment_expression,
          "compound assignment expression");
}

void test_immediate_closure() {
  raz::compiler::SourceManager sources;
  raz::compiler::DiagnosticEngine diagnostics;
  const auto tree = parse(R"(
fn main() -> i64 {
  return fn(i64 value) -> i64 { return value * 2; }(21);
}
)", diagnostics, sources);
  require(!diagnostics.has_errors(), "immediate closure parses");
  const auto& call = tree.root().children[0].children.back().children[0].children[0];
  require(call.kind == raz::compiler::SyntaxKind::call_expression,
          "closure invocation is a call expression");
  require(call.children.front().kind == raz::compiler::SyntaxKind::closure_expression,
          "call target is a closure expression");
}

void test_numeric_cast() {
  raz::compiler::SourceManager sources;
  raz::compiler::DiagnosticEngine diagnostics;
  const auto tree = parse(R"(
fn main() -> i64 {
  f64 value = 41 as f64;
  return value as i64;
}
)", diagnostics, sources);
  require(!diagnostics.has_errors(), "numeric casts parse");
  const auto& body = tree.root().children[0].children.back();
  require(body.children[0].children[0].kind == raz::compiler::SyntaxKind::cast_expression,
          "initializer is a cast expression");
  require(body.children[1].children[0].kind == raz::compiler::SyntaxKind::cast_expression,
          "return value is a cast expression");
}

void test_function_identifier_variable() {
  raz::compiler::SourceManager sources;
  raz::compiler::DiagnosticEngine diagnostics;
  const auto tree = parse(R"(
fn main() -> i64 {
  i64 function = 42;
  return function;
}
)", diagnostics, sources);
  require(!diagnostics.has_errors(), "`function` is valid as a local identifier");
  const auto& body = tree.root().children[0].children.back();
  require(body.children.size() == 2, "function identifier body has two statements");
  require(body.children[0].kind == raz::compiler::SyntaxKind::variable_declaration,
          "function identifier parses as variable declaration");
}

void test_recovery() {
  raz::compiler::SourceManager sources;
  raz::compiler::DiagnosticEngine diagnostics;
  (void)parse("fn broken( { return 1 } fn good() { return; }", diagnostics, sources);
  require(diagnostics.has_errors(), "invalid syntax reports diagnostics");
  require(diagnostics.error_count() >= 1, "at least one parser diagnostic");
}

}  // namespace

int main() {
  test_program_structure();
  test_postfix_and_assignment();
  test_immediate_closure();
  test_numeric_cast();
  test_function_identifier_variable();
  test_recovery();
  std::cout << "parser tests passed\n";
  return 0;
}
