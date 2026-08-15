// Copyright 2026 Mario Vinciguerra
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include <optional>
#include <string>
#include <vector>
#include "minilang/ast.hpp"
#include "minilang/token.hpp"

namespace minilang {

struct ParseResult {
    std::optional<Program> program;
    std::vector<std::string> diagnostics;
    [[nodiscard]] bool ok() const noexcept { return program.has_value() && diagnostics.empty(); }
};

[[nodiscard]] ParseResult parse(std::vector<Token> tokens);

} // namespace minilang
