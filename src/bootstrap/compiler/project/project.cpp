// Copyright 2026 Mario Vinciguerra
// SPDX-License-Identifier: Apache-2.0

#include "compiler/project/project.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <map>
#include <set>
#include <sstream>

namespace raz::compiler {
namespace {

std::string trim(std::string value) {
  const auto first = value.find_first_not_of(" \t\r\n");
  if (first == std::string::npos) {
    return {};
  }

  const auto last = value.find_last_not_of(" \t\r\n");
  return value.substr(first, last - first + 1);
}

std::string unquote(std::string value) {
  value = trim(std::move(value));
  if (value.size() >= 2 && value.front() == '"' && value.back() == '"') {
    return value.substr(1, value.size() - 2);
  }
  return value;
}


std::vector<std::string> parse_string_array(const std::string& value, bool& ok) {
  std::vector<std::string> out;
  auto text = trim(value);
  ok = false;
  if (text.size() < 2 || text.front() != '[' || text.back() != ']') return out;
  text = trim(text.substr(1, text.size() - 2));
  if (text.empty()) { ok = true; return out; }
  std::size_t cursor = 0;
  while (cursor < text.size()) {
    while (cursor < text.size() && std::isspace(static_cast<unsigned char>(text[cursor]))) ++cursor;
    if (cursor >= text.size() || text[cursor] != '"') return {};
    ++cursor;
    std::string item;
    while (cursor < text.size() && text[cursor] != '"') {
      if (text[cursor] == '\\') {
        ++cursor;
        if (cursor >= text.size()) return {};
        if (text[cursor] == '"' || text[cursor] == '\\') item.push_back(text[cursor]);
        else return {};
        ++cursor;
        continue;
      }
      item.push_back(text[cursor++]);
    }
    if (cursor >= text.size() || text[cursor] != '"') return {};
    ++cursor;
    if (item.empty()) return {};
    out.push_back(std::move(item));
    while (cursor < text.size() && std::isspace(static_cast<unsigned char>(text[cursor]))) ++cursor;
    if (cursor == text.size()) break;
    if (text[cursor] != ',') return {};
    ++cursor;
  }
  ok = true;
  return out;
}

bool parse_bool(const std::string& value, bool& out) {
  if (trim(value) == "true") {
    out = true;
    return true;
  }
  if (trim(value) == "false") {
    out = false;
    return true;
  }
  return false;
}

std::string scan_file_namespace(const std::filesystem::path& path) {
  std::ifstream input(path);
  std::string line;
  while (std::getline(input, line)) {
    line = trim(line);
    if (line.empty() || line.starts_with("//")) continue;
    if (!line.starts_with("namespace ") || !line.ends_with(';')) return {};
    line.erase(0, 10);
    line.pop_back();
    return trim(std::move(line));
  }
  return {};
}

std::vector<ImportSpec> scan_imports(const std::filesystem::path& path) {
  std::ifstream input(path);
  std::vector<ImportSpec> imports;
  std::string line;

  while (std::getline(input, line)) {
    line = trim(line);
    bool reexport = false;
    if (line.starts_with("public import ")) {
      reexport = true;
      line.erase(0, 7);
    } else if (line.starts_with("private import ")) {
      line.erase(0, 8);
    }
    if (!line.starts_with("import ")) continue;

    line.erase(0, 7);
    const auto semicolon = line.find(';');
    if (semicolon != std::string::npos) line.resize(semicolon);
    line = trim(line);

    ImportSpec spec;
    spec.reexport = reexport;
    const auto alias = line.rfind(" as ");
    if (alias != std::string::npos) {
      spec.path = trim(line.substr(0, alias));
      spec.alias = trim(line.substr(alias + 4));
    } else {
      spec.path = std::move(line);
    }
    if (!spec.path.empty()) imports.push_back(std::move(spec));
  }

  return imports;
}

bool source_has_entry_main(const std::filesystem::path& path) {
  std::ifstream input(path);
  std::string line;
  int depth = 0;
  while (std::getline(input, line)) {
    line = trim(std::move(line));
    if (depth == 0) {
      auto declaration = line;
      if (declaration.starts_with("public ")) declaration.erase(0, 7);
      else if (declaration.starts_with("private ")) declaration.erase(0, 8);
      if (declaration.starts_with("fn main(") || declaration.starts_with("fn main (")) return true;
    }
    for (const char ch : line) {
      if (ch == '{') ++depth;
      else if (ch == '}') --depth;
    }
  }
  return false;
}

std::string logical_name(const std::filesystem::path& base, const std::filesystem::path& file) {
  auto relative = std::filesystem::relative(file, base);
  relative.replace_extension();

  std::string name;
  for (const auto& component : relative) {
    if (!name.empty()) {
      name += "::";
    }
    name += component.string();
  }

  if (name.ends_with("::mod")) {
    name.resize(name.size() - 5);
  }
  return name;
}

bool apply_explicit_source_order(const std::filesystem::path& source_order_path,
                                 std::vector<ModuleUnit>& modules,
                                 ProjectError& error) {
  std::ifstream order_input(source_order_path);
  std::vector<std::filesystem::path> ordered_paths;
  std::string order_line;

  while (std::getline(order_input, order_line)) {
    order_line = trim(std::move(order_line));
    if (order_line.empty() || order_line.starts_with("#")) {
      continue;
    }
    ordered_paths.push_back(
        std::filesystem::weakly_canonical(source_order_path.parent_path() / order_line));
  }

  if (ordered_paths.empty()) {
    error.message = "source-order.txt contains no source modules: " + source_order_path.string();
    return false;
  }
  if (ordered_paths.size() != modules.size()) {
    error.message = "source-order.txt must list every package .rz source exactly once";
    return false;
  }

  std::map<std::filesystem::path, std::size_t> ranks;
  for (std::size_t index = 0; index < ordered_paths.size(); ++index) {
    const auto& path = ordered_paths[index];
    const bool valid_source = path.extension() == ".rz" && std::filesystem::is_regular_file(path);
    if (!valid_source || !ranks.emplace(path, index).second) {
      error.message = "invalid or duplicate source-order.txt entry: " + path.string();
      return false;
    }
  }

  for (const auto& module : modules) {
    if (!ranks.contains(std::filesystem::weakly_canonical(module.source_path))) {
      error.message = "source-order.txt does not list source: " + module.source_path.string();
      return false;
    }
  }

  std::sort(modules.begin(), modules.end(), [&](const auto& left, const auto& right) {
    return ranks.at(std::filesystem::weakly_canonical(left.source_path)) <
           ranks.at(std::filesystem::weakly_canonical(right.source_path));
  });
  return true;
}

bool discover_recursive(const std::filesystem::path& manifest_path, ProjectGraph& result,
                        ProjectError& error, std::set<std::filesystem::path>& visiting) {
  const auto canonical = std::filesystem::weakly_canonical(manifest_path);
  if (!visiting.insert(canonical).second) {
    error.message = "cyclic package dependency involving " + canonical.string();
    return false;
  }

  if (!load_manifest(canonical, result.manifest, error)) {
    return false;
  }

  const auto source_root = result.manifest.root / result.manifest.source_directory;
  if (!std::filesystem::is_directory(source_root)) {
    error.message = "source directory does not exist: " + source_root.string();
    return false;
  }

  // A source tree may contain path-dependency packages (for example the
  // production compiler's src/* layout). A directory that owns a
  // raz.toml starts a nested package boundary: its .rz files must be discovered
  // when that package is traversed through [dependencies], never as modules of
  // the containing package.
  auto source_it = std::filesystem::recursive_directory_iterator(source_root);
  const auto source_end = std::filesystem::recursive_directory_iterator{};
  for (; source_it != source_end; ++source_it) {
    const auto& entry = *source_it;
    if (entry.is_directory() &&
        std::filesystem::is_regular_file(entry.path() / "raz.toml")) {
      source_it.disable_recursion_pending();
      continue;
    }
    if (!entry.is_regular_file() || entry.path().extension() != ".rz") {
      continue;
    }

    ModuleUnit unit;
    unit.logical_name = logical_name(source_root, entry.path());
    unit.source_path = entry.path();
    unit.source_namespace = scan_file_namespace(entry.path());
    unit.imports = scan_imports(entry.path());
    unit.is_entry = std::filesystem::weakly_canonical(entry.path()) ==
                    std::filesystem::weakly_canonical(result.manifest.root / result.manifest.entry);
    result.modules.push_back(std::move(unit));
  }

  // Ordinary packages use deterministic logical-name ordering. Compiler/toolchain
  // packages may opt into an explicit physical order while their modules are still
  // concatenated into one compilation unit.
  const auto source_order_path = result.manifest.root / "source-order.txt";
  if (std::filesystem::is_regular_file(source_order_path)) {
    if (!apply_explicit_source_order(source_order_path, result.modules, error)) {
      return false;
    }
  } else {
    std::sort(result.modules.begin(), result.modules.end(), [](const auto& left, const auto& right) {
      return left.logical_name < right.logical_name;
    });
  }

  if (result.modules.empty()) {
    error.message = "package contains no .rz source modules";
    return false;
  }

  if (result.manifest.kind == PackageKind::executable &&
      std::none_of(result.modules.begin(), result.modules.end(), [](const auto& module) {
        return module.is_entry;
      })) {
    error.message =
        "executable entry source does not exist: " +
        (result.manifest.root / result.manifest.entry).string();
    return false;
  }

  if (result.manifest.kind == PackageKind::executable) {
    bool entry_has_main = false;
    for (const auto& module : result.modules) {
      const bool has_main = source_has_entry_main(module.source_path);
      if (module.is_entry) entry_has_main = has_main;
      else if (has_main) {
        error.message = module.source_path.string() + ": 'main' is only allowed in the configured executable entry module";
        return false;
      }
    }
    if (!entry_has_main) {
      error.message = (result.manifest.root / result.manifest.entry).string() +
                      ": executable entry module must define fn main(...)";
      return false;
    }
  }

  std::set<std::string> available;
  for (const auto& module : result.modules) {
    available.insert(module.logical_name);
    if (!module.source_namespace.empty()) available.insert(module.source_namespace);
  }
  for (const auto& [name, _] : result.manifest.dependencies) {
    available.insert(name);
  }

  for (const auto& module : result.modules) {
    for (const auto& imported : module.imports) {
      const auto root = imported.path.substr(0, imported.path.find("::"));
      if (!available.contains(imported.path) && !available.contains(root)) {
        error.message = module.source_path.string() + ": unresolved import '" + imported.path + "'";
        return false;
      }
      if (!imported.alias.empty()) {
        if (!std::isalpha(static_cast<unsigned char>(imported.alias.front())) && imported.alias.front() != '_') {
          error.message = module.source_path.string() + ": invalid import alias '" + imported.alias + "'";
          return false;
        }
        for (const unsigned char ch : imported.alias) {
          if (!std::isalnum(ch) && ch != '_') {
            error.message = module.source_path.string() + ": invalid import alias '" + imported.alias + "'";
            return false;
          }
        }
      }
    }
  }

  for (const auto& [alias, dependency] : result.manifest.dependencies) {
    ProjectGraph graph;
    graph.dependency_alias = alias;
    const auto dependency_manifest = dependency.path / "raz.toml";
    if (!discover_recursive(dependency_manifest, graph, error, visiting)) {
      return false;
    }
    graph.dependency_alias = alias;
    result.dependencies.push_back(std::move(graph));
  }

  visiting.erase(canonical);
  return true;
}

bool validate_module_names(const ProjectGraph& graph, ProjectError& error) {
  std::map<std::string, std::filesystem::path> owners;
  for (const auto& module : graph.modules) {
    std::vector<std::string> names{module.logical_name};
    if (!module.source_namespace.empty()) names.push_back(module.source_namespace);
    for (const auto& name : names) {
      const auto [it, inserted] = owners.emplace(name, module.source_path);
      if (!inserted && std::filesystem::weakly_canonical(it->second) != std::filesystem::weakly_canonical(module.source_path)) {
        error.message = "duplicate module namespace '" + name + "' in package " + graph.manifest.name;
        return false;
      }
    }
    std::map<std::string, std::string> aliases;
    for (const auto& imported : module.imports) {
      const auto source_alias = imported.alias.empty() ? imported.path : imported.alias;
      const auto [it, inserted] = aliases.emplace(source_alias, imported.path);
      if (!inserted && it->second != imported.path) {
        error.message = module.source_path.string() + ": import alias '" + source_alias +
                        "' refers to both '" + it->second + "' and '" + imported.path + "'";
        return false;
      }
    }
  }
  for (const auto& dependency : graph.dependencies) {
    if (!validate_module_names(dependency, error)) return false;
  }
  return true;
}

bool validate_package_identities(const ProjectGraph& graph, ProjectError& error,
                                 std::map<std::string, std::pair<std::string, std::filesystem::path>>& seen) {
  const auto canonical = std::filesystem::weakly_canonical(graph.manifest.root);
  const auto found = seen.find(graph.manifest.name);
  if (found != seen.end()) {
    if (found->second.first != graph.manifest.version ||
        std::filesystem::weakly_canonical(found->second.second) != canonical) {
      error.message = "conflicting package identity '" + graph.manifest.name + "': " +
                      found->second.first + " at " + found->second.second.string() + " versus " +
                      graph.manifest.version + " at " + canonical.string();
      return false;
    }
  } else {
    seen.emplace(graph.manifest.name, std::pair{graph.manifest.version, canonical});
  }
  for (const auto& dependency : graph.dependencies) {
    if (!validate_package_identities(dependency, error, seen)) return false;
  }
  return true;
}

}  // namespace

std::filesystem::path find_manifest(const std::filesystem::path& start) {
  auto current = std::filesystem::is_directory(start) ? start : start.parent_path();
  if (current.empty()) {
    current = std::filesystem::current_path();
  }
  current = std::filesystem::absolute(current);

  while (true) {
    const auto candidate = current / "raz.toml";
    if (std::filesystem::exists(candidate)) {
      return candidate;
    }
    if (current == current.root_path()) {
      break;
    }
    current = current.parent_path();
  }

  return {};
}

bool load_manifest(const std::filesystem::path& path, Manifest& result, ProjectError& error) {
  std::ifstream input(path);
  if (!input) {
    error.message = "unable to open manifest: " + path.string();
    return false;
  }

  result.root = std::filesystem::absolute(path).parent_path();
  result.profiles["debug"] = BuildProfile{};
  result.profiles["release"] = BuildProfile{3, false, true};

  std::string section;
  std::string line;
  std::size_t number = 0;

  while (std::getline(input, line)) {
    ++number;
    const auto comment = line.find('#');
    if (comment != std::string::npos) {
      line.resize(comment);
    }

    line = trim(line);
    if (line.empty()) {
      continue;
    }

    if (line.front() == '[' && line.back() == ']') {
      section = trim(line.substr(1, line.size() - 2));
      continue;
    }

    const auto equal = line.find('=');
    if (equal == std::string::npos) {
      error.message =
          path.string() + ":" + std::to_string(number) + ": expected key = value";
      return false;
    }

    const auto key = trim(line.substr(0, equal));
    const auto value = trim(line.substr(equal + 1));

    if (section == "package") {
      if (key == "name") {
        result.name = unquote(value);
      } else if (key == "version") {
        result.version = unquote(value);
      } else if (key == "source") {
        result.source_directory = unquote(value);
      } else if (key == "entry") {
        result.entry = unquote(value);
      } else if (key == "kind") {
        const auto kind = unquote(value);
        if (kind == "executable" || kind == "bin") {
          result.kind = PackageKind::executable;
        } else if (kind == "static" || kind == "static-library") {
          result.kind = PackageKind::static_library;
        } else if (kind == "shared" || kind == "shared-library") {
          result.kind = PackageKind::shared_library;
        } else {
          error.message = "unknown package kind: " + kind;
          return false;
        }
      }
    } else if (section == "dependencies") {
      Dependency dependency{key, result.root / unquote(value)};
      result.dependencies.emplace(key, std::move(dependency));
    } else if (section == "native") {
      bool array_ok = false;
      if (key == "libraries") {
        auto libraries = parse_string_array(value, array_ok);
        if (!array_ok) {
          error.message = path.string() + ":" + std::to_string(number) + ": native libraries must be a string array";
          return false;
        }
        for (auto& library : libraries) {
          const bool valid = std::all_of(library.begin(), library.end(), [](unsigned char ch) {
            return std::isalnum(ch) || ch == '_' || ch == '-' || ch == '+' || ch == '.';
          });
          if (!valid || library.front() == '.' || library.find("..") != std::string::npos) {
            error.message = path.string() + ":" + std::to_string(number) + ": invalid native library name: " + library;
            return false;
          }
          result.native_libraries.push_back(std::move(library));
        }
      } else if (key == "library-paths") {
        auto paths = parse_string_array(value, array_ok);
        if (!array_ok) {
          error.message = path.string() + ":" + std::to_string(number) + ": native library-paths must be a string array";
          return false;
        }
        for (const auto& native_path : paths) {
          const auto resolved = (result.root / native_path).lexically_normal();
          result.native_library_paths.push_back(resolved);
        }
      } else {
        error.message = path.string() + ":" + std::to_string(number) + ": unknown [native] key: " + key;
        return false;
      }
    } else if (section.starts_with("profile.")) {
      const auto profile_name = section.substr(8);
      auto& profile = result.profiles[profile_name];

      if (key == "optimization") {
        try {
          profile.optimization_level = static_cast<unsigned>(std::stoul(value));
        } catch (...) {
          error.message = "invalid optimization level in profile " + profile_name;
          return false;
        }
        if (profile.optimization_level > 3) {
          error.message = "optimization must be between 0 and 3";
          return false;
        }
      } else if (key == "debug") {
        if (!parse_bool(value, profile.debug_info)) {
          error.message = "invalid boolean for debug";
          return false;
        }
      } else if (key == "incremental") {
        if (!parse_bool(value, profile.incremental)) {
          error.message = "invalid boolean for incremental";
          return false;
        }
      }
    }
  }

  if (result.name.empty()) {
    error.message = "manifest [package] requires name";
    return false;
  }
  return true;
}

bool discover_project(const std::filesystem::path& start, ProjectGraph& result,
                      ProjectError& error) {
  auto manifest = start.filename() == "raz.toml" ? start : find_manifest(start);
  if (manifest.empty()) {
    error.message = "could not find raz.toml from " + start.string();
    return false;
  }

  std::set<std::filesystem::path> visiting;
  if (!discover_recursive(manifest, result, error, visiting)) return false;
  if (!validate_module_names(result, error)) return false;
  std::map<std::string, std::pair<std::string, std::filesystem::path>> identities;
  return validate_package_identities(result, error, identities);
}

std::string package_kind_name(PackageKind kind) {
  switch (kind) {
    case PackageKind::executable:
      return "executable";
    case PackageKind::static_library:
      return "static-library";
    case PackageKind::shared_library:
      return "shared-library";
  }
  return "unknown";
}

}  // namespace raz::compiler
