// Copyright 2026 Mario Vinciguerra
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "compiler/project/project.hpp"

#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace raz::compiler {

struct WorkspaceModuleState final {
  std::string key;
  std::string package_name;
  std::string logical_name;
  std::filesystem::path source_path;
  std::uint64_t source_fingerprint = 0;
  std::uint64_t import_fingerprint = 0;
  std::vector<std::string> dependencies;
  std::vector<std::string> dependents;
};

struct WorkspaceDelta final {
  std::set<std::string> added;
  std::set<std::string> removed;
  std::set<std::string> source_changed;
  std::set<std::string> graph_changed;
  std::set<std::string> dirty;

  [[nodiscard]] bool empty() const {
    return added.empty() && removed.empty() && source_changed.empty() && graph_changed.empty();
  }
};

class WorkspaceGraph final {
 public:
  static constexpr std::string_view schema = "raz-workspace-v1";

  std::filesystem::path root;
  std::map<std::string, WorkspaceModuleState> modules;
  std::map<std::filesystem::path, std::string> module_by_path;
  std::uint64_t structure_fingerprint = 0;

  [[nodiscard]] std::optional<std::string> key_for_path(const std::filesystem::path& path) const;
  [[nodiscard]] WorkspaceDelta compare(const WorkspaceGraph& newer) const;
  [[nodiscard]] std::set<std::string> dirty_from_path(const std::filesystem::path& path) const;
  [[nodiscard]] bool save(const std::filesystem::path& path) const;
  [[nodiscard]] static std::optional<WorkspaceGraph> load(const std::filesystem::path& path);
};

[[nodiscard]] WorkspaceGraph build_workspace_graph(const ProjectGraph& graph);
[[nodiscard]] std::uint64_t workspace_text_fingerprint(std::string_view text);

}  // namespace raz::compiler
