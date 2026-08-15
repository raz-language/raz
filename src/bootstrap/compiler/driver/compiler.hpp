// Copyright 2026 Mario Vinciguerra
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "compiler/diagnostics/diagnostic_engine.hpp"
#include "compiler/session/session.hpp"
#include "compiler/source/source_manager.hpp"
#include "compiler/lexer/token.hpp"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>
#include <optional>

namespace raz::compiler {

enum class SemanticSymbolKind : std::uint8_t {
  namespace_, type, enum_, trait, function, method, field, parameter, variable, constant, enum_variant
};

struct SemanticSymbol final {
  std::string name;
  SemanticSymbolKind kind = SemanticSymbolKind::variable;
  std::string type_name;
  std::string detail;
  std::string container;
  SourceRange declaration{};
  SourceRange scope{};
};

struct SemanticOccurrence final {
  std::string name;
  SourceRange range{};
  std::optional<std::size_t> symbol;
  bool declaration = false;
};

struct FrontendAnalysis final {
  SourceManager sources;
  DiagnosticEngine diagnostics;
  std::size_t token_count = 0;
  std::vector<Token> tokens;
  std::vector<SemanticSymbol> symbols;
  std::vector<SemanticOccurrence> occurrences;
};

class Compiler final {
 public:
  [[nodiscard]] int run(const Session& session) const;
  [[nodiscard]] FrontendAnalysis analyze_text(std::filesystem::path path,
                                              std::string text) const;
};

}  // namespace raz::compiler
