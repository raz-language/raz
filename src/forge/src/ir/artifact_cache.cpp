// Copyright 2026 Mario Vinciguerra
// SPDX-License-Identifier: Apache-2.0

#include "forge/ir/artifact_cache.hpp"
#include <algorithm>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace forge::ir {
namespace {
void json_string(std::ostringstream& out, std::string_view value) {
    out << '"';
    for (const char ch : value) {
        switch (ch) {
        case '"': out << "\\\""; break;
        case '\\': out << "\\\\"; break;
        case '\n': out << "\\n"; break;
        case '\r': out << "\\r"; break;
        case '\t': out << "\\t"; break;
        default: out << ch; break;
        }
    }
    out << '"';
}

bool valid_key(std::string_view key) {
    return key.size() == 64 && std::all_of(key.begin(), key.end(), [](char ch) {
        return (ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f');
    });
}

const FunctionFingerprint* find_function(const IncrementalSnapshot& snapshot, std::string_view name) {
    const auto it = std::lower_bound(snapshot.functions.begin(), snapshot.functions.end(), name,
        [](const FunctionFingerprint& item, std::string_view value) { return item.name < value; });
    return it != snapshot.functions.end() && it->name == name ? &*it : nullptr;
}
} // namespace

std::size_t IncrementalBuildPlan::rebuild_count() const noexcept { return std::count_if(functions.begin(), functions.end(), [](const auto& d){ return d.action == BuildAction::rebuild; }); }
std::size_t IncrementalBuildPlan::reuse_count() const noexcept { return std::count_if(functions.begin(), functions.end(), [](const auto& d){ return d.action == BuildAction::reuse; }); }
std::size_t IncrementalBuildPlan::frontend_refresh_count() const noexcept { return std::count_if(functions.begin(), functions.end(), [](const auto& d){ return d.action == BuildAction::frontend_refresh; }); }
std::size_t IncrementalBuildPlan::remove_count() const noexcept { return std::count_if(functions.begin(), functions.end(), [](const auto& d){ return d.action == BuildAction::remove; }); }

std::string_view artifact_kind_name(ArtifactKind kind) noexcept {
    switch (kind) {
    case ArtifactKind::object: return "object";
    case ArtifactKind::machine_code: return "machine-code";
    case ArtifactKind::source_map: return "source-map";
    case ArtifactKind::diagnostics: return "diagnostics";
    case ArtifactKind::custom: return "custom";
    }
    return "custom";
}

std::string_view build_action_name(BuildAction action) noexcept {
    switch (action) {
    case BuildAction::rebuild: return "rebuild";
    case BuildAction::reuse: return "reuse";
    case BuildAction::frontend_refresh: return "frontend-refresh";
    case BuildAction::remove: return "remove";
    }
    return "reuse";
}

std::string build_function_cache_key(const FunctionFingerprint& function, std::string_view frontend_id,
                                     std::string_view configuration, ArtifactKind kind,
                                     bool include_frontend_state) {
    Module module("function-cache-key");
    module.metadata().push_back({"cache.frontend", std::string(frontend_id)});
    module.metadata().push_back({"cache.configuration", std::string(configuration)});
    module.metadata().push_back({"cache.kind", std::string(artifact_kind_name(kind))});
    module.metadata().push_back({"cache.function", function.name});
    module.metadata().push_back({"cache.semantic", function.semantic_fingerprint});
    if (include_frontend_state) module.metadata().push_back({"cache.frontend-state", function.frontend_fingerprint});
    return build_cache_key(module, "forge-function-artifact-v1", include_frontend_state ? "frontend" : "semantic");
}

IncrementalBuildPlan build_incremental_build_plan(const IncrementalSnapshot& previous,
                                                   const IncrementalSnapshot& current,
                                                   std::string_view frontend_id,
                                                   std::string_view configuration,
                                                   ArtifactKind kind) {
    IncrementalBuildPlan plan;
    plan.previous_semantic_fingerprint = previous.semantic_fingerprint;
    plan.current_semantic_fingerprint = current.semantic_fingerprint;
    const auto changes = compare_incremental_snapshots(previous, current, true);
    plan.functions.reserve(changes.size());
    for (const auto& change : changes) {
        FunctionBuildDecision decision;
        decision.name = change.name;
        const auto* current_function = find_function(current, change.name);
        const auto* previous_function = find_function(previous, change.name);
        switch (change.kind) {
        case FunctionChangeKind::added:
        case FunctionChangeKind::modified: decision.action = BuildAction::rebuild; break;
        case FunctionChangeKind::frontend_only: decision.action = BuildAction::frontend_refresh; break;
        case FunctionChangeKind::removed: decision.action = BuildAction::remove; break;
        case FunctionChangeKind::unchanged: decision.action = BuildAction::reuse; break;
        }
        const auto* function = current_function != nullptr ? current_function : previous_function;
        if (function != nullptr) {
            decision.semantic_cache_key = build_function_cache_key(*function, frontend_id, configuration, kind, false);
            decision.frontend_cache_key = build_function_cache_key(*function, frontend_id, configuration, kind, true);
        }
        plan.functions.push_back(std::move(decision));
    }
    return plan;
}

std::string build_incremental_build_plan_json(const IncrementalBuildPlan& plan) {
    std::ostringstream out;
    out << "{\"version\":1,\"previousSemanticFingerprint\":"; json_string(out, plan.previous_semantic_fingerprint);
    out << ",\"currentSemanticFingerprint\":"; json_string(out, plan.current_semantic_fingerprint);
    out << ",\"summary\":{\"rebuild\":" << plan.rebuild_count() << ",\"reuse\":" << plan.reuse_count()
        << ",\"frontendRefresh\":" << plan.frontend_refresh_count() << ",\"remove\":" << plan.remove_count() << "},\"functions\":[";
    for (std::size_t index = 0; index < plan.functions.size(); ++index) {
        if (index != 0) out << ',';
        const auto& decision = plan.functions[index];
        out << "{\"name\":"; json_string(out, decision.name);
        out << ",\"action\":"; json_string(out, build_action_name(decision.action));
        out << ",\"semanticCacheKey\":"; json_string(out, decision.semantic_cache_key);
        out << ",\"frontendCacheKey\":"; json_string(out, decision.frontend_cache_key); out << '}';
    }
    out << "]}";
    return out.str();
}

ArtifactCache::ArtifactCache(std::filesystem::path root) : root_(std::move(root)) {
    if (root_.empty()) throw std::invalid_argument("artifact cache root is empty");
}

std::filesystem::path ArtifactCache::path_for(std::string_view key) const {
    if (!valid_key(key)) throw std::invalid_argument("artifact cache key must be 64 lowercase hexadecimal characters");
    return root_ / std::string(key.substr(0, 2)) / (std::string(key) + ".bin");
}

bool ArtifactCache::contains(std::string_view key) const { return std::filesystem::is_regular_file(path_for(key)); }
std::vector<std::uint8_t> ArtifactCache::load(std::string_view key) const {
    const auto path = path_for(key);
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("artifact cache entry not found: " + path.string());
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

void ArtifactCache::store(std::string_view key, std::span<const std::uint8_t> bytes) {
    const auto path = path_for(key);
    std::filesystem::create_directories(path.parent_path());
    const auto temporary = path.string() + ".tmp";
    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        if (!output) throw std::runtime_error("failed to create artifact cache entry: " + temporary);
        output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        if (!output) throw std::runtime_error("failed to write artifact cache entry: " + temporary);
    }
    std::error_code error;
    std::filesystem::rename(temporary, path, error);
    if (error) {
        std::filesystem::remove(path, error);
        error.clear();
        std::filesystem::rename(temporary, path, error);
        if (error) { std::filesystem::remove(temporary); throw std::runtime_error("failed to commit artifact cache entry"); }
    }
}

bool ArtifactCache::erase(std::string_view key) { return std::filesystem::remove(path_for(key)); }

} // namespace forge::ir
