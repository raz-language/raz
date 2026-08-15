// Copyright 2026 Mario Vinciguerra
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include <string>
#include <string_view>
#include <vector>
#include "minilang/token.hpp"

namespace minilang {

struct LexResult {
    std::vector<Token> tokens;
    std::vector<std::string> diagnostics;
    [[nodiscard]] bool ok() const noexcept { return diagnostics.empty(); }
};

[[nodiscard]] LexResult lex(std::string_view source, std::string file_name);

} // namespace minilang
