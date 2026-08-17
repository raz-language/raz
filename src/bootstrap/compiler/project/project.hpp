// Copyright 2026 Mario Vinciguerra
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace raz::compiler {

enum class PackageKind { executable, static_library, shared_library };

struct Dependency final {
  std::string name;
  std::filesystem::path path;
};

struct BuildProfile final {
  unsigned optimization_level = 0;
  bool debug_info = true;
  bool incremental = true;
};

struct Manifest final {
  std::string name;
  std::string version = "0.1.0";
  std::filesystem::path root;
  std::filesystem::path source_directory = "src";
  std::filesystem::path entry = "src/main.rz";
  PackageKind kind = PackageKind::executable;
  std::map<std::string, Dependency> dependencies;
  std::vector<std::string> native_libraries;
  std::vector<std::filesystem::path> native_library_paths;
  std::map<std::string, BuildProfile> profiles;
};

struct ImportSpec final {
  std::string path;
  std::string alias;
  bool reexport = false;
};

struct ModuleUnit final {
  std::string logical_name;
  std::filesystem::path source_path;
  std::string source_namespace;
  std::vector<ImportSpec> imports;
  bool is_entry = false;
};

struct ProjectGraph final {
  Manifest manifest;
  std::string dependency_alias;
  std::vector<ModuleUnit> modules;
  std::vector<ProjectGraph> dependencies;
};

struct ProjectError final { std::string message; };

[[nodiscard]] bool load_manifest(const std::filesystem::path& path, Manifest& result,
                                 ProjectError& error);
[[nodiscard]] bool discover_project(const std::filesystem::path& start, ProjectGraph& result,
                                    ProjectError& error);
[[nodiscard]] std::filesystem::path find_manifest(const std::filesystem::path& start);
[[nodiscard]] std::string package_kind_name(PackageKind kind);

}  // namespace raz::compiler
