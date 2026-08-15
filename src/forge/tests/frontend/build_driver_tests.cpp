// Copyright 2026 Mario Vinciguerra
// SPDX-License-Identifier: Apache-2.0

#include "forge/ir/artifact_cache.hpp"
#include "forge/ir/build_driver.hpp"
#include "forge/ir/builder.hpp"
#include <atomic>
#include <filesystem>
#include <iostream>

namespace {
forge::ir::Module make_module(int count, int changed_index = -1) {
    forge::ir::Context context;
    auto& module = context.create_module("parallel-build-test");
    forge::ir::IRBuilder builder(context, module);
    for (int index = 0; index < count; ++index) {
        const auto function = builder.create_function_handle("f" + std::to_string(index), forge::ir::i64_type());
        const auto entry = builder.create_block_handle(function, "entry");
        builder.position_at_end(entry);
        const auto value = builder.create_constant(forge::ir::i64_type(),
            std::to_string(index == changed_index ? index + 100 : index));
        builder.create_return(value);
    }
    return module;
}
}

int main() {
    const auto previous = forge::ir::build_incremental_snapshot(make_module(8));
    const auto current = forge::ir::build_incremental_snapshot(make_module(8, 3));
    const auto plan = forge::ir::build_incremental_build_plan(previous, current, "raz", "-O2;x86_64");
    const auto schedule = forge::ir::build_parallel_build_schedule(plan, 3);
    if (schedule.worker_count != 3 || schedule.shards.size() != 3) return 1;
    std::size_t scheduled = 0;
    for (const auto& shard : schedule.shards) scheduled += shard.functions.size();
    if (scheduled != 8) return 1;
    const auto schedule_json = forge::ir::build_parallel_build_schedule_json(schedule);
    if (schedule_json.find("\"workerCount\":3") == std::string::npos) return 1;

    const auto root = std::filesystem::temp_directory_path() / "forge-parallel-build-driver-test";
    std::filesystem::remove_all(root);
    forge::ir::ArtifactCache cache(root);
    std::atomic<std::size_t> compiled{};
    auto compiler = [&](const forge::ir::FunctionBuildDecision& decision) {
        ++compiled;
        return std::vector<std::uint8_t>(decision.name.begin(), decision.name.end());
    };

    // Prime unchanged artifacts so the first run has seven hits and one miss.
    for (const auto& decision : plan.functions) {
        if (decision.action == forge::ir::BuildAction::reuse) {
            const std::vector<std::uint8_t> bytes(decision.name.begin(), decision.name.end());
            cache.store(decision.semantic_cache_key, bytes);
        }
    }
    const auto first = forge::ir::execute_incremental_build_plan(plan, cache, compiler, {}, 3);
    if (!first.success() || first.worker_count != 3 || first.cache_hits != 7 ||
        first.cache_misses != 1 || first.rebuilt != 1 || first.reused != 7 || compiled != 1) return 1;
    const auto first_json = forge::ir::build_execution_summary_json(first);
    if (first_json.find("\"cacheHits\":7") == std::string::npos ||
        first_json.find("\"rebuilt\":1") == std::string::npos) return 1;

    // A repeated run should source every semantic artifact from the cache.
    const auto second = forge::ir::execute_incremental_build_plan(plan, cache, compiler, {}, 4);
    if (!second.success() || second.cache_hits != 8 || second.cache_misses != 0 || compiled != 1) return 1;

    std::filesystem::remove_all(root);
    std::cout << "parallel cache-aware build driver test passed\n";
    return 0;
}
