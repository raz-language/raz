// Copyright 2026 Mario Vinciguerra
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "forge/machine/module.hpp"

#include <cstdint>
#include <vector>

namespace forge::machine {

struct LivenessAnalysis {
    std::vector<std::vector<bool>> uses;
    std::vector<std::vector<bool>> defs;
    std::vector<std::vector<bool>> live_in;
    std::vector<std::vector<bool>> live_out;
    std::vector<std::vector<std::vector<bool>>> live_after;
    std::vector<std::vector<std::size_t>> successors;
    std::uint32_t fixed_point_iterations{};
    std::uint32_t cross_block_live_values{};
};

[[nodiscard]] LivenessAnalysis analyze_liveness(const Function& function);

} // namespace forge::machine
