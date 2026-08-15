// Copyright 2026 Mario Vinciguerra
// SPDX-License-Identifier: Apache-2.0

#include "compiler/project/workspace.hpp"

#include <algorithm>
#include <fstream>
#include <iomanip>
#include <limits>
#include <queue>
#include <sstream>

namespace raz::compiler {
namespace {

constexpr std::uint64_t fnv_offset = 1469598103934665603ULL;
constexpr std::uint64_t fnv_prime = 1099511628211ULL;

std::uint64_t hash_bytes(std::string_view text, std::uint64_t seed = fnv_offset) {
  auto hash = seed;
  for (const unsigned char byte : text) {
    hash ^= static_cast<std::uint64_t>(byte);
    hash *= fnv_prime;
  }
  return hash;
}

std::uint64_t hash_file(const std::filesystem::path& path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) return 0;
  auto hash = fnv_offset;
  char buffer[64 * 1024];
  while (input) {
    input.read(buffer, sizeof(buffer));
    const auto count = input.gcount();
    hash = hash_bytes(std::string_view(buffer, static_cast<std::size_t>(count)), hash);
  }
  return hash;
}

std::filesystem::path canonical_path(const std::filesystem::path& path) {
  std::error_code error;
  auto result = std::filesystem::weakly_canonical(path, error);
  return error ? path.lexically_normal() : result;
}

std::string module_key(const ProjectGraph& package, const ModuleUnit& module) {
  return package.manifest.name + "@" + package.manifest.version + "::" + module.logical_name;
}

const ProjectGraph* dependency_by_alias(const ProjectGraph& graph, std::string_view alias) {
  for (const auto& dependency : graph.dependencies) {
    if (dependency.dependency_alias == alias || dependency.manifest.name == alias) return &dependency;
  }
  return nullptr;
}

const ModuleUnit* module_for_import(const ProjectGraph& package, std::string_view import_path) {
  const ModuleUnit* best = nullptr;
  std::size_t best_length = 0;
  for (const auto& module : package.modules) {
    const std::string_view logical = module.logical_name;
    const std::string_view ns = module.source_namespace;
    const bool logical_match = import_path == logical ||
        (import_path.size() > logical.size() && import_path.starts_with(logical) && import_path[logical.size()] == ':');
    const bool namespace_match = !ns.empty() && (import_path == ns ||
        (import_path.size() > ns.size() && import_path.starts_with(ns) && import_path[ns.size()] == ':'));
    const std::size_t candidate_length = logical_match ? logical.size() : (namespace_match ? ns.size() : 0);
    if (candidate_length > best_length) {
      best = &module;
      best_length = candidate_length;
    }
  }
  return best;
}

std::vector<std::string> resolve_import(const ProjectGraph& package, std::string_view import_path) {
  std::vector<std::string> result;
  const auto separator = import_path.find("::");
  const auto root = separator == std::string_view::npos ? import_path : import_path.substr(0, separator);
  if (const auto* dependency = dependency_by_alias(package, root)) {
    if (separator == std::string_view::npos) {
      for (const auto& module : dependency->modules) result.push_back(module_key(*dependency, module));
      return result;
    }
    const auto remainder = import_path.substr(separator + 2);
    if (const auto* module = module_for_import(*dependency, remainder)) result.push_back(module_key(*dependency, *module));
    return result;
  }
  if (const auto* module = module_for_import(package, import_path)) result.push_back(module_key(package, *module));
  return result;
}

void build_package_modules(const ProjectGraph& package, WorkspaceGraph& workspace) {
  for (const auto& module : package.modules) {
    WorkspaceModuleState state;
    state.key = module_key(package, module);
    state.package_name = package.manifest.name;
    state.logical_name = module.logical_name;
    state.source_path = canonical_path(module.source_path);
    state.source_fingerprint = hash_file(module.source_path);
    auto import_hash = hash_bytes("raz-imports-v1");
    for (const auto& spec : module.imports) {
      import_hash = hash_bytes(spec.path, import_hash);
      import_hash = hash_bytes(spec.alias, import_hash);
      import_hash = hash_bytes(spec.reexport ? "1" : "0", import_hash);
      for (const auto& dependency : resolve_import(package, spec.path)) state.dependencies.push_back(dependency);
    }
    std::sort(state.dependencies.begin(), state.dependencies.end());
    state.dependencies.erase(std::unique(state.dependencies.begin(), state.dependencies.end()), state.dependencies.end());
    state.import_fingerprint = import_hash;
    workspace.module_by_path.emplace(state.source_path, state.key);
    workspace.modules.emplace(state.key, std::move(state));
  }
  for (const auto& dependency : package.dependencies) build_package_modules(dependency, workspace);
}

void rebuild_dependents(WorkspaceGraph& workspace) {
  for (auto& [_, module] : workspace.modules) module.dependents.clear();
  for (const auto& [key, module] : workspace.modules) {
    for (const auto& dependency : module.dependencies) {
      if (auto found = workspace.modules.find(dependency); found != workspace.modules.end()) found->second.dependents.push_back(key);
    }
  }
  for (auto& [_, module] : workspace.modules) {
    std::sort(module.dependents.begin(), module.dependents.end());
    module.dependents.erase(std::unique(module.dependents.begin(), module.dependents.end()), module.dependents.end());
  }
}

std::uint64_t structure_hash(const WorkspaceGraph& workspace) {
  auto hash = hash_bytes("raz-workspace-structure-v1");
  for (const auto& [key, module] : workspace.modules) {
    hash = hash_bytes(key, hash);
    hash = hash_bytes(module.source_path.generic_string(), hash);
    for (const auto& dependency : module.dependencies) hash = hash_bytes(dependency, hash);
  }
  return hash;
}

void propagate_dirty(const WorkspaceGraph& graph, std::set<std::string>& dirty) {
  std::queue<std::string> queue;
  for (const auto& item : dirty) queue.push(item);
  while (!queue.empty()) {
    const auto key = queue.front();
    queue.pop();
    const auto found = graph.modules.find(key);
    if (found == graph.modules.end()) continue;
    for (const auto& dependent : found->second.dependents) {
      if (dirty.insert(dependent).second) queue.push(dependent);
    }
  }
}

}  // namespace

