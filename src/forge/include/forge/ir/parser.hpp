// Copyright 2026 Mario Vinciguerra
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include <optional>
#include <string_view>
#include "forge/diagnostics/diagnostic.hpp"
#include "forge/ir/module.hpp"

namespace forge::ir {

struct ParseResult {
    std::optional<Module> module;
    Diagnostics diagnostics;
    [[nodiscard]] bool ok() const noexcept { return module.has_value() && diagnostics.empty(); }
};

[[nodiscard]] ParseResult parse_module(std::string_view source);

} // namespace forge::ir
