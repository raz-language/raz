// Copyright 2026 Mario Vinciguerra
// SPDX-License-Identifier: Apache-2.0

#include "compiler/diagnostics/diagnostic_engine.hpp"
#include "compiler/lexer/lexer.hpp"
#include "compiler/parser/parser.hpp"
#include "compiler/semantic/semantic_analyzer.hpp"
#include "compiler/source/source_manager.hpp"

#include <cstdlib>
#include <iostream>
#include <string>

namespace {

bool check(std::string source, bool should_succeed) {
  raz::compiler::SourceManager sources;
  raz::compiler::DiagnosticEngine diagnostics;
  const auto file = sources.add_virtual_file("semantic-test.rz", std::move(source));
  raz::compiler::Lexer lexer(sources, diagnostics, file);
  raz::compiler::Parser parser(sources, diagnostics, lexer.lex_all());
  const auto syntax = parser.parse();
  if (!diagnostics.has_errors()) {
    raz::compiler::SemanticAnalyzer analyzer(diagnostics);
    const auto hir = analyzer.analyze(syntax);
    (void)hir;
  }
  return diagnostics.has_errors() != should_succeed;
}

}  // namespace

int main() {
  if (!check("fn add(i64 a, i64 b) -> i64 { i64 sum = a + b; return sum; }", true)) {
    std::cerr << "valid semantic program failed\n";
    return EXIT_FAILURE;
  }

  if (!check("fn main() { Missing value; }", false)) {
    std::cerr << "unknown type was not rejected\n";
    return EXIT_FAILURE;
  }

  if (!check("fn main() { i64 value = missing; }", false)) {
    std::cerr << "unknown name was not rejected\n";
    return EXIT_FAILURE;
  }

  if (!check("fn main() { i64 value = 1; i64 value = 2; }", false)) {
    std::cerr << "duplicate local was not rejected\n";
    return EXIT_FAILURE;
  }

  if (!check("fn valid() -> i64 { i64 value = 41; f64 wide = value as f64; wide += 1.75; return wide as i64; }", true)) {
    std::cerr << "valid numeric casts failed\n";
    return EXIT_FAILURE;
  }

  if (!check("fn invalid() -> i64 { i64 value = 1; f64 other = 2.0; f64 result = value + other; return 0; }", false)) {
    std::cerr << "mixed numeric arithmetic without a cast was not rejected\n";
    return EXIT_FAILURE;
  }

  if (!check("fn takes(f64 value) -> f64 { return value; } fn invalid() -> i64 { f64 value = takes(1); return 0; }", false)) {
    std::cerr << "mixed numeric call argument without a cast was not rejected\n";
    return EXIT_FAILURE;
  }

  if (!check("fn invalid(u64 value) -> f64 { return value as f64; }", false)) {
    std::cerr << "unsupported u64 to float cast was not rejected\n";
    return EXIT_FAILURE;
  }

  if (!check("fn valid(i64 a, i64 b) -> bool { return (a & b) != 0 && a < b; }", true)) {
    std::cerr << "valid logical and bitwise operators failed\n";
    return EXIT_FAILURE;
  }

  if (!check("fn invalid(i64 value) -> bool { return value && true; }", false)) {
    std::cerr << "non-bool logical operand was not rejected\n";
    return EXIT_FAILURE;
  }

  if (!check("fn invalid(f64 value) -> f64 { return value << 1; }", false)) {
    std::cerr << "floating-point shift was not rejected\n";
    return EXIT_FAILURE;
  }

  if (!check("struct Box { i64 value; } fn consume() -> i64 { Box first = Box { value: 1 }; Box second = move first; return second.value; }", true)) {
    std::cerr << "valid explicit move failed\n";
    return EXIT_FAILURE;
  }

  if (!check("struct Box { i64 value; } fn invalid() -> i64 { Box first = Box { value: 1 }; Box second = move first; return first.value; }", false)) {
    std::cerr << "use after move was not rejected\n";
    return EXIT_FAILURE;
  }

  if (!check("struct Box { i64 value; } fn valid() -> i64 { Box first = Box { value: 1 }; Box second = move first; first = Box { value: 2 }; return first.value + second.value; }", true)) {
    std::cerr << "assignment did not reinitialize moved value\n";
    return EXIT_FAILURE;
  }

  if (!check("struct Box { i64 value; } fn invalid() { Box first = Box { value: 1 }; Box second = move first; Box third = move first; }", false)) {
    std::cerr << "double move was not rejected\n";
    return EXIT_FAILURE;
  }

  // Keep this native suite deliberately limited to the semantic subset needed
  // to construct the production compiler.  The C++ host compiler is frozen by
  // tests/data/host-compiler-contract.sha256; ownership path sensitivity,
  // borrow joins, trait/effect solving, async safety, closure capture rules and
  // other evolving language semantics are qualified against the Raz-written
  // compiler instead of turning this binary into a second language test suite.
  std::cout << "semantic tests passed\n";
  return EXIT_SUCCESS;
}