std::uint64_t workspace_text_fingerprint(std::string_view text) { return hash_bytes(text); }

std::optional<std::string> WorkspaceGraph::key_for_path(const std::filesystem::path& path) const {
  const auto canonical = canonical_path(path);
  const auto found = module_by_path.find(canonical);
  if (found == module_by_path.end()) return std::nullopt;
  return found->second;
}

std::set<std::string> WorkspaceGraph::dirty_from_path(const std::filesystem::path& path) const {
  std::set<std::string> dirty;
  if (const auto key = key_for_path(path)) dirty.insert(*key);
  propagate_dirty(*this, dirty);
  return dirty;
}

WorkspaceDelta WorkspaceGraph::compare(const WorkspaceGraph& newer) const {
  WorkspaceDelta delta;
  for (const auto& [key, previous] : modules) {
    const auto current = newer.modules.find(key);
    if (current == newer.modules.end()) {
      delta.removed.insert(key);
      continue;
    }
    if (previous.source_fingerprint != current->second.source_fingerprint) delta.source_changed.insert(key);
    if (previous.import_fingerprint != current->second.import_fingerprint ||
        previous.dependencies != current->second.dependencies || previous.source_path != current->second.source_path) {
      delta.graph_changed.insert(key);
    }
  }

  for (const auto& [key, _] : newer.modules) if (!modules.contains(key)) delta.added.insert(key);
  delta.dirty.insert(delta.added.begin(), delta.added.end());
  delta.dirty.insert(delta.source_changed.begin(), delta.source_changed.end());
  delta.dirty.insert(delta.graph_changed.begin(), delta.graph_changed.end());
  for (const auto& removed : delta.removed) {
    const auto previous = modules.find(removed);
    if (previous == modules.end()) continue;
    for (const auto& dependent : previous->second.dependents) delta.dirty.insert(dependent);
  }

  propagate_dirty(newer, delta.dirty);
  return delta;
}

bool WorkspaceGraph::save(const std::filesystem::path& path) const {
  std::error_code error;
  std::filesystem::create_directories(path.parent_path(), error);
  std::ofstream output(path, std::ios::trunc);
  if (!output) return false;
  output << schema << '\n' << "root " << std::quoted(root.generic_string()) << '\n'
         << "structure " << std::hex << structure_fingerprint << std::dec << '\n';
  for (const auto& [key, module] : modules) {
    output << "module " << std::quoted(key) << ' ' << std::quoted(module.package_name) << ' '
           << std::quoted(module.logical_name) << ' ' << std::quoted(module.source_path.generic_string()) << ' '
           << std::hex << module.source_fingerprint << ' ' << module.import_fingerprint << std::dec << '\n';
    for (const auto& dependency : module.dependencies) output << "edge " << std::quoted(key) << ' ' << std::quoted(dependency) << '\n';
  }
  return static_cast<bool>(output);
}

std::optional<WorkspaceGraph> WorkspaceGraph::load(const std::filesystem::path& path) {
  std::ifstream input(path);
  std::string header;
  if (!std::getline(input, header) || header != schema) return std::nullopt;
  WorkspaceGraph graph;
  std::string kind;
  while (input >> kind) {
    if (kind == "root") {
      std::string value;
      input >> std::quoted(value);
      graph.root = value;
    } else if (kind == "structure") {
      input >> std::hex >> graph.structure_fingerprint >> std::dec;
    } else if (kind == "module") {
      WorkspaceModuleState state;
      std::string path_text;
      input >> std::quoted(state.key) >> std::quoted(state.package_name) >> std::quoted(state.logical_name)
            >> std::quoted(path_text) >> std::hex >> state.source_fingerprint >> state.import_fingerprint >> std::dec;
      state.source_path = path_text;
      graph.module_by_path.emplace(canonical_path(state.source_path), state.key);
      graph.modules.emplace(state.key, std::move(state));
    } else if (kind == "edge") {
      std::string from;
      std::string to;
      input >> std::quoted(from) >> std::quoted(to);
      if (auto found = graph.modules.find(from); found != graph.modules.end()) found->second.dependencies.push_back(to);
    }
    input.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
  }

  for (auto& [_, module] : graph.modules) {
    std::sort(module.dependencies.begin(), module.dependencies.end());
    module.dependencies.erase(std::unique(module.dependencies.begin(), module.dependencies.end()), module.dependencies.end());
  }

  rebuild_dependents(graph);
  return graph;
}

WorkspaceGraph build_workspace_graph(const ProjectGraph& graph) {
  WorkspaceGraph workspace;
  workspace.root = canonical_path(graph.manifest.root);
  build_package_modules(graph, workspace);
  rebuild_dependents(workspace);
  workspace.structure_fingerprint = structure_hash(workspace);
  return workspace;
}

}  // namespace raz::compiler
