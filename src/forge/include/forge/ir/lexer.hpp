// Copyright 2026 Mario Vinciguerra
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>
#include "forge/diagnostics/diagnostic.hpp"

namespace forge::ir {
enum class TokenKind { identifier, value, symbol, integer, string, punctuation, arrow, end };
struct Token { TokenKind kind{}; std::string text; SourceRange range{}; };
struct LexResult { std::vector<Token> tokens; Diagnostics diagnostics; };
[[nodiscard]] LexResult lex(std::string_view source);
}
