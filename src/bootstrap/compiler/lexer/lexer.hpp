// Copyright 2026 Mario Vinciguerra
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "compiler/lexer/token.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace raz::compiler {

class DiagnosticEngine;
class SourceManager;

class Lexer final {
 public:
  Lexer(const SourceManager& sources, DiagnosticEngine& diagnostics,
        SourceFileId file);

  [[nodiscard]] Token next();
  [[nodiscard]] std::vector<Token> lex_all();

 private:
  [[nodiscard]] bool at_end() const noexcept;
  [[nodiscard]] char peek(std::uint32_t distance = 0) const noexcept;
  char advance() noexcept;
  [[nodiscard]] bool consume(char expected) noexcept;
  void skip_trivia();
  void skip_line_comment();
  void skip_block_comment();
  [[nodiscard]] Token lex_identifier_or_keyword();
  [[nodiscard]] Token lex_number();
  [[nodiscard]] Token lex_string();
  [[nodiscard]] Token lex_character();
  [[nodiscard]] Token lex_lifetime();
  [[nodiscard]] Token make_token(TokenKind kind, std::uint32_t begin) const noexcept;
  void report(std::string code, std::uint32_t begin, std::uint32_t end,
              std::string message);

  DiagnosticEngine& diagnostics_;
  SourceFileId file_;
  std::string_view text_;
  std::uint32_t offset_ = 0;
};

}  // namespace raz::compiler
