// Copyright 2026 Mario Vinciguerra
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "forge/ir/artifact_cache.hpp"
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace forge::ir {

struct BuildShard {
    std::size_t index{};
    std::vector<FunctionBuildDecision> functions;
};

struct ParallelBuildSchedule {
    std::size_t requested_workers{};
    std::size_t worker_count{};
    std::vector<BuildShard> shards;
};

struct FunctionBuildResult {
    std::string name;
    BuildAction action{BuildAction::reuse};
    bool cache_hit{};
    bool succeeded{};
    std::size_t artifact_bytes{};
    std::string message;
};

struct BuildExecutionSummary {
    std::size_t worker_count{};
    std::size_t cache_hits{};
    std::size_t cache_misses{};
    std::size_t rebuilt{};
    std::size_t reused{};
    std::size_t frontend_refreshed{};
    std::size_t removed{};
    std::size_t failed{};
    std::size_t artifact_bytes{};
    std::vector<FunctionBuildResult> functions;
    [[nodiscard]] bool success() const noexcept { return failed == 0; }
};

using FunctionCompiler = std::function<std::vector<std::uint8_t>(const FunctionBuildDecision&)>;
using FrontendRefresher = std::function<void(const FunctionBuildDecision&)>;

[[nodiscard]] ParallelBuildSchedule build_parallel_build_schedule(
    const IncrementalBuildPlan& plan, std::size_t requested_workers = 0);
[[nodiscard]] std::string build_parallel_build_schedule_json(const ParallelBuildSchedule& schedule);
[[nodiscard]] std::string build_execution_summary_json(const BuildExecutionSummary& summary);

[[nodiscard]] BuildExecutionSummary execute_incremental_build_plan(
    const IncrementalBuildPlan& plan,
    ArtifactCache& cache,
    FunctionCompiler compiler,
    FrontendRefresher frontend_refresher = {},
    std::size_t requested_workers = 0);

} // namespace forge::ir
