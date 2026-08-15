// Copyright 2026 Mario Vinciguerra
// SPDX-License-Identifier: Apache-2.0

#include "forge/ir/artifact_cache.hpp"
#include "forge/ir/builder.hpp"
#include "forge/ir/dependency_build.hpp"
#include <filesystem>
#include <iostream>

namespace {
forge::ir::Module make_module(bool change_leaf) {
    forge::ir::Context context;
    auto& module = context.create_module("dependency-test");
    forge::ir::IRBuilder builder(context, module);
    auto make_constant = [&](std::string name, std::int64_t value) {
        const auto function = builder.create_function_handle(std::move(name), forge::ir::i64_type());
        const auto entry = builder.create_block_handle(function, "entry");
        builder.position_at_end(entry);
        const auto result = builder.create_constant(forge::ir::i64_type(), std::to_string(value));
        builder.create_return(result);
    };
    make_constant("leaf", change_leaf ? 2 : 1);
    const auto middle = builder.create_function_handle("middle", forge::ir::i64_type());
    auto entry = builder.create_block_handle(middle, "entry");
    builder.position_at_end(entry);
    auto value = builder.create_call(forge::ir::i64_type(), "@leaf", {});
    builder.create_return(value);
    const auto root = builder.create_function_handle("root", forge::ir::i64_type());
    entry = builder.create_block_handle(root, "entry");
    builder.position_at_end(entry);
    value = builder.create_call(forge::ir::i64_type(), "@middle", {});
    builder.create_return(value);
    make_constant("independent", 9);
    return module;
}
}

int main() {
    const auto previous_module = make_module(false);
    const auto current_module = make_module(true);
    const auto graph = forge::ir::build_function_dependency_graph(current_module);
    if (graph.functions.size() != 4) return 1;
    const auto graph_json = forge::ir::build_dependency_graph_json(graph);
    if (graph_json.find("\"leaf\"") == std::string::npos ||
        graph_json.find("\"middle\"") == std::string::npos) return 1;

    auto plan = forge::ir::build_incremental_build_plan(
        forge::ir::build_incremental_snapshot(previous_module),
        forge::ir::build_incremental_snapshot(current_module), "raz", "-O2;x86_64");
    plan = forge::ir::propagate_dependency_invalidations(plan, graph, "raz", "-O2;x86_64");
    if (plan.rebuild_count() != 3 || plan.reuse_count() != 1) return 1;

    const auto schedule = forge::ir::build_dependency_build_schedule(plan, graph, 4);
    if (schedule.levels.size() < 3 || schedule.levels[0].functions.empty()) return 1;
    if (schedule.levels[0].functions.front().name != "independent" &&
        schedule.levels[0].functions.front().name != "leaf") return 1;
    const auto schedule_json = forge::ir::build_dependency_build_schedule_json(schedule);
    if (schedule_json.find("\"levels\"") == std::string::npos) return 1;

    const auto cache_root = std::filesystem::temp_directory_path() / "forge-dependency-build-test";
    std::filesystem::remove_all(cache_root);
    forge::ir::ArtifactCache cache(cache_root);
    for (const auto& decision : plan.functions) {
        if (decision.action == forge::ir::BuildAction::remove) continue;
        const std::vector<std::uint8_t> bytes(decision.name.begin(), decision.name.end());
        cache.store(decision.semantic_cache_key, bytes);
    }
    const auto assembled = forge::ir::assemble_incremental_artifacts(plan, cache);
    if (assembled.size() < 16 || assembled[0] != 'F' || assembled[1] != 'R') return 1;
    const auto custom = forge::ir::assemble_incremental_artifacts(plan, cache,
        [](const std::vector<forge::ir::FunctionArtifact>& artifacts) {
            std::vector<std::uint8_t> output;
            for (const auto& artifact : artifacts) output.push_back(static_cast<std::uint8_t>(artifact.name.size()));
            return output;
        });
    if (custom.size() != 4) return 1;
    std::filesystem::remove_all(cache_root);
    std::cout << "dependency-aware incremental build test passed\n";
    return 0;
}
