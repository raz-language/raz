// Copyright 2026 Mario Vinciguerra
// SPDX-License-Identifier: Apache-2.0

#include "forge/ir/artifact_cache.hpp"
#include "forge/ir/builder.hpp"
#include <array>
#include <filesystem>
#include <iostream>

namespace {
forge::ir::Module make_module(std::string literal, bool frontend_change, bool extra_function) {
    forge::ir::Context context;
    auto& owned = context.create_module("incremental-cache-test");
    forge::ir::IRBuilder builder(context, owned);
    auto function = builder.create_function_handle("answer", forge::ir::i64_type());
    auto entry = builder.create_block_handle(function, "entry");
    builder.position_at_end(entry);
    if (frontend_change) builder.set_next_attribute("frontend.display", "changed");
    const auto value = builder.create_constant(forge::ir::i64_type(), std::move(literal));
    builder.create_return(value);
    if (extra_function) {
        auto helper = builder.create_function_handle("helper", forge::ir::void_type());
        auto helper_entry = builder.create_block_handle(helper, "entry");
        builder.position_at_end(helper_entry);
        builder.create_return();
    }
    return owned;
}
}

int main() {
    const auto previous_module = make_module("41", false, true);
    const auto current_module = make_module("42", false, false);
    const auto previous = forge::ir::build_incremental_snapshot(previous_module);
    const auto current = forge::ir::build_incremental_snapshot(current_module);
    const auto plan = forge::ir::build_incremental_build_plan(previous, current, "raz", "-O2;x86_64");
    if (plan.rebuild_count() != 1 || plan.remove_count() != 1 || plan.reuse_count() != 0) return 1;
    if (plan.functions.size() != 2 || plan.functions.front().name != "answer") return 1;
    if (plan.functions.front().semantic_cache_key.size() != 64 ||
        plan.functions.front().frontend_cache_key.size() != 64) return 1;
    const auto json = forge::ir::build_incremental_build_plan_json(plan);
    if (json.find("\"rebuild\":1") == std::string::npos || json.find("\"remove\":1") == std::string::npos) return 1;

    const auto metadata_module = make_module("41", true, true);
    const auto metadata_plan = forge::ir::build_incremental_build_plan(
        previous, forge::ir::build_incremental_snapshot(metadata_module), "raz", "-O2;x86_64");
    if (metadata_plan.frontend_refresh_count() != 1 || metadata_plan.reuse_count() != 1) return 1;

    const auto root = std::filesystem::temp_directory_path() / "forge-artifact-cache-test";
    std::filesystem::remove_all(root);
    forge::ir::ArtifactCache cache(root);
    const auto key = plan.functions.front().semantic_cache_key;
    const std::array<std::uint8_t, 5> bytes{0x46, 0x4f, 0x52, 0x47, 0x45};
    cache.store(key, bytes);
    if (!cache.contains(key) || cache.load(key) != std::vector<std::uint8_t>(bytes.begin(), bytes.end())) return 1;
    if (!cache.erase(key) || cache.contains(key)) return 1;
    std::filesystem::remove_all(root);
    std::cout << "incremental artifact cache and selective build plan test passed\n";
    return 0;
}
