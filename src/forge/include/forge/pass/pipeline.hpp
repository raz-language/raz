// Copyright 2026 Mario Vinciguerra
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <optional>
#include <string_view>

#include "forge/pass/pass.hpp"

namespace forge::pass {

enum class OptimizationLevel { o0, o1, o2, o3, os, oz };

[[nodiscard]] std::optional<OptimizationLevel> parse_optimization_level(std::string_view text);
[[nodiscard]] std::string_view optimization_level_name(OptimizationLevel level);
void build_standard_pipeline(PassManager& pipeline, OptimizationLevel level);

} // namespace forge::pass
