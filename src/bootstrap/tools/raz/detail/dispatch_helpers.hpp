// Copyright 2026 Mario Vinciguerra
// SPDX-License-Identifier: Apache-2.0

int diagnostic_catalog(const Options& options) {
  static constexpr std::string_view codes[] = {
#include "diagnostic_catalog.hpp"
  };
  struct Category { std::string_view name; std::string_view range; std::string_view description; };
  static constexpr Category categories[] = {
      {"lexer", "D0000-D0999", "lexical/source tokenization diagnostics"},
      {"parser", "D1000-D1999", "syntax and parser recovery diagnostics"},
      {"semantic", "D2000-D2999", "types, ownership, names, traits, and semantic diagnostics"},
      {"lowering", "D3000-D3999", "HIR/MIR lowering and control-flow diagnostics"},
      {"backend", "D4000-D4999", "Forge/backend lowering and verification diagnostics"},
  };
  if (options.diagnostic_format == DiagnosticFormat::json) {
    std::cout << "{\"schema\":\"raz-diagnostic-catalog-v1\",\"categories\":[";
    for (std::size_t i = 0; i < std::size(categories); ++i) {
      if (i != 0) std::cout << ',';
      std::cout << "{\"name\":\"" << categories[i].name << "\",\"range\":\"" << categories[i].range
                << "\",\"description\":\"" << json_escape(categories[i].description) << "\"}";
    }
    std::cout << "],\"codes\":[";
    for (std::size_t i = 0; i < std::size(codes); ++i) {
      if (i != 0) std::cout << ',';
      std::cout << '"' << codes[i] << '"';
    }
    std::cout << "],\"configurable_warnings\":[\"D2052\"]}\n";
    return 0;
  }
  std::cout << "Raz diagnostic catalog (" << std::size(codes) << " known codes)\n\n";
  for (const auto& category : categories)
    std::cout << "  " << std::left << std::setw(10) << category.name << ' ' << category.range << "  " << category.description << '\n';
  std::cout << "\nConfigurable warnings:\n  D2052  moving a Copy value is equivalent to copying it\n"
            << "\nWarning policy: --allow/--warn/--deny accept a code, category name, or 'warnings'.\n";
  return 0;
}

int create_project(const Options& options) {
  const auto root = std::filesystem::absolute(options.project);
  if (std::filesystem::exists(root) && !std::filesystem::is_empty(root)) {
    cli_errorf("destination is not empty: ", root); return 1;
  }

  std::filesystem::create_directories(root / "src");
  const auto name = root.filename().string();
  std::ofstream(root / "raz.toml") << "[package]\nname = \"" << name << "\"\nversion = \"0.1.0\"\nkind = \"executable\"\nentry = \"src/main.rz\"\n\n[dependencies]\n\n[profile.debug]\noptimization = 0\ndebug = true\nincremental = true\n\n[profile.release]\noptimization = 3\ndebug = false\nincremental = true\n";
  std::ofstream(root / "src/main.rz") << "fn main() -> i64 {\n  return 0;\n}\n";
  cli_status("Created", std::string("package ") + name + " at " + root.string(), raz::terminal::green);
  return 0;
}

int metadata(const ProjectGraph& graph) {
  std::cout << "package " << graph.manifest.name << ' ' << graph.manifest.version << " ("
            << raz::compiler::package_kind_name(graph.manifest.kind) << ")\n";
  for (const auto& module : graph.modules) {
    std::cout << "  module " << module.logical_name << " -> " << module.source_path;
    if (module.is_entry) std::cout << " [entry]";
    std::cout << '\n';
    for (const auto& imported : module.imports) {
      std::cout << "    " << (imported.reexport ? "reexports " : "imports ") << imported.path;
      if (!imported.alias.empty()) std::cout << " as " << imported.alias;
      std::cout << '\n';
    }
  }

  for (const auto& dependency : graph.dependencies)
    std::cout << "  dependency " << dependency.manifest.name << " -> " << dependency.manifest.root << '\n';
  return 0;
}
}  // namespace
