// Copyright 2026 Mario Vinciguerra
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include <optional>
#include <string>
#include <vector>
#include <forge/ir/context.hpp>
#include "minilang/ast.hpp"

namespace minilang {

struct LowerResult {
    forge::ir::Context context;
    forge::ir::Module* module{};
    std::vector<std::string> diagnostics;
    [[nodiscard]] bool ok() const noexcept { return module != nullptr && diagnostics.empty(); }
};

[[nodiscard]] LowerResult lower_to_forge(const Program& program);

} // namespace minilang
