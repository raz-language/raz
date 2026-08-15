// Copyright 2026 Mario Vinciguerra
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "forge/ir/build_driver.hpp"
#include "forge/ir/module.hpp"
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace forge::ir {

struct FunctionDependencyNode {
    std::string name;
    std::vector<std::string> callees;
    std::vector<std::string> callers;
};

struct FunctionDependencyGraph {
    std::vector<FunctionDependencyNode> functions;
};

struct DependencyBuildLevel {
    std::size_t index{};
    std::vector<FunctionBuildDecision> functions;
};

struct DependencyBuildSchedule {
    std::size_t requested_workers{};
    std::size_t worker_count{};
    std::vector<DependencyBuildLevel> levels;
};

struct FunctionArtifact {
    std::string name;
    std::vector<std::uint8_t> bytes;
};

using ArtifactAssembler = std::function<std::vector<std::uint8_t>(const std::vector<FunctionArtifact>&)>;

[[nodiscard]] FunctionDependencyGraph build_function_dependency_graph(const Module& module);
[[nodiscard]] IncrementalBuildPlan propagate_dependency_invalidations(
    const IncrementalBuildPlan& plan,
    const FunctionDependencyGraph& graph,
    std::string_view frontend_id,
    std::string_view configuration,
    ArtifactKind kind = ArtifactKind::object);
[[nodiscard]] DependencyBuildSchedule build_dependency_build_schedule(
    const IncrementalBuildPlan& plan,
    const FunctionDependencyGraph& graph,
    std::size_t requested_workers = 0);
[[nodiscard]] std::string build_dependency_graph_json(const FunctionDependencyGraph& graph);
[[nodiscard]] std::string build_dependency_build_schedule_json(const DependencyBuildSchedule& schedule);
[[nodiscard]] std::vector<std::uint8_t> assemble_incremental_artifacts(
    const IncrementalBuildPlan& plan,
    const ArtifactCache& cache,
    ArtifactAssembler assembler = {});

} // namespace forge::ir
