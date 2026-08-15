// Copyright 2026 Mario Vinciguerra
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "compiler/lexer/token_kind.hpp"
#include "compiler/source/source_location.hpp"

namespace raz::compiler {

struct Token final {
  TokenKind kind = TokenKind::invalid;
  SourceRange range{};

  [[nodiscard]] constexpr bool is(TokenKind expected) const noexcept {
    return kind == expected;
  }
};

}  // namespace raz::compiler
