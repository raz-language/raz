// Copyright 2026 Mario Vinciguerra
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "forge/ir/incremental.hpp"
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace forge::ir {

enum class ArtifactKind { object, machine_code, source_map, diagnostics, custom };
enum class BuildAction { rebuild, reuse, frontend_refresh, remove };

struct FunctionBuildDecision {
    std::string name;
    BuildAction action{BuildAction::reuse};
    std::string semantic_cache_key;
    std::string frontend_cache_key;
};

struct IncrementalBuildPlan {
    std::string previous_semantic_fingerprint;
    std::string current_semantic_fingerprint;
    std::vector<FunctionBuildDecision> functions;
    [[nodiscard]] std::size_t rebuild_count() const noexcept;
    [[nodiscard]] std::size_t reuse_count() const noexcept;
    [[nodiscard]] std::size_t frontend_refresh_count() const noexcept;
    [[nodiscard]] std::size_t remove_count() const noexcept;
};

[[nodiscard]] std::string_view artifact_kind_name(ArtifactKind kind) noexcept;
[[nodiscard]] std::string_view build_action_name(BuildAction action) noexcept;
[[nodiscard]] std::string build_function_cache_key(const FunctionFingerprint& function,
                                                    std::string_view frontend_id,
                                                    std::string_view configuration,
                                                    ArtifactKind kind,
                                                    bool include_frontend_state = false);
[[nodiscard]] IncrementalBuildPlan build_incremental_build_plan(const IncrementalSnapshot& previous,
                                                                const IncrementalSnapshot& current,
                                                                std::string_view frontend_id,
                                                                std::string_view configuration,
                                                                ArtifactKind kind = ArtifactKind::object);
[[nodiscard]] std::string build_incremental_build_plan_json(const IncrementalBuildPlan& plan);

class ArtifactCache {
public:
    explicit ArtifactCache(std::filesystem::path root);
    [[nodiscard]] const std::filesystem::path& root() const noexcept { return root_; }
    [[nodiscard]] std::filesystem::path path_for(std::string_view key) const;
    [[nodiscard]] bool contains(std::string_view key) const;
    [[nodiscard]] std::vector<std::uint8_t> load(std::string_view key) const;
    void store(std::string_view key, std::span<const std::uint8_t> bytes);
    bool erase(std::string_view key);
private:
    std::filesystem::path root_;
};

} // namespace forge::ir
