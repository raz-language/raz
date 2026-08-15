// Copyright 2026 Mario Vinciguerra
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <string>
#include <string_view>
#include <vector>
#include "forge/ir/module.hpp"

namespace forge::ir {

struct FunctionFingerprint {
    std::string name;
    std::string semantic_fingerprint;
    std::string frontend_fingerprint;
};

struct IncrementalSnapshot {
    std::string semantic_fingerprint;
    std::string frontend_fingerprint;
    std::vector<FunctionFingerprint> functions;
};

enum class FunctionChangeKind {
    added,
    removed,
    modified,
    frontend_only,
    unchanged
};

struct FunctionChange {
    std::string name;
    FunctionChangeKind kind{FunctionChangeKind::unchanged};
    std::string previous_semantic_fingerprint;
    std::string current_semantic_fingerprint;
    std::string previous_frontend_fingerprint;
    std::string current_frontend_fingerprint;
};

[[nodiscard]] IncrementalSnapshot build_incremental_snapshot(const Module& module);
[[nodiscard]] std::vector<FunctionChange> compare_incremental_snapshots(
    const IncrementalSnapshot& previous,
    const IncrementalSnapshot& current,
    bool include_unchanged = false);
[[nodiscard]] std::string build_incremental_manifest_json(const IncrementalSnapshot& snapshot);
[[nodiscard]] std::string build_cache_key(const Module& module,
                                          std::string_view frontend_id,
                                          std::string_view configuration = {});
[[nodiscard]] std::string_view function_change_kind_name(FunctionChangeKind kind) noexcept;

} // namespace forge::ir
