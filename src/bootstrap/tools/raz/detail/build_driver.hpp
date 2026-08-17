// Copyright 2026 Mario Vinciguerra
// SPDX-License-Identifier: Apache-2.0

std::uint64_t hash_file(const std::filesystem::path& path, std::string_view salt) {
  std::ifstream input(path, std::ios::binary);
  std::uint64_t hash = 1469598103934665603ULL;
  char c{};
  while (input.get(c)) { hash ^= static_cast<unsigned char>(c); hash *= 1099511628211ULL; }
  for (const char value : salt) { hash ^= static_cast<unsigned char>(value); hash *= 1099511628211ULL; }
  return hash;
}

std::string hex(std::uint64_t value) { std::ostringstream out; out << std::hex << value; return out.str(); }

std::uint64_t hash_text(std::string_view text, std::uint64_t seed = 1469598103934665603ULL) {
  auto hash = seed;
  for (const char value : text) { hash ^= static_cast<unsigned char>(value); hash *= 1099511628211ULL; }
  return hash;
}

std::string canonical_declaration(std::string line) {
  const auto comment = line.find("//");
  if (comment != std::string::npos) line.resize(comment);
  std::string out;
  bool space = false;
  for (const unsigned char ch : line) {
    if (std::isspace(ch)) { space = !out.empty(); continue; }
    if (space && !out.empty()) out.push_back(' ');
    space = false;
    out.push_back(static_cast<char>(ch));
  }

  while (!out.empty() && out.back() == ' ') out.pop_back();
  return out;
}

enum class SemanticSurface { public_api, package_api };

enum class SourceVisibility { package, public_, private_ };

SourceVisibility source_visibility(std::string_view declaration) {
  if (declaration.starts_with("public ")) return SourceVisibility::public_;
  if (declaration.starts_with("private ")) return SourceVisibility::private_;
  return SourceVisibility::package;
}

std::string_view without_visibility(std::string_view declaration) {
  if (declaration.starts_with("public ")) return declaration.substr(7);
  if (declaration.starts_with("private ")) return declaration.substr(8);
  return declaration;
}

std::string_view semantic_body(std::string_view declaration) {
  if (declaration.starts_with("@interface ")) declaration.remove_prefix(11);
  return without_visibility(declaration);
}

bool word_present(std::string_view text, std::string_view word) {
  std::size_t at = 0;
  while ((at = text.find(word, at)) != std::string_view::npos) {
    const auto before_ok = at == 0 || (!std::isalnum(static_cast<unsigned char>(text[at - 1])) && text[at - 1] != '_');
    const auto after = at + word.size();
    const auto after_ok = after == text.size() || (!std::isalnum(static_cast<unsigned char>(text[after])) && text[after] != '_');
    if (before_ok && after_ok) return true;
    at = after;
  }
  return false;
}

std::string declared_type_name(std::string_view canonical) {
  canonical = without_visibility(canonical);
  for (const std::string_view keyword : {"struct ", "enum ", "trait "}) {
    if (!canonical.starts_with(keyword)) continue;
    canonical.remove_prefix(keyword.size());
    const auto end = canonical.find_first_of("< :={;");
    return std::string(canonical.substr(0, end));
  }
  return {};
}

std::string compact_impl_interface(std::string_view declaration) {
  const auto open = declaration.find('{');
  if (open == std::string_view::npos) return std::string(declaration);
  std::string result(declaration.substr(0, open + 1));
  result.push_back(' ');
  std::size_t cursor = open + 1;
  int depth = 1;
  while (cursor < declaration.size() && depth == 1) {
    while (cursor < declaration.size() && std::isspace(static_cast<unsigned char>(declaration[cursor]))) ++cursor;
    if (cursor >= declaration.size() || declaration[cursor] == '}') break;
    const std::size_t start = cursor;
    int paren = 0;
    int angle = 0;
    bool string = false;
    bool character = false;
    bool escaped = false;
    std::size_t stop = cursor;
    char terminator = '\0';
    for (; stop < declaration.size(); ++stop) {
      const char ch = declaration[stop];
      if (escaped) { escaped = false; continue; }
      if ((string || character) && ch == '\\') { escaped = true; continue; }
      if (!character && ch == '"') { string = !string; continue; }
      if (!string && ch == '\'') { character = !character; continue; }
      if (string || character) continue;
      if (ch == '(') ++paren;
      else if (ch == ')') --paren;
      else if (ch == '<') ++angle;
      else if (ch == '>' && angle > 0) --angle;
      else if (paren == 0 && angle == 0 && (ch == '{' || ch == ';')) { terminator = ch; break; }
      else if (paren == 0 && angle == 0 && ch == '}') { terminator = ch; break; }
    }

    if (stop >= declaration.size() || terminator == '}') break;
    std::string signature(declaration.substr(start, stop - start));
    while (!signature.empty() && std::isspace(static_cast<unsigned char>(signature.back()))) signature.pop_back();
    if (!signature.empty()) {
      const auto body = without_visibility(signature);
      const bool function = body.starts_with("fn ") || body.starts_with("async fn ") ||
                            body.starts_with("unsafe fn ") || body.starts_with("unsafe async fn ") ||
                            body.starts_with("const fn ") || body.starts_with("extern fn ");
      if (function) result += "@interface " + signature + "; ";
      else result += signature + "; ";
    }

    if (terminator == ';') { cursor = stop + 1; continue; }
    int body_depth = 1;
    cursor = stop + 1;
    for (; cursor < declaration.size() && body_depth > 0; ++cursor) {
      if (declaration[cursor] == '{') ++body_depth;
      else if (declaration[cursor] == '}') --body_depth;
    }
  }

  if (result.size() >= 2 && result.ends_with("{ ")) result.pop_back();
  result += '}';
  return result;
}

bool visible_on_surface(SourceVisibility visibility, SemanticSurface surface) {
  if (visibility == SourceVisibility::private_) return false;
  return surface == SemanticSurface::package_api || visibility == SourceVisibility::public_;
}

std::vector<std::string> semantic_declarations(const std::filesystem::path& path,
                                               SemanticSurface surface) {
  std::ifstream input(path);
  std::vector<std::string> lines;
  std::string raw;
  while (std::getline(input, raw)) lines.push_back(canonical_declaration(raw));

  std::set<std::string> public_types;
  for (const auto& line : lines) {
    if (source_visibility(line) != SourceVisibility::public_) continue;
    const auto name = declared_type_name(line);
    if (!name.empty()) public_types.insert(name);
  }

  std::vector<std::string> items;
  int outer_depth = 0;
  for (std::size_t line_index = 0; line_index < lines.size();) {
    const auto& line = lines[line_index];
    if (line.empty()) { ++line_index; continue; }

    if (outer_depth != 0) {
      for (const char ch : line) { if (ch == '{') ++outer_depth; else if (ch == '}') --outer_depth; }
      ++line_index;
      continue;
    }

    const auto visibility = source_visibility(line);
    auto body = without_visibility(line);
    const bool aggregate = body.starts_with("struct ") || body.starts_with("enum ") || body.starts_with("trait ");
    const bool implementation = body.starts_with("impl<") || body.starts_with("impl ");
    const bool function = body.starts_with("fn ") || body.starts_with("async fn ") ||
                          body.starts_with("unsafe fn ") || body.starts_with("unsafe async fn ") ||
                          body.starts_with("const fn ") || body.starts_with("extern fn ");
    const bool constant = body.starts_with("const ") && !body.starts_with("const fn ");

    if (aggregate || implementation) {
      std::string declaration;
      int depth = 0;
      bool opened = false;
      std::size_t cursor = line_index;
      for (; cursor < lines.size(); ++cursor) {
        if (!lines[cursor].empty()) {
          if (!declaration.empty()) declaration.push_back(' ');
          declaration += lines[cursor];
          for (const char ch : lines[cursor]) {
            if (ch == '{') { ++depth; opened = true; }
            else if (ch == '}') --depth;
          }
        }
        if (opened && depth == 0) { ++cursor; break; }
        if (!opened && declaration.ends_with(';')) { ++cursor; break; }
      }
      bool include = aggregate && visible_on_surface(visibility, surface);
      if (implementation) {
        if (surface == SemanticSurface::package_api) include = true;
        else {
          for (const auto& name : public_types) {
            if (word_present(declaration, name)) { include = true; break; }
          }
        }
      }
      if (include) {
        items.push_back(std::move(declaration));
      }
      line_index = cursor;
      continue;
    }

    if (function) {
      std::string signature;
      std::size_t cursor = line_index;
      bool finished = false;
      for (; cursor < lines.size() && !finished; ++cursor) {
        const auto& part = lines[cursor];
        if (!signature.empty() && !part.empty()) signature.push_back(' ');
        const auto brace = part.find('{');
        const auto semicolon = part.find(';');
        const auto stop = std::min(brace == std::string::npos ? part.size() : brace,
                                   semicolon == std::string::npos ? part.size() : semicolon);
        signature += part.substr(0, stop);
        if (brace != std::string::npos || semicolon != std::string::npos) finished = true;
      }
      while (!signature.empty() && signature.back() == ' ') signature.pop_back();
      if (visible_on_surface(visibility, surface) && !signature.empty()) items.push_back("@interface " + signature + ";");
      // If a body opened on the declaration line, skip it without treating nested
      // declarations as module API.
      int depth = 0;
      bool opened = false;
      for (std::size_t i = line_index; i < lines.size(); ++i) {
        for (const char ch : lines[i]) { if (ch == '{') { ++depth; opened = true; } else if (ch == '}') --depth; }
        if (opened && depth == 0) { cursor = std::max(cursor, i + 1); break; }
        if (!opened && lines[i].find(';') != std::string::npos) break;
      }
      line_index = std::max(cursor, line_index + 1);
      continue;
    }

    if (constant && visible_on_surface(visibility, surface)) {
      std::string declaration = line;
      std::size_t cursor = line_index + 1;
      while (declaration.find(';') == std::string::npos && cursor < lines.size()) {
        if (!lines[cursor].empty()) declaration += " " + lines[cursor];
        ++cursor;
      }
      items.push_back(std::move(declaration));
      line_index = cursor;
      continue;
    }

    for (const char ch : line) { if (ch == '{') ++outer_depth; else if (ch == '}') --outer_depth; }
    ++line_index;
  }

  std::sort(items.begin(), items.end());
  items.erase(std::unique(items.begin(), items.end()), items.end());
  return items;
}

std::vector<std::string> exported_semantic_declarations(const std::filesystem::path& path) {
  return semantic_declarations(path, SemanticSurface::public_api);
}

std::vector<std::string> package_semantic_declarations(const std::filesystem::path& path) {
  return semantic_declarations(path, SemanticSurface::package_api);
}

std::string semantic_declaration_name(std::string_view declaration) {
  declaration = semantic_body(declaration);
  if (const auto type_name = declared_type_name(declaration); !type_name.empty()) return type_name;

  auto function = declaration;
  for (const std::string_view qualifier : {"unsafe ", "async ", "const ", "extern "}) {
    if (function.starts_with(qualifier)) function.remove_prefix(qualifier.size());
  }
  // Qualifiers can be combined (for example unsafe async fn).
  for (int pass = 0; pass < 3; ++pass) {
    if (function.starts_with("unsafe ")) function.remove_prefix(7);
    if (function.starts_with("async ")) function.remove_prefix(6);
    if (function.starts_with("const ")) function.remove_prefix(6);
    if (function.starts_with("extern ")) function.remove_prefix(7);
  }

  if (function.starts_with("fn ")) {
    function.remove_prefix(3);
    const auto end = function.find_first_of("(< ");
    return std::string(function.substr(0, end));
  }

  if (declaration.starts_with("const ") && !declaration.starts_with("const fn ")) {
    declaration.remove_prefix(6);
    const auto end = declaration.find_first_of(" =:;");
    return std::string(declaration.substr(0, end));
  }
  return {};
}

std::string hex_encode(std::string_view value) {
  static constexpr char digits[] = "0123456789abcdef";
  std::string out;
  out.reserve(value.size() * 2);
  for (const unsigned char ch : value) {
    out.push_back(digits[ch >> 4]);
    out.push_back(digits[ch & 0x0f]);
  }
  return out;
}

std::optional<std::string> hex_decode(std::string_view value) {
  if ((value.size() & 1U) != 0U) return std::nullopt;
  auto nibble = [](char ch) -> int {
    if (ch >= '0' && ch <= '9') return ch - '0';
    if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
    if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
    return -1;
  };
  std::string out;
  out.reserve(value.size() / 2);
  for (std::size_t index = 0; index < value.size(); index += 2) {
    const int high = nibble(value[index]);
    const int low = nibble(value[index + 1]);
    if (high < 0 || low < 0) return std::nullopt;
    out.push_back(static_cast<char>((high << 4) | low));
  }
  return out;
}

std::uint64_t public_interface_fingerprint(const ProjectGraph& graph) {
  auto hash = hash_text("raz-interface-v5");
  hash = hash_text(graph.manifest.name, hash);
  hash = hash_text(graph.manifest.version, hash);
  for (const auto& dependency : graph.dependencies) hash = hash_text(hex(public_interface_fingerprint(dependency)), hash);

  std::string generic_closure_text;
  std::vector<std::string> hidden_candidates;
  for (const auto& module : graph.modules) {
    hash = hash_text(module.logical_name, hash);
    hash = hash_text(module.source_namespace, hash);
    for (const auto& imported : module.imports) {
      hash = hash_text(imported.path, hash);
      hash = hash_text(imported.alias, hash);
      hash = hash_text(imported.reexport ? "public" : "package", hash);
    }
    const auto exported = exported_semantic_declarations(module.source_path);
    const std::set<std::string> exported_set(exported.begin(), exported.end());
    for (const auto& item : exported) {
      hash = hash_text(item, hash);
      const auto body = semantic_body(item);
      if (body.starts_with("impl<") || body.starts_with("impl ")) {
        generic_closure_text += item;
        generic_closure_text.push_back('\n');
      }
    }

    for (const auto& item : package_semantic_declarations(module.source_path)) {
      if (!exported_set.contains(item)) hidden_candidates.push_back(item);
    }
  }

  // Resolve helper signatures package-wide because an exported generic in one
  // module may use a package-internal helper imported from another module.
  std::sort(hidden_candidates.begin(), hidden_candidates.end());
  hidden_candidates.erase(std::unique(hidden_candidates.begin(), hidden_candidates.end()), hidden_candidates.end());
  std::vector<std::string> hidden_closure;
  std::set<std::string> included;
  bool progress = true;
  while (progress) {
    progress = false;
    for (const auto& item : hidden_candidates) {
      if (included.contains(item)) continue;
      const auto name = semantic_declaration_name(item);
      if (name.empty() || !word_present(generic_closure_text, name)) continue;
      included.insert(item);
      hidden_closure.push_back(item);
      generic_closure_text += item;
      generic_closure_text.push_back('\n');
      progress = true;
    }
  }

  std::sort(hidden_closure.begin(), hidden_closure.end());
  for (const auto& item : hidden_closure) hash = hash_text(item, hash);
  return hash;
}

std::uint64_t module_interface_fingerprint(const ProjectGraph& graph,
                                           const raz::compiler::ModuleUnit& module,
                                           std::uint64_t package_interface_hash) {
  auto hash = hash_text("raz-module-interface-v1");
  hash = hash_text(module.logical_name, hash);
  hash = hash_text(module.source_namespace, hash);
  bool exported_generic = false;
  for (const auto& imported : module.imports) {
    if (!imported.reexport) continue;
    hash = hash_text(imported.path, hash);
    hash = hash_text(imported.alias, hash);
  }

  for (const auto& item : exported_semantic_declarations(module.source_path)) {
    hash = hash_text(item, hash);
    const auto body = semantic_body(item);
    if (body.starts_with("impl<")) exported_generic = true;
  }
  // Exported generic bodies can depend on package-private helpers in sibling
  // modules. Preserve the existing package-wide closure hash only for that
  // case; ordinary interfaces stay module-local and invalidate precisely.
  if (exported_generic) hash = hash_text(hex(package_interface_hash), hash);
  return hash;
}

void collect_module_interface_fingerprints(
    const ProjectGraph& graph, const raz::compiler::WorkspaceGraph& workspace,
    std::map<std::string, std::uint64_t>& fingerprints) {
  const auto package_hash = public_interface_fingerprint(graph);
  for (const auto& module : graph.modules) {
    const auto key = workspace.key_for_path(module.source_path);
    if (!key.has_value()) continue;
    fingerprints[*key] = module_interface_fingerprint(graph, module, package_hash);
  }

  for (const auto& dependency : graph.dependencies)
    collect_module_interface_fingerprints(dependency, workspace, fingerprints);
}

std::uint64_t incremental_module_fingerprint(
    const raz::compiler::ModuleUnit& module, const Options& options,
    const BuildProfile& profile, const raz::compiler::WorkspaceGraph& workspace,
    const std::map<std::string, std::uint64_t>& interface_fingerprints) {
  auto hash = hash_file(module.source_path,
      "raz-module-build-v2" + options.target + options.profile +
      std::to_string(profile.optimization_level));
  const auto key = workspace.key_for_path(module.source_path);
  if (!key.has_value()) return hash;
  const auto found = workspace.modules.find(*key);
  if (found == workspace.modules.end()) return hash;
  hash = hash_text(hex(found->second.import_fingerprint), hash);
  for (const auto& dependency : found->second.dependencies) {
    hash = hash_text(dependency, hash);
    if (const auto interface_entry = interface_fingerprints.find(dependency);
        interface_entry != interface_fingerprints.end()) {
      hash = hash_text(hex(interface_entry->second), hash);
    }
  }
  return hash;
}

std::string sanitize(std::string value) {
  for (auto& c : value) if (c == ':' || c == '/' || c == '\\') c = '_';
  return value;
}

bool incremental_stage_cache_matches(const std::filesystem::path& path,
                                     std::string_view build_fingerprint) {
  std::ifstream input(path);
  std::string schema;
  std::string line;
  if (!std::getline(input, schema) || schema != "raz-incremental-stages-v1") return false;
  while (std::getline(input, line)) {
    if (line.starts_with("build=")) return line.substr(6) == build_fingerprint;
  }
  return false;
}

bool write_incremental_stage_cache(const std::filesystem::path& path,
                                   const raz::compiler::WorkspaceModuleState& state,
                                   std::uint64_t interface_fingerprint,
                                   std::string_view build_fingerprint,
                                   const Options& options,
                                   const BuildProfile& profile) {
  std::ofstream out(path, std::ios::trunc);
  if (!out) return false;
  auto semantic = hash_text(hex(state.source_fingerprint));
  semantic = hash_text(hex(state.import_fingerprint), semantic);
  for (const auto& dependency : state.dependencies) semantic = hash_text(dependency, semantic);
  const auto hir = hash_text("hir-v1", semantic);
  auto mir = hash_text("mir-v1", hir);
  mir = hash_text(options.target, mir);
  auto forge_ir = hash_text("forge-ir-v1", mir);
  forge_ir = hash_text(std::to_string(profile.optimization_level), forge_ir);
  out << "raz-incremental-stages-v1\n"
      << "source=" << hex(state.source_fingerprint) << '\n'
      << "imports=" << hex(state.import_fingerprint) << '\n'
      << "interface=" << hex(interface_fingerprint) << '\n'
      << "semantic=" << hex(semantic) << '\n'
      << "hir=" << hex(hir) << '\n'
      << "mir=" << hex(mir) << '\n'
      << "forge_ir=" << hex(forge_ir) << '\n'
      << "build=" << build_fingerprint << '\n';
  return static_cast<bool>(out);
}

std::string shell_quote(const std::filesystem::path& value) {
#if defined(_WIN32)
  std::string text = value.string();
  std::string result = "\"";
  for (const char c : text) result += c == '"' ? "\\\"" : std::string(1, c);
  return result + "\"";
#else
  std::string result = "'";
  for (const char c : value.string()) result += c == '\'' ? "'\\''" : std::string(1, c);
  return result + "'";
#endif
}

int execute_shell_command(const std::string& command) {
#if defined(_WIN32)
  // cmd.exe has special handling for a command line whose executable path is
  // quoted.  std::system() routes through cmd /c, and without an additional
  // outer quote a command such as "C:\\Program Files\\...\\cl.exe" ...
  // is parsed as the executable C:\\Program.  Wrap the complete command
  // so cmd receives the conventional: cmd /c ""C:\\Program Files\\..." args".
  return std::system((std::string("\"") + command + "\"").c_str());
#else
  const int status = std::system(command.c_str());
  if (status == -1) return 127;
  if (WIFEXITED(status)) return WEXITSTATUS(status);
  if (WIFSIGNALED(status)) return 128 + WTERMSIG(status);
  return 1;
#endif
}

std::string native_linker() {
  const std::string configured = environment_value("RAZ_LINKER");
#if defined(_WIN32)
  return configured.empty() ? "clang++" : configured;
#else
  return configured.empty() ? "c++" : configured;
#endif
}

bool windows_msvc_style_driver(std::string linker) {
#if defined(_WIN32)
  std::replace(linker.begin(), linker.end(), '\\', '/');
  const auto slash = linker.find_last_of('/');
  if (slash != std::string::npos) linker.erase(0, slash + 1);
  std::transform(linker.begin(), linker.end(), linker.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return linker == "cl" || linker == "cl.exe" || linker == "clang-cl" || linker == "clang-cl.exe";
#else
  (void)linker;
  return false;
#endif
}

std::string_view diagnostic_format_name(DiagnosticFormat format) {
  switch (format) {
    case DiagnosticFormat::human: return "human";
    case DiagnosticFormat::short_: return "short";
    case DiagnosticFormat::json: return "json";
  }
  return "human";
}

std::string diagnostic_policy_spec(const raz::compiler::DiagnosticPolicy& policy) {
  std::ostringstream out;
  out << (policy.deny_warnings ? "1" : "0");
  for (const auto& override : policy.overrides) {
    char level = 'w';
    if (override.level == raz::compiler::DiagnosticLevel::allow) level = 'a';
    else if (override.level == raz::compiler::DiagnosticLevel::deny) level = 'd';
    out << ';' << level << ':' << override.pattern;
  }
  return out.str();
}

raz::compiler::DiagnosticPolicy parse_diagnostic_policy(std::string_view spec) {
  raz::compiler::DiagnosticPolicy policy;
  std::size_t start = 0;
  bool first = true;
  while (start <= spec.size()) {
    const auto end = spec.find(';', start);
    const auto part = spec.substr(start, end == std::string_view::npos ? spec.size() - start : end - start);
    if (first) {
      policy.deny_warnings = part == "1";
      first = false;
    } else if (part.size() >= 3 && part[1] == ':') {
      raz::compiler::DiagnosticLevel level = raz::compiler::DiagnosticLevel::warn;
      if (part[0] == 'a') level = raz::compiler::DiagnosticLevel::allow;
      else if (part[0] == 'd') level = raz::compiler::DiagnosticLevel::deny;
      policy.overrides.push_back({std::string(part.substr(2)), level});
    }

    if (end == std::string_view::npos) break;
    start = end + 1;
  }
  return policy;
}

int compile_module_worker(const SessionOptions& session) {
  if (g_self_executable.empty()) return Compiler{}.run(Session(session));
  std::ostringstream command;
  command << shell_quote(g_self_executable) << " __compile-module "
          << shell_quote(session.input) << ' ';
  if (session.output.empty()) command << shell_quote(std::filesystem::path("-"));
  else command << shell_quote(session.output);
  command << ' ' << shell_quote(std::filesystem::path(session.target_triple.empty() ? "host" : session.target_triple))
          << ' ' << session.optimization_level
          << ' ' << (session.check_only ? 1 : 0)
          << ' ' << diagnostic_format_name(session.diagnostic_format) << ' ';
  if (session.diagnostic_output.empty()) command << shell_quote(std::filesystem::path("-"));
  else command << shell_quote(session.diagnostic_output);
  command << ' ';
  if (session.diagnostic_display_path.empty()) command << shell_quote(std::filesystem::path("-"));
  else command << shell_quote(session.diagnostic_display_path);
  command << ' ' << session.diagnostic_line_delta << ' ' << session.diagnostic_byte_delta
          << ' ' << shell_quote(std::filesystem::path(diagnostic_policy_spec(session.diagnostic_policy)));
  return execute_shell_command(command.str());
}

std::string native_link_command(const std::vector<std::filesystem::path>& inputs,
                                const std::filesystem::path& output,
                                bool shared = false,
                                const std::vector<std::string>& native_libraries = {},
                                const std::vector<std::filesystem::path>& native_library_paths = {}) {
  const std::string linker = native_linker();
  std::ostringstream command;
#if defined(_WIN32)
  if (windows_msvc_style_driver(linker)) {
    command << shell_quote(std::filesystem::path(linker)) << " /nologo ";
    if (shared) command << "/LD ";
    for (const auto& input : inputs) command << shell_quote(input) << ' ';
    for (const auto& path : native_library_paths) command << "/LIBPATH:" << shell_quote(path) << ' ';
    for (const auto& library : native_libraries) command << shell_quote(std::filesystem::path(library + ".lib")) << ' ';
#ifdef RAZ_RUNTIME_LIBRARY_PATH
    command << shell_quote(std::filesystem::path(RAZ_RUNTIME_LIBRARY_PATH)) << ' ';
#endif
#ifdef RAZ_FORGE_BRIDGE_LIBRARY_PATH
    command << shell_quote(std::filesystem::path(RAZ_FORGE_BRIDGE_LIBRARY_PATH)) << ' ';
#endif
#ifdef RAZ_FORGE_LIBRARY_PATH
    command << shell_quote(std::filesystem::path(RAZ_FORGE_LIBRARY_PATH)) << ' ';
#endif
#ifdef RAZ_OPENSSL_SSL_LIBRARY_PATH
    command << shell_quote(std::filesystem::path(RAZ_OPENSSL_SSL_LIBRARY_PATH)) << ' ';
#endif
#ifdef RAZ_OPENSSL_CRYPTO_LIBRARY_PATH
    command << shell_quote(std::filesystem::path(RAZ_OPENSSL_CRYPTO_LIBRARY_PATH)) << ' ';
#endif
    command << "ws2_32.lib bcrypt.lib crypt32.lib /Fe:" << shell_quote(output);
    if (!shared) {
      // The self-hosted compiler recursively walks large syntax/HIR trees and
      // needs more stack than ordinary Raz applications on Windows. Keep the
      // normal executable reserve at 8 MiB, but give raz-compiler a 32 MiB
      // reserve so compiler-sized inputs cannot hit STATUS_STACK_OVERFLOW.
      const auto output_name = output.filename().string();
      if (output_name == "raz-compiler.exe" || output_name == "raz-compiler")
        command << " /link /STACK:33554432";
      else
        command << " /link /STACK:8388608";
    }
    return command.str();
  }
#endif
  command << shell_quote(std::filesystem::path(linker)) << ' ';
  if (shared) command << "-shared ";
  for (const auto& input : inputs) command << shell_quote(input) << ' ';
  for (const auto& path : native_library_paths) command << "-L" << shell_quote(path) << ' ';
  for (const auto& library : native_libraries) command << "-l" << shell_quote(std::filesystem::path(library)) << ' ';
#ifdef RAZ_RUNTIME_LIBRARY_PATH
  command << shell_quote(std::filesystem::path(RAZ_RUNTIME_LIBRARY_PATH)) << ' ';
#endif
#ifdef RAZ_FORGE_BRIDGE_LIBRARY_PATH
  command << shell_quote(std::filesystem::path(RAZ_FORGE_BRIDGE_LIBRARY_PATH)) << ' ';
#endif
#ifdef RAZ_FORGE_LIBRARY_PATH
  command << shell_quote(std::filesystem::path(RAZ_FORGE_LIBRARY_PATH)) << ' ';
#endif
#ifdef RAZ_OPENSSL_SSL_LIBRARY_PATH
  command << shell_quote(std::filesystem::path(RAZ_OPENSSL_SSL_LIBRARY_PATH)) << ' ';
#endif
#ifdef RAZ_OPENSSL_CRYPTO_LIBRARY_PATH
  command << shell_quote(std::filesystem::path(RAZ_OPENSSL_CRYPTO_LIBRARY_PATH)) << ' ';
#endif
#if defined(_WIN32)
  command << "-lws2_32 -lbcrypt -lcrypt32 ";
#else
  command << "-pthread ";
#endif
  command << "-o " << shell_quote(output);
  return command.str();
}

std::string native_link_command(const std::filesystem::path& object,
                                const std::filesystem::path& output,
                                bool shared = false) {
  return native_link_command(std::vector<std::filesystem::path>{object}, output, shared);
}

bool write_bytes(const std::filesystem::path& path, const std::vector<std::byte>& bytes) {
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
  return static_cast<bool>(output);
}

void print_forge_diagnostics(const forge::Diagnostics& diagnostics) {
  std::cerr << forge::diagnostics::render_all(diagnostics);
}

bool emit_native_object(const std::filesystem::path& ir_path, const std::filesystem::path& object_path,
                        unsigned optimization_level) {
  std::ifstream input(ir_path, std::ios::binary);
  std::ostringstream source;
  source << input.rdbuf();
  auto parsed = forge::ir::parse_module(source.str());
  if (!parsed.ok()) { print_forge_diagnostics(parsed.diagnostics); return false; }
  const auto verified = forge::ir::verify_module(*parsed.module);
  if (!verified.empty()) { print_forge_diagnostics(verified); return false; }
  forge::pass::PassManager pipeline;
  const auto level = optimization_level == 0 ? forge::pass::OptimizationLevel::o0
                   : optimization_level == 1 ? forge::pass::OptimizationLevel::o1
                   : optimization_level == 2 ? forge::pass::OptimizationLevel::o2
                                             : forge::pass::OptimizationLevel::o3;
  forge::pass::build_standard_pipeline(pipeline, level);
  try { (void)pipeline.run(*parsed.module, false); }
  catch (const std::exception& error) { cli_errorf("Forge optimization failed: ", error.what()); return false; }
  auto lowered = forge::machine::lower_module(*parsed.module);
  if (!lowered.ok()) { print_forge_diagnostics(lowered.diagnostics); return false; }
#if defined(_WIN32)
  auto object = forge::object::emit_coff_x86_64(*lowered.module, forge::codegen::x86_64::Abi::windows);
#else
  auto object = forge::object::emit_elf64_x86_64(*lowered.module, forge::codegen::x86_64::Abi::system_v);
#endif
  if (!object.ok()) { print_forge_diagnostics(object.diagnostics); return false; }
  if (!write_bytes(object_path, object.bytes)) {
    cli_errorf("failed to write native object ", object_path); return false;
  }
  return true;
}

std::string package_namespace(const ProjectGraph& graph) {
  return "__raz_pkg_" + hex(hash_text(graph.manifest.name + "@" + graph.manifest.version));
}

std::string module_namespace(const ProjectGraph& graph, const raz::compiler::ModuleUnit& module) {
  const auto identity = graph.manifest.name + "@" + graph.manifest.version + "::" + module.logical_name;
  const bool entry = graph.manifest.kind == raz::compiler::PackageKind::executable && module.is_entry;
  return package_namespace(graph) + std::string(entry ? "__entry_" : "__mod_") + hex(hash_text(identity));
}

const ProjectGraph* direct_dependency(const ProjectGraph& graph, std::string_view alias) {
  for (const auto& dependency : graph.dependencies)
    if (dependency.dependency_alias == alias || dependency.manifest.name == alias) return &dependency;
  return nullptr;
}

const raz::compiler::ModuleUnit* local_module(const ProjectGraph& graph, std::string_view logical_name) {
  for (const auto& module : graph.modules) {
    if (module.logical_name == logical_name || (!module.source_namespace.empty() && module.source_namespace == logical_name)) return &module;
  }
  return nullptr;
}

struct ModuleImportBinding final {
  std::string source_path;
  std::string alias;
  std::string target_namespace;
  bool reexport = false;
};

std::vector<ModuleImportBinding> module_import_bindings(const ProjectGraph& graph,
                                                        const raz::compiler::ModuleUnit& module) {
  std::vector<ModuleImportBinding> bindings;
  if (!module.source_namespace.empty())
    bindings.push_back({module.source_namespace, {}, module_namespace(graph, module), false});
  for (const auto& imported : module.imports) {
    if (const auto* local = local_module(graph, imported.path)) {
      bindings.push_back({imported.path, imported.alias, module_namespace(graph, *local), imported.reexport});
      continue;
    }
    const auto separator = imported.path.find("::");
    const auto root = separator == std::string::npos ? imported.path : imported.path.substr(0, separator);
    const auto* dependency = direct_dependency(graph, root);
    if (dependency == nullptr) continue;
    const auto suffix = separator == std::string::npos ? std::string{} : imported.path.substr(separator + 2);
    for (const auto& candidate : dependency->modules) {
      const bool match = suffix.empty() || candidate.logical_name == suffix || candidate.source_namespace == suffix ||
                         candidate.logical_name.starts_with(suffix + "::") || candidate.source_namespace.starts_with(suffix + "::");
      if (match) bindings.push_back({imported.path, imported.alias, module_namespace(*dependency, candidate), imported.reexport});
    }
  }

  std::sort(bindings.begin(), bindings.end(), [](const auto& a, const auto& b) {
    return std::tie(a.source_path, a.alias, a.target_namespace, a.reexport) <
           std::tie(b.source_path, b.alias, b.target_namespace, b.reexport);
  });
  bindings.erase(std::unique(bindings.begin(), bindings.end(), [](const auto& a, const auto& b) {
    return a.source_path == b.source_path && a.alias == b.alias && a.target_namespace == b.target_namespace &&
           a.reexport == b.reexport;
  }), bindings.end());
  return bindings;
}

void append_module_body(const raz::compiler::ModuleUnit& module, std::ofstream& output) {
  std::ifstream input(module.source_path);
  std::string line;
  while (std::getline(input, line)) {
    std::string trimmed = line;
    const auto first = trimmed.find_first_not_of(" \t");
    trimmed = first == std::string::npos ? std::string{} : trimmed.substr(first);
    if (trimmed.starts_with("import ") || trimmed.starts_with("public import ") ||
        trimmed.starts_with("private import ")) { output << std::string(line.size(), ' ') << '\n'; continue; }
    if (trimmed.starts_with("namespace ") && trimmed.ends_with(';')) { output << std::string(line.size(), ' ') << '\n'; continue; }
    output << line << '\n';
  }
}

void emit_module_import_bindings(const ProjectGraph& graph,
                                 const raz::compiler::ModuleUnit& module,
                                 std::ofstream& output) {
  for (const auto& binding : module_import_bindings(graph, module)) {
    // Do not import the module's own public/source namespace back into the
    // namespace currently being materialized. That alias is for other modules;
    // injecting it here shadows local type declarations during semantic layout
    // registration (notably named struct literals).
    if (!module.source_namespace.empty() && binding.source_path == module.source_namespace &&
        binding.target_namespace == module_namespace(graph, module) && binding.alias.empty()) continue;
    if (binding.reexport) output << "public ";
    output << "import " << binding.source_path << "::" << binding.target_namespace;
    if (!binding.alias.empty()) output << " as " << binding.alias;
    output << ";\n";
  }
}

void append_namespaced_module(const ProjectGraph& graph, const raz::compiler::ModuleUnit& module,
                              std::ofstream& output) {
  const auto ns = module_namespace(graph, module);
  output << "// module " << graph.manifest.name << "::" << module.logical_name << '\n';
  output << "namespace " << ns << " {\n";
  emit_module_import_bindings(graph, module, output);
  output << "// raz-source-begin " << module.source_path.generic_string() << '\n';
  append_module_body(module, output);
  output << "}\n";
}

bool local_import_targets_module(const ProjectGraph& graph, const raz::compiler::ImportSpec& imported,
                                 const raz::compiler::ModuleUnit& candidate) {
  if (imported.path == candidate.logical_name || imported.path == candidate.source_namespace) return true;
  // Dependency-qualified imports belong to a child package, not this package's
  // physical-module ordering graph.
  if (graph.manifest.dependencies.contains(imported.path)) return false;
  return false;
}

void append_package_modules_dependency_order(const ProjectGraph& graph, std::ofstream& output) {
  std::vector<bool> emitted(graph.modules.size(), false);
  std::size_t emitted_count = 0;
  while (emitted_count < graph.modules.size()) {
    bool progress = false;
    for (std::size_t index = 0; index < graph.modules.size(); ++index) {
      if (emitted[index]) continue;
      const auto& module = graph.modules[index];
      bool ready = true;
      for (const auto& imported : module.imports) {
        for (std::size_t dependency_index = 0; dependency_index < graph.modules.size(); ++dependency_index) {
          if (dependency_index == index || emitted[dependency_index]) continue;
          if (local_import_targets_module(graph, imported, graph.modules[dependency_index])) {
            ready = false;
            break;
          }
        }
        if (!ready) break;
      }
      if (!ready) continue;
      append_namespaced_module(graph, module, output);
      emitted[index] = true;
      ++emitted_count;
      progress = true;
    }

    if (progress) continue;

    // Import cycles are diagnosed at semantic resolution where the declarations
    // are available. Keep assembly deterministic instead of depending on the
    // recursive_directory_iterator order.
    for (std::size_t index = 0; index < graph.modules.size(); ++index) {
      if (emitted[index]) continue;
      append_namespaced_module(graph, graph.modules[index], output);
      emitted[index] = true;
      ++emitted_count;
    }
  }
}

void append_namespaced_sources(const ProjectGraph& graph, std::ofstream& output,
                               std::set<std::filesystem::path>& emitted_packages) {
  const auto root = std::filesystem::weakly_canonical(graph.manifest.root);
  if (!emitted_packages.insert(root).second) return;
  for (const auto& dependency : graph.dependencies) append_namespaced_sources(dependency, output, emitted_packages);
  append_package_modules_dependency_order(graph, output);
}

void append_legacy_sources(const ProjectGraph& graph, std::ofstream& output) {
  for (const auto& dependency : graph.dependencies) append_legacy_sources(dependency, output);
  for (const auto& module : graph.modules) append_module_body(module, output);
}

std::filesystem::path native_artifact_path(const ProjectGraph& graph, const Options& options) {
  const auto root = graph.manifest.root / "target" / options.target / options.profile;
#if defined(_WIN32)
  if (graph.manifest.kind == raz::compiler::PackageKind::executable) return root / (graph.manifest.name + ".exe");
  if (graph.manifest.kind == raz::compiler::PackageKind::static_library) return root / (graph.manifest.name + ".lib");
  return root / (graph.manifest.name + ".dll");
#else
  if (graph.manifest.kind == raz::compiler::PackageKind::executable) return root / graph.manifest.name;
  if (graph.manifest.kind == raz::compiler::PackageKind::static_library) return root / ("lib" + graph.manifest.name + ".a");
  return root / ("lib" + graph.manifest.name + ".so");
#endif
}

struct LinkInputCacheEntry final {
  std::uintmax_t size = 0;
  std::int64_t stamp = 0;
  std::string content;
};

using LinkInputCache = std::map<std::string, LinkInputCacheEntry>;

LinkInputCache read_link_input_cache(const std::filesystem::path& path) {
  LinkInputCache cache;
  std::ifstream input(path);
  std::string key;
  LinkInputCacheEntry entry;
  while (input >> std::quoted(key) >> entry.size >> entry.stamp >> entry.content)
    cache.emplace(key, entry);
  return cache;
}

void write_link_input_cache(const std::filesystem::path& path, const LinkInputCache& cache) {
  std::ofstream output(path, std::ios::trunc);
  if (!output) return;
  for (const auto& [key, entry] : cache)
    output << std::quoted(key) << ' ' << entry.size << ' ' << entry.stamp << ' ' << entry.content << '\n';
}

std::uint64_t file_content_fingerprint(const std::filesystem::path& path, std::uint64_t seed,
                                       LinkInputCache& cache) {
  if (!std::filesystem::is_regular_file(path)) return hash_text("missing:" + path.generic_string(), seed);
  std::error_code error;
  const auto size = std::filesystem::file_size(path, error);
  if (error) return hash_text("unreadable:" + path.generic_string(), seed);
  error.clear();
  const auto write_time = std::filesystem::last_write_time(path, error);
  if (error) return hash_text("unreadable:" + path.generic_string(), seed);
  const auto stamp = static_cast<std::int64_t>(write_time.time_since_epoch().count());
  const auto key = path.lexically_normal().generic_string();
  auto found = cache.find(key);
  if (found == cache.end() || found->second.size != size || found->second.stamp != stamp || found->second.content.empty()) {
    LinkInputCacheEntry entry;
    entry.size = size;
    entry.stamp = stamp;
    entry.content = hex(hash_file(path, "raz-link-input-v1"));
    found = cache.insert_or_assign(key, std::move(entry)).first;
  }

  return hash_text(found->second.content, hash_text(key, seed));
}

struct NativeObjectCacheState final {
  std::string input;
  std::string object;
};

NativeObjectCacheState read_native_object_state(const std::filesystem::path& path) {
  NativeObjectCacheState state;
  std::ifstream input(path);
  std::string line;
  while (std::getline(input, line)) {
    if (line.starts_with("input=")) state.input = line.substr(6);
    else if (line.starts_with("object=")) state.object = line.substr(7);
    else if (!line.empty() && state.input.empty()) state.input = line; // v2 compatibility
  }
  return state;
}

bool replace_file(const std::filesystem::path& source, const std::filesystem::path& destination) {
  std::error_code error;
  std::filesystem::remove(destination, error);
  error.clear();
  std::filesystem::rename(source, destination, error);
  if (!error) return true;
  error.clear();
  std::filesystem::copy_file(source, destination, std::filesystem::copy_options::overwrite_existing, error);
  if (error) return false;
  std::filesystem::remove(source, error);
  return true;
}

bool cached_native_object(const std::filesystem::path& ir_path,
                          const std::filesystem::path& object_path,
                          const std::filesystem::path& fingerprint_path,
                          unsigned optimization_level,
                          bool force,
                          std::string& fingerprint,
                          bool& rebuilt) {
  const auto input_fingerprint = hex(hash_file(
      ir_path, "raz-native-object-v3:" + std::to_string(optimization_level)));
  auto previous = read_native_object_state(fingerprint_path);
  if (!force && previous.input == input_fingerprint && std::filesystem::is_regular_file(object_path)) {
    fingerprint = previous.object.empty()
        ? hex(hash_file(object_path, "raz-native-object-content-v1"))
        : previous.object;
    rebuilt = false;
    return true;
  }

  auto temporary = object_path;
  temporary += ".tmp";
  std::error_code cleanup_error;
  std::filesystem::remove(temporary, cleanup_error);
  if (!emit_native_object(ir_path, temporary, optimization_level)) return false;
  const auto emitted_fingerprint = hex(hash_file(temporary, "raz-native-object-content-v1"));

  bool object_changed = true;
  if (std::filesystem::is_regular_file(object_path)) {
    const auto existing_fingerprint = hex(hash_file(object_path, "raz-native-object-content-v1"));
    object_changed = existing_fingerprint != emitted_fingerprint;
  }

  if (object_changed) {
    if (!replace_file(temporary, object_path)) {
      cli_errorf("failed to replace native object ", object_path);
      return false;
    }
  } else {
    std::filesystem::remove(temporary, cleanup_error);
  }

  std::ofstream output(fingerprint_path, std::ios::trunc);
  if (!output) return false;
  output << "raz-native-object-v3\n"
         << "input=" << input_fingerprint << '\n'
         << "object=" << emitted_fingerprint << '\n';
  fingerprint = emitted_fingerprint;
  rebuilt = object_changed;
  return true;
}

void collect_native_dependency_artifacts(const ProjectGraph& graph, const Options& options,
                                         std::vector<std::filesystem::path>& inputs) {
  for (const auto& dependency : graph.dependencies) {
    const auto artifact = native_artifact_path(dependency, options);
    if (std::filesystem::exists(artifact)) inputs.push_back(artifact);
    collect_native_dependency_artifacts(dependency, options, inputs);
  }
}

void collect_native_link_requirements(const ProjectGraph& graph,
                                      std::vector<std::string>& libraries,
                                      std::vector<std::filesystem::path>& library_paths) {
  for (const auto& dependency : graph.dependencies)
    collect_native_link_requirements(dependency, libraries, library_paths);
  for (const auto& path : graph.manifest.native_library_paths)
    if (std::find(library_paths.begin(), library_paths.end(), path) == library_paths.end())
      library_paths.push_back(path);
  for (const auto& library : graph.manifest.native_libraries)
    if (std::find(libraries.begin(), libraries.end(), library) == libraries.end())
      libraries.push_back(library);
}

struct FirFunctionDefinition final {
  std::string name;
  std::string declaration;
  std::string canonical;
  std::size_t begin_line = 0;
  std::size_t end_line = 0;
  bool internal = false;
};

std::string canonicalize_fir_function(std::string text) {
  std::unordered_map<std::string, std::string> registers;
  std::size_t next_register = 0;
  std::string output;
  output.reserve(text.size());
  for (std::size_t index = 0; index < text.size();) {
    if (text[index] == '%' && index + 2 < text.size() && text[index + 1] == 'v' &&
        std::isdigit(static_cast<unsigned char>(text[index + 2]))) {
      std::size_t finish = index + 2;
      while (finish < text.size() && std::isdigit(static_cast<unsigned char>(text[finish]))) ++finish;
      const auto original = text.substr(index, finish - index);
      auto [it, inserted] = registers.emplace(original, std::string{});
      if (inserted) it->second = "%r" + std::to_string(next_register++);
      output += it->second;
      index = finish;
      continue;
    }

    if (std::isspace(static_cast<unsigned char>(text[index]))) {
      if (!output.empty() && output.back() != ' ') output.push_back(' ');
      ++index;
      continue;
    }
    output.push_back(text[index++]);
  }

  while (!output.empty() && output.back() == ' ') output.pop_back();
  return output;
}

std::vector<FirFunctionDefinition> fir_function_definitions(const std::vector<std::string>& lines) {
  std::vector<FirFunctionDefinition> definitions;
  for (std::size_t line_index = 0; line_index < lines.size(); ++line_index) {
    const auto& line = lines[line_index];
    const auto first = line.find_first_not_of(" \t");
    if (first == std::string::npos) continue;
    const auto trimmed = line.substr(first);
    if (trimmed.starts_with("extern ") || trimmed.starts_with("signature ")) continue;
    const auto function_at = trimmed.find("func @");
    if (function_at == std::string::npos) continue;
    const auto symbol_start = function_at + 6;
    const auto symbol_end = trimmed.find('(', symbol_start);
    const auto open = trimmed.find('{', symbol_end);
    if (symbol_end == std::string::npos || open == std::string::npos) continue;
    int depth = 0;
    std::string body;
    std::size_t end_line = line_index;
    for (std::size_t cursor = line_index; cursor < lines.size(); ++cursor) {
      body += lines[cursor];
      body.push_back('\n');
      for (const char ch : lines[cursor]) {
        if (ch == '{') ++depth;
        else if (ch == '}') --depth;
      }
      end_line = cursor;
      if (depth == 0) break;
    }
    auto declaration = line.substr(0, first) + trimmed.substr(0, open);
    while (!declaration.empty() && std::isspace(static_cast<unsigned char>(declaration.back()))) declaration.pop_back();
    definitions.push_back(FirFunctionDefinition{
        trimmed.substr(symbol_start, symbol_end - symbol_start), std::move(declaration),
        canonicalize_fir_function(std::move(body)), line_index, end_line,
        trimmed.substr(0, function_at).find("internal") != std::string::npos});
    line_index = end_line;
  }
  return definitions;
}

std::string external_fir_declaration(const FirFunctionDefinition& definition) {
  auto declaration = definition.declaration;
  const auto first = declaration.find_first_not_of(" \t");
  const auto function_at = declaration.find("func @", first);
  if (function_at == std::string::npos) return declaration;
  declaration.insert(function_at, "extern ");
  declaration.push_back('\n');
  return declaration;
}

struct NativeIrOwnership final {
  bool ok = true;
  bool requires_aggregate = false;
  std::size_t coalesced_definitions = 0;
  std::vector<std::filesystem::path> paths;
};

bool native_ir_has_duplicate_definitions(const std::vector<std::filesystem::path>& paths) {
  std::set<std::string> definitions;
  for (const auto& path : paths) {
    std::ifstream input(path);
    std::string line;
    while (std::getline(input, line)) {
      const auto first = line.find_first_not_of(" \t");
      if (first == std::string::npos) continue;
      auto trimmed = line.substr(first);
      if (trimmed.starts_with("extern ") || trimmed.starts_with("signature ")) continue;
      std::size_t symbol_start = std::string::npos;
      if (const auto function_at = trimmed.find("func @"); function_at != std::string::npos)
        symbol_start = function_at + 6;
      else if (const auto global_at = trimmed.find("global @"); global_at != std::string::npos)
        symbol_start = global_at + 8;
      if (symbol_start == std::string::npos) continue;
      const auto symbol_end = trimmed.find_first_of("( :={\t", symbol_start);
      const auto symbol = trimmed.substr(symbol_start, symbol_end - symbol_start);
      if (!definitions.insert(symbol).second) return true;
    }
  }
  return false;
}

NativeIrOwnership prepare_native_ir_ownership(
    const std::vector<std::filesystem::path>& source_paths,
    const std::vector<std::string>& stems,
    const std::filesystem::path& output_root) {
  NativeIrOwnership result;
  if (source_paths.size() != stems.size()) { result.ok = false; return result; }
  std::filesystem::create_directories(output_root);

  struct ModuleText final {
    std::filesystem::path source;
    std::filesystem::path output;
    std::vector<std::string> lines;
    std::vector<FirFunctionDefinition> functions;
  };
  std::vector<ModuleText> modules;
  modules.reserve(source_paths.size());
  std::unordered_map<std::string, std::vector<std::pair<std::size_t, std::size_t>>> occurrences;
  for (std::size_t module_index = 0; module_index < source_paths.size(); ++module_index) {
    std::ifstream input(source_paths[module_index]);
    if (!input) { result.ok = false; return result; }
    ModuleText module;
    module.source = source_paths[module_index];
    module.output = output_root / (stems[module_index] + ".owned.fir");
    std::string line;
    while (std::getline(input, line)) module.lines.push_back(line);
    module.functions = fir_function_definitions(module.lines);
    for (std::size_t function_index = 0; function_index < module.functions.size(); ++function_index)
      occurrences[module.functions[function_index].name].push_back({module_index, function_index});
    modules.push_back(std::move(module));
  }

  std::vector<std::unordered_map<std::size_t, std::string>> replacements(modules.size());
  for (const auto& [name, defs] : occurrences) {
    if (defs.size() < 2) continue;
    const auto& first = modules[defs.front().first].functions[defs.front().second];
    bool equivalent = !first.internal;
    for (std::size_t index = 1; equivalent && index < defs.size(); ++index) {
      const auto& candidate = modules[defs[index].first].functions[defs[index].second];
      equivalent = !candidate.internal && candidate.canonical == first.canonical;
    }

    if (!equivalent) {
      result.requires_aggregate = true;
      continue;
    }
    auto owner = defs.front();
    for (const auto& candidate : defs) {
      if (modules[candidate.first].source.generic_string() < modules[owner.first].source.generic_string()) owner = candidate;
    }

    for (const auto& occurrence : defs) {
      if (occurrence == owner) continue;
      const auto& definition = modules[occurrence.first].functions[occurrence.second];
      replacements[occurrence.first][definition.begin_line] = external_fir_declaration(definition);
      for (std::size_t line = definition.begin_line + 1; line <= definition.end_line; ++line)
        replacements[occurrence.first][line] = {};
      ++result.coalesced_definitions;
    }
  }

  for (std::size_t module_index = 0; module_index < modules.size(); ++module_index) {
    std::ofstream output(modules[module_index].output, std::ios::trunc);
    if (!output) { result.ok = false; return result; }
    for (std::size_t line_index = 0; line_index < modules[module_index].lines.size(); ++line_index) {
      if (const auto found = replacements[module_index].find(line_index); found != replacements[module_index].end()) {
        output << found->second;
      } else {
        output << modules[module_index].lines[line_index] << '\n';
      }
    }
    result.paths.push_back(modules[module_index].output);
  }
  return result;
}

bool build_aggregate_native_artifact(const ProjectGraph& graph, const Options& options,
                                     const BuildProfile& profile) {
  const auto output_root = graph.manifest.root / "target" / options.target / options.profile;
  const auto native_root = output_root / "native" / "aggregate";
  std::filesystem::create_directories(native_root);
  const auto combined_source = native_root / "package.rz";
  const auto combined_ir = native_root / "package.fir";
#if defined(_WIN32)
  const auto object = native_root / "package.obj";
#else
  const auto object = native_root / "package.o";
#endif
  const auto compile_state = native_root / "compile.fingerprint";
  {
    std::ofstream output(combined_source, std::ios::trunc);
    if (std::filesystem::is_regular_file(graph.manifest.root / "source-order.txt")) {
      append_legacy_sources(graph, output);
    } else {
      std::set<std::filesystem::path> emitted_packages;
      append_namespaced_sources(graph, output, emitted_packages);
    }
  }
  const auto compile_fingerprint = hex(hash_file(
      combined_source, "raz-aggregate-native-v2:" + options.target + ":" + options.profile + ":" +
      std::to_string(profile.optimization_level)));
  std::string previous;
  { std::ifstream input(compile_state); input >> previous; }
  bool rebuilt = options.force || previous != compile_fingerprint || !std::filesystem::is_regular_file(object);
  if (rebuilt) {
    SessionOptions session;
    session.input = combined_source;
    session.emit_forge_ir = true;
    session.output = combined_ir;
    session.target_triple = options.target;
    session.optimization_level = profile.optimization_level;
    session.suppress_success_output = true;
    if (Compiler{}.run(Session(std::move(session))) != 0) return false;
    if (!emit_native_object(combined_ir, object, profile.optimization_level)) return false;
    std::ofstream state(compile_state, std::ios::trunc);
    if (!state) return false;
    state << compile_fingerprint << '\n';
  }
  const auto artifact = native_artifact_path(graph, options);
  if (graph.manifest.kind == raz::compiler::PackageKind::static_library) {
    const std::vector<std::filesystem::path> aggregate_objects{object};
    auto archive = forge::object::emit_static_archive_from_files(aggregate_objects);
    if (!archive.ok()) { print_forge_diagnostics(archive.diagnostics); return false; }
    return write_bytes(artifact, archive.bytes);
  }
  std::vector<std::filesystem::path> inputs{object};
  const auto aggregate_command = native_link_command(
      inputs, artifact, graph.manifest.kind == raz::compiler::PackageKind::shared_library);
  const auto link_input_state_path = native_root / "link-inputs.state";
  auto link_input_cache = read_link_input_cache(link_input_state_path);
  auto link_hash = hash_text("raz-aggregate-link-v4");
  link_hash = hash_text(aggregate_command, link_hash);
  link_hash = file_content_fingerprint(object, link_hash, link_input_cache);
#ifdef RAZ_RUNTIME_LIBRARY_PATH
  link_hash = file_content_fingerprint(std::filesystem::path(RAZ_RUNTIME_LIBRARY_PATH), link_hash, link_input_cache);
#endif
#ifdef RAZ_FORGE_BRIDGE_LIBRARY_PATH
  link_hash = file_content_fingerprint(std::filesystem::path(RAZ_FORGE_BRIDGE_LIBRARY_PATH), link_hash, link_input_cache);
#endif
#ifdef RAZ_FORGE_LIBRARY_PATH
  link_hash = file_content_fingerprint(std::filesystem::path(RAZ_FORGE_LIBRARY_PATH), link_hash, link_input_cache);
#endif
#ifdef RAZ_OPENSSL_SSL_LIBRARY_PATH
  link_hash = file_content_fingerprint(std::filesystem::path(RAZ_OPENSSL_SSL_LIBRARY_PATH), link_hash, link_input_cache);
#endif
#ifdef RAZ_OPENSSL_CRYPTO_LIBRARY_PATH
  link_hash = file_content_fingerprint(std::filesystem::path(RAZ_OPENSSL_CRYPTO_LIBRARY_PATH), link_hash, link_input_cache);
#endif
  const auto link_fingerprint = hex(link_hash);
  write_link_input_cache(link_input_state_path, link_input_cache);
  const auto link_state_path = native_root / "link.fingerprint";
  std::string previous_link;
  { std::ifstream input(link_state_path); input >> previous_link; }
  if (!options.force && previous_link == link_fingerprint && std::filesystem::is_regular_file(artifact)) {
    if (options.verbose) cli_status("Fresh", graph.manifest.name + " aggregate native link", raz::terminal::cyan);
    return true;
  }

  if (execute_shell_command(aggregate_command) != 0) {
    cli_error("native linker failed; set RAZ_LINKER to a compatible C++ driver");
    return false;
  }

  std::ofstream link_state(link_state_path, std::ios::trunc);
  if (link_state) link_state << link_fingerprint << '\n';
  return std::filesystem::exists(artifact);
}

bool build_native_artifact(const ProjectGraph& graph, const Options& options, const BuildProfile& profile) {
  if (options.target != "host" && options.target != "test-host") {
    cli_error("native emission currently requires --target host; cross-target project layout remains available");
    return false;
  }
  const auto output_root = graph.manifest.root / "target" / options.target / options.profile;
  const auto module_root = output_root / "modules";
  const auto native_root = output_root / "native";
  const auto object_root = native_root / "modules";
  std::filesystem::create_directories(object_root);

  std::vector<std::filesystem::path> ir_paths;
  std::vector<std::string> stems;
  if (std::filesystem::is_regular_file(graph.manifest.root / "source-order.txt")) {
    ir_paths.push_back(module_root / "ordered-package.fir");
    stems.push_back("ordered-package");
  } else {
    for (const auto& module : graph.modules) {
      ir_paths.push_back(module_root / (sanitize(module.logical_name) + ".fir"));
      stems.push_back(sanitize(module.logical_name));
    }
  }

  const auto ownership = prepare_native_ir_ownership(ir_paths, stems, native_root / "owned-ir");
  if (!ownership.ok) {
    cli_error("unable to prepare native module symbol ownership");
    return false;
  }

  if (ownership.requires_aggregate) {
    if (options.verbose) cli_status("Fallback", graph.manifest.name + " aggregate native object (conflicting shared definitions)", raz::terminal::yellow);
    return build_aggregate_native_artifact(graph, options, profile);
  }

  if (native_ir_has_duplicate_definitions(ownership.paths)) {
    if (options.verbose) cli_status("Fallback", graph.manifest.name + " aggregate native object (remaining duplicate definitions)", raz::terminal::yellow);
    return build_aggregate_native_artifact(graph, options, profile);
  }

  if (options.verbose && ownership.coalesced_definitions != 0)
    cli_status("Owned", std::to_string(ownership.coalesced_definitions) + " shared definition(s) across modules", raz::terminal::magenta);
  ir_paths = ownership.paths;

  struct ObjectResult final {
    bool ok = false;
    bool rebuilt = false;
    std::filesystem::path object;
    std::string fingerprint;
  };
  std::vector<std::future<ObjectResult>> jobs;
  jobs.reserve(ir_paths.size());
  for (std::size_t index = 0; index < ir_paths.size(); ++index) {
    const auto ir = ir_paths[index];
#if defined(_WIN32)
    const auto object = object_root / (stems[index] + ".obj");
#else
    const auto object = object_root / (stems[index] + ".o");
#endif
    const auto fingerprint_path = object_root / (stems[index] + ".object.fingerprint");
    jobs.push_back(std::async(std::launch::async, [=]() {
      ObjectResult result;
      result.object = object;
      result.ok = std::filesystem::is_regular_file(ir) &&
          cached_native_object(ir, object, fingerprint_path, profile.optimization_level,
                               options.force, result.fingerprint, result.rebuilt);
      return result;
    }));
  }

  std::vector<std::filesystem::path> objects;
  std::vector<std::string> object_fingerprints;
  bool any_object_changed = false;
  for (auto& job : jobs) {
    auto result = job.get();
    if (!result.ok) return false;
    any_object_changed = any_object_changed || result.rebuilt;
    objects.push_back(std::move(result.object));
    object_fingerprints.push_back(std::move(result.fingerprint));
  }

  const auto artifact = native_artifact_path(graph, options);
  if (graph.manifest.kind == raz::compiler::PackageKind::static_library) {
    auto archive_hash = hash_text("raz-native-archive-v1");
    for (const auto& fingerprint : object_fingerprints) archive_hash = hash_text(fingerprint, archive_hash);
    const auto archive_fingerprint = hex(archive_hash);
    const auto archive_state_path = native_root / "archive.fingerprint";
    std::string previous_archive;
    { std::ifstream input(archive_state_path); input >> previous_archive; }
    if (!options.force && !any_object_changed && previous_archive == archive_fingerprint &&
        std::filesystem::is_regular_file(artifact)) {
      if (options.verbose) cli_status("Fresh", graph.manifest.name + " native archive", raz::terminal::cyan);
      return true;
    }
    auto archive = forge::object::emit_static_archive_from_files(objects);
    if (!archive.ok()) { print_forge_diagnostics(archive.diagnostics); return false; }
    const auto temporary = artifact.string() + ".tmp";
    if (!write_bytes(temporary, archive.bytes)) return false;
    const auto temporary_path = std::filesystem::path(temporary);
    bool changed = true;
    if (std::filesystem::is_regular_file(artifact))
      changed = hash_file(temporary_path, "raz-native-archive-content-v1") !=
                hash_file(artifact, "raz-native-archive-content-v1");
    if (changed) {
      if (!replace_file(temporary_path, artifact)) return false;
    } else {
      std::error_code error;
      std::filesystem::remove(temporary_path, error);
    }

    std::ofstream archive_state(archive_state_path, std::ios::trunc);
    if (archive_state) archive_state << archive_fingerprint << '\n';
    return std::filesystem::is_regular_file(artifact);
  }

  std::vector<std::filesystem::path> link_inputs = objects;
  collect_native_dependency_artifacts(graph, options, link_inputs);
  std::vector<std::string> native_libraries;
  std::vector<std::filesystem::path> native_library_paths;
  collect_native_link_requirements(graph, native_libraries, native_library_paths);
  const std::string command = native_link_command(
      link_inputs, artifact, graph.manifest.kind == raz::compiler::PackageKind::shared_library,
      native_libraries, native_library_paths);
  const auto link_input_state_path = native_root / "link-inputs.state";
  auto link_input_cache = read_link_input_cache(link_input_state_path);
  auto link_hash = hash_text("raz-native-link-v4");
  link_hash = hash_text(command, link_hash);
  for (const auto& fingerprint : object_fingerprints) link_hash = hash_text(fingerprint, link_hash);
  for (std::size_t index = objects.size(); index < link_inputs.size(); ++index)
    link_hash = file_content_fingerprint(link_inputs[index], link_hash, link_input_cache);
#ifdef RAZ_RUNTIME_LIBRARY_PATH
  link_hash = file_content_fingerprint(std::filesystem::path(RAZ_RUNTIME_LIBRARY_PATH), link_hash, link_input_cache);
#endif
#ifdef RAZ_FORGE_BRIDGE_LIBRARY_PATH
  link_hash = file_content_fingerprint(std::filesystem::path(RAZ_FORGE_BRIDGE_LIBRARY_PATH), link_hash, link_input_cache);
#endif
#ifdef RAZ_FORGE_LIBRARY_PATH
  link_hash = file_content_fingerprint(std::filesystem::path(RAZ_FORGE_LIBRARY_PATH), link_hash, link_input_cache);
#endif
#ifdef RAZ_OPENSSL_SSL_LIBRARY_PATH
  link_hash = file_content_fingerprint(std::filesystem::path(RAZ_OPENSSL_SSL_LIBRARY_PATH), link_hash, link_input_cache);
#endif
#ifdef RAZ_OPENSSL_CRYPTO_LIBRARY_PATH
  link_hash = file_content_fingerprint(std::filesystem::path(RAZ_OPENSSL_CRYPTO_LIBRARY_PATH), link_hash, link_input_cache);
#endif
  const auto link_fingerprint = hex(link_hash);
  write_link_input_cache(link_input_state_path, link_input_cache);
  const auto link_fingerprint_path = native_root / "link.fingerprint";
  std::string previous_link;
  { std::ifstream input(link_fingerprint_path); input >> previous_link; }
  if (!options.force && previous_link == link_fingerprint &&
      std::filesystem::is_regular_file(artifact)) {
    if (options.verbose) cli_status("Fresh", graph.manifest.name + " native link", raz::terminal::cyan);
    return true;
  }

  if (execute_shell_command(command) != 0) {
    cli_error("native linker failed; set RAZ_LINKER to a compatible C++ driver");
    return false;
  }

  std::ofstream link_state(link_fingerprint_path, std::ios::trunc);
  if (link_state) link_state << link_fingerprint << '\n';
  return std::filesystem::exists(artifact);
}

bool write_interface(const raz::compiler::ModuleUnit& module, const std::filesystem::path& path,
                     const raz::compiler::Manifest& manifest,
                     std::uint64_t package_interface_hash,
                     std::uint64_t module_interface_hash) {
  std::ofstream out(path, std::ios::trunc);
  if (!out) return false;
  out << "raz-interface-v5\npackage=" << manifest.name << '\n'
      << "version=" << manifest.version << '\n' << "module=" << module.logical_name << '\n'
      << "qualified_module=" << manifest.name << "::" << module.logical_name << '\n'
      << "source_namespace=" << module.source_namespace << '\n'
      << "namespace=" << "__raz_pkg_" + hex(hash_text(manifest.name + "@" + manifest.version)) +
             std::string((manifest.kind == raz::compiler::PackageKind::executable && module.is_entry) ? "__entry_" : "__mod_") +
             hex(hash_text(manifest.name + "@" + manifest.version + "::" + module.logical_name)) << '\n'
      << "package_interface_hash=" << hex(package_interface_hash) << '\n'
      << "module_interface_hash=" << hex(module_interface_hash) << '\n';
  for (const auto& imported : module.imports) {
    out << (imported.reexport ? "reexport=" : "import=") << imported.path;
    if (!imported.alias.empty()) out << " as " << imported.alias;
    out << '\n';
  }
  const auto exported = exported_semantic_declarations(module.source_path);
  const std::set<std::string> exported_set(exported.begin(), exported.end());
  for (const auto& item : exported) {
    const auto body = semantic_body(item);
    if (body.starts_with("trait ")) out << "trait=" << item << '\n';
    else if (body.starts_with("impl<") || body.starts_with("impl ")) out << "impl=" << item << '\n';
    else if (body.starts_with("fn ") || body.starts_with("async fn ") || body.starts_with("unsafe fn ") ||
             body.starts_with("unsafe async fn ") || body.starts_with("const fn ") || body.starts_with("extern fn "))
      out << "function=" << item << '\n';
    else if (body.starts_with("const ")) out << "constant=" << item << '\n';
    else out << "type=" << item << '\n';
    out << "semantic=" << hex_encode(item) << '\n';
  }

  // Exported generic implementation bodies are specialized in the consuming
  // package. They may call package-internal helpers whose signatures are not
  // part of the public API. Preserve those declarations as hidden semantic
  // closure: normal visibility rules still reject direct cross-package access,
  // while generic bodies can resolve their own module/package dependencies.
  // Hidden declarations stay encoded and retain package visibility. Emitting
  // the full package semantic surface lets exported generic bodies resolve
  // helpers imported from sibling modules; the package fingerprint above only
  // includes the subset actually referenced by exported generic impls.
  for (const auto& item : package_semantic_declarations(module.source_path)) {
    if (exported_set.contains(item)) continue;
    out << "hidden_semantic=" << hex_encode(item) << '\n';
  }
  return true;
}

std::vector<std::string> read_semantic_interface(const std::filesystem::path& path) {
  std::ifstream input(path);
  std::vector<std::string> declarations;
  std::string line;
  bool version_ok = false;
  while (std::getline(input, line)) {
    if (line == "raz-interface-v5" || line == "raz-interface-v4" || line == "raz-interface-v3" || line == "raz-interface-v2") { version_ok = true; continue; }
    if (!version_ok) continue;
    std::string_view encoded;
    if (line.starts_with("semantic=")) encoded = std::string_view(line).substr(9);
    else if (line.starts_with("hidden_semantic=")) encoded = std::string_view(line).substr(16);
    else continue;
    const auto decoded = hex_decode(encoded);
    if (decoded.has_value()) declarations.push_back(*decoded);
  }
  return declarations;
}

void collect_dependency_semantics(const ProjectGraph& graph, const Options& options,
                                  std::vector<std::string>& declarations) {
  for (const auto& dependency : graph.dependencies) {
    collect_dependency_semantics(dependency, options, declarations);
    const auto module_root = dependency.manifest.root / "target" / options.target /
                             options.profile / "modules";
    for (const auto& module : dependency.modules) {
      const auto interface_path = module_root / (sanitize(module.logical_name) + ".dmi");
      const auto imported = read_semantic_interface(interface_path);
      declarations.insert(declarations.end(), imported.begin(), imported.end());
    }
  }

  std::sort(declarations.begin(), declarations.end());
  declarations.erase(std::unique(declarations.begin(), declarations.end()), declarations.end());
}

void write_namespaced_dependency_semantics(const ProjectGraph& graph, const Options& options,
                                           std::ofstream& output) {
  for (const auto& dependency : graph.dependencies) {
    write_namespaced_dependency_semantics(dependency, options, output);
    const auto module_root = dependency.manifest.root / "target" / options.target /
                             options.profile / "modules";
    for (const auto& dependency_module : dependency.modules) {
      const auto interface_path = module_root / (sanitize(dependency_module.logical_name) + ".dmi");
      const auto declarations = read_semantic_interface(interface_path);
      if (declarations.empty() && dependency_module.imports.empty()) continue;
      output << "namespace " << module_namespace(dependency, dependency_module) << " {\n";
      emit_module_import_bindings(dependency, dependency_module, output);
      for (const auto& declaration : declarations) output << declaration << "\n";
      output << "}\n";
    }
  }
}

void write_local_semantic_module(const ProjectGraph& graph,
                                 const raz::compiler::ModuleUnit& module,
                                 std::ofstream& output,
                                 std::set<std::filesystem::path>& emitted) {
  const auto canonical = std::filesystem::weakly_canonical(module.source_path);
  if (!emitted.insert(canonical).second) return;

  // A module interface can itself mention types/traits from modules it imports.
  // Emit those prerequisites first so the single-pass semantic analyzer sees
  // their declarations before dependent interface declarations. Do not inject
  // unrelated sibling modules: namespace/package visibility still requires an
  // explicit import edge.
  for (const auto& imported : module.imports) {
    if (const auto* local = local_module(graph, imported.path))
      write_local_semantic_module(graph, *local, output, emitted);
  }

  const auto declarations = package_semantic_declarations(module.source_path);
  if (declarations.empty() && module.imports.empty()) return;
  output << "namespace " << module_namespace(graph, module) << " {\n";
  emit_module_import_bindings(graph, module, output);
  for (const auto& declaration : declarations) output << declaration << "\n";
  output << "}\n";
}

struct SemanticInputFile final {
  std::filesystem::path path;
  std::filesystem::path display_path;
  std::int64_t line_delta = 0;
  std::int64_t byte_delta = 0;
};

SemanticInputFile semantic_input_for_module(const ProjectGraph& graph,
                                            const raz::compiler::ModuleUnit& module,
                                            const Options& options,
                                            const std::filesystem::path& cache_root) {
  const auto generated = cache_root / (sanitize(module.logical_name) + ".semantic.rz");
  {
    std::ofstream output(generated, std::ios::trunc);
    write_namespaced_dependency_semantics(graph, options, output);

    std::set<std::filesystem::path> emitted;
    for (const auto& imported : module.imports) {
      if (const auto* local = local_module(graph, imported.path))
        write_local_semantic_module(graph, *local, output, emitted);
    }

    append_namespaced_module(graph, module, output);
  }

  std::ifstream input(generated, std::ios::binary);
  const std::string text((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
  const std::string marker = "// raz-source-begin " + module.source_path.generic_string() + "\n";
  const auto marker_position = text.rfind(marker);
  std::int64_t line_delta = 0;
  std::int64_t byte_delta = 0;
  if (marker_position != std::string::npos) {
    const auto body_position = marker_position + marker.size();
    const auto generated_body_line = static_cast<std::int64_t>(
        std::count(text.begin(), text.begin() + static_cast<std::ptrdiff_t>(body_position), '\n') + 1);
    line_delta = 1 - generated_body_line;
    byte_delta = -static_cast<std::int64_t>(body_position);
  }
  return SemanticInputFile{generated, module.source_path, line_delta, byte_delta};
}

bool write_ordered_semantic_source(const ProjectGraph& graph, const Options& options,
                                   const std::filesystem::path& output_path) {
  std::vector<std::string> declarations;
  collect_dependency_semantics(graph, options, declarations);

  std::ofstream output(output_path, std::ios::binary | std::ios::trunc);
  if (!output) {
    return false;
  }

  for (const auto& declaration : declarations) {
    output << declaration << '\n';
  }

  // `source-order.txt` means these physical files form one compilation unit.
  // Preserve that order and strip package imports because dependency interfaces
  // have already been materialized above.
  for (const auto& module : graph.modules) {
    std::ifstream source(module.source_path, std::ios::binary);
    std::string line;
    while (std::getline(source, line)) {
      std::string trimmed = line;
      trimmed.erase(0, trimmed.find_first_not_of(" \t"));
      if (trimmed.starts_with("import ") || trimmed.starts_with("public import ") ||
          trimmed.starts_with("private import ")) {
        continue;
      }
      output << line << '\n';
    }
  }

  return static_cast<bool>(output);
}

std::string read_text_file(const std::filesystem::path& path);

std::string build_package_display(const ProjectGraph& graph) {
  std::string text = graph.manifest.name;
  if (!graph.manifest.version.empty()) text += " v" + graph.manifest.version;
  return text;
}

bool build_ordered_compilation_unit(const ProjectGraph& graph, const Options& options,
                                    const BuildProfile& profile,
                                    const std::filesystem::path& module_root,
                                    const std::filesystem::path& cache_root,
                                    std::uint64_t package_interface_hash, bool check_only,
                                    std::size_t& compiled, std::size_t& fresh,
                                    std::vector<std::string>* diagnostic_reports) {
  const auto source_path = cache_root / "ordered-package.rz";
  const auto ir_path = module_root / "ordered-package.fir";
  const auto fingerprint_path = cache_root / "ordered-package.fingerprint";

  if (!write_ordered_semantic_source(graph, options, source_path)) {
    cli_errorf("failed to materialize ordered package source ", source_path);
    return false;
  }

  const auto fingerprint = hex(hash_file(
      source_path,
      options.target + options.profile + std::to_string(profile.optimization_level) +
          hex(package_interface_hash)));

  bool interfaces_exist = true;
  for (const auto& module : graph.modules) {
    const auto interface_path = module_root / (sanitize(module.logical_name) + ".dmi");
    interfaces_exist = interfaces_exist && std::filesystem::exists(interface_path);
  }

  std::string previous;
  {
    std::ifstream input(fingerprint_path);
    input >> previous;
  }

  if (!options.force && profile.incremental && previous == fingerprint && interfaces_exist &&
      (check_only || std::filesystem::exists(ir_path))) {
    ++fresh;
    if (options.verbose) {
      cli_status("Fresh", graph.manifest.name + " (ordered compilation unit)", raz::terminal::cyan);
    }
    return true;
  }

  cli_status(check_only ? "Checking" : "Compiling", build_package_display(graph),
             check_only ? raz::terminal::cyan : raz::terminal::green);
  if (options.verbose)
    cli_status("Unit", graph.manifest.name + " (ordered compilation unit)", raz::terminal::cyan);

  SessionOptions session;
  session.input = source_path;
  session.check_only = check_only;
  session.target_triple = options.target;
  session.optimization_level = profile.optimization_level;
  session.diagnostic_format = options.diagnostic_format;
  session.diagnostic_policy = options.diagnostic_policy;
  session.suppress_success_output = true;
  if (check_only && diagnostic_reports != nullptr && options.diagnostic_format == DiagnosticFormat::json) {
    session.diagnostic_output = cache_root / "ordered-package.diagnostics.json";
    session.suppress_success_output = true;
  }

  if (!check_only) {
    session.emit_forge_ir = true;
    session.output = ir_path;
  }
  const auto diagnostic_report_path = session.diagnostic_output;
  const int compiler_status = Compiler{}.run(Session(std::move(session)));
  if (diagnostic_reports != nullptr && !diagnostic_report_path.empty() && std::filesystem::exists(diagnostic_report_path)) {
    diagnostic_reports->push_back(read_text_file(diagnostic_report_path));
  }

  if (compiler_status != 0) return false;

  // Keep one interface per physical module so downstream packages retain the
  // same import metadata shape as ordinary independently compiled packages.
  for (const auto& module : graph.modules) {
    const auto interface_path = module_root / (sanitize(module.logical_name) + ".dmi");
    if (!write_interface(module, interface_path, graph.manifest, package_interface_hash,
                         module_interface_fingerprint(graph, module, package_interface_hash))) {
      cli_errorf("failed to write interface ", interface_path);
      return false;
    }
  }

  {
    std::ofstream output(fingerprint_path, std::ios::trunc);
    output << fingerprint << '\n';
  }
  ++compiled;
  return true;
}

bool build_graph_impl(const ProjectGraph& graph, const Options& options, bool check_only,
                 std::set<std::filesystem::path>& built, std::size_t& compiled,
                 std::size_t& fresh, std::vector<std::string>* diagnostic_reports,
                 const raz::compiler::WorkspaceGraph& workspace,
                 const std::map<std::string, std::uint64_t>& interface_fingerprints) {
  const auto canonical = std::filesystem::weakly_canonical(graph.manifest.root);
  if (!built.insert(canonical).second) {
    return true;
  }

  for (const auto& dependency : graph.dependencies) {
    if (!build_graph_impl(dependency, options, check_only, built, compiled, fresh, diagnostic_reports,
                          workspace, interface_fingerprints)) {
      return false;
    }
  }

  const auto profile_it = graph.manifest.profiles.find(options.profile);
  if (profile_it == graph.manifest.profiles.end()) {
    cli_errorf("profile '", options.profile, "' is not defined in ", graph.manifest.root / "raz.toml");
    return false;
  }

  const BuildProfile& profile = profile_it->second;
  const auto output_root = graph.manifest.root / "target" / options.target / options.profile;
  const auto module_root = output_root / "modules";
  const auto cache_root = graph.manifest.root / ".raz" / "cache" / options.target / options.profile;
  std::filesystem::create_directories(module_root);
  std::filesystem::create_directories(cache_root);

  const auto package_interface_hash = public_interface_fingerprint(graph);
  const bool ordered_compilation_unit =
      std::filesystem::is_regular_file(graph.manifest.root / "source-order.txt");

  if (ordered_compilation_unit) {
    if (!build_ordered_compilation_unit(graph, options, profile, module_root, cache_root,
                                        package_interface_hash, check_only, compiled, fresh, diagnostic_reports)) {
      return false;
    }
  } else {
    struct PendingModuleCompile final {
      const raz::compiler::ModuleUnit* module = nullptr;
      std::filesystem::path interface_path;
      std::filesystem::path fingerprint_path;
      std::filesystem::path stage_cache_path;
      std::filesystem::path diagnostic_report_path;
      std::string fingerprint;
      std::uint64_t module_interface_hash = 0;
      std::future<int> status;
    };
    std::deque<PendingModuleCompile> active;
    bool package_announced = false;

    auto finish_front = [&]() -> bool {
      if (active.empty()) return true;
      auto pending = std::move(active.front());
      active.pop_front();
      const int compiler_status = pending.status.get();
      if (diagnostic_reports != nullptr && !pending.diagnostic_report_path.empty() &&
          std::filesystem::exists(pending.diagnostic_report_path)) {
        diagnostic_reports->push_back(read_text_file(pending.diagnostic_report_path));
      }
      if (compiler_status != 0) return false;
      if (!write_interface(*pending.module, pending.interface_path, graph.manifest,
                           package_interface_hash, pending.module_interface_hash)) {
        cli_errorf("failed to write interface ", pending.interface_path);
        return false;
      }
      {
        std::ofstream output(pending.fingerprint_path, std::ios::trunc);
        output << pending.fingerprint << '\n';
      }
      if (const auto key = workspace.key_for_path(pending.module->source_path); key.has_value()) {
        const auto state = workspace.modules.find(*key);
        if (state != workspace.modules.end() &&
            !write_incremental_stage_cache(pending.stage_cache_path, state->second,
                                           pending.module_interface_hash, pending.fingerprint,
                                           options, profile)) {
          cli_errorf("failed to persist incremental stage cache ", pending.stage_cache_path);
          return false;
        }
      }
      ++compiled;
      return true;
    };

    for (const auto& module : graph.modules) {
      const auto stem = sanitize(module.logical_name);
      const auto ir_path = module_root / (stem + ".fir");
      const auto interface_path = module_root / (stem + ".dmi");
      const auto fingerprint_path = cache_root / (stem + ".fingerprint");
      const auto stage_cache_path = cache_root / (stem + ".incremental");
      const auto module_interface_hash = module_interface_fingerprint(graph, module, package_interface_hash);
      const auto fingerprint = hex(incremental_module_fingerprint(
          module, options, profile, workspace, interface_fingerprints));

      std::string previous;
      { std::ifstream input(fingerprint_path); input >> previous; }
      if (!options.force && profile.incremental && previous == fingerprint &&
          incremental_stage_cache_matches(stage_cache_path, fingerprint) &&
          std::filesystem::exists(interface_path) &&
          (check_only || std::filesystem::exists(ir_path))) {
        ++fresh;
        if (options.verbose) cli_status("Fresh", graph.manifest.name + "::" + module.logical_name, raz::terminal::cyan);
        continue;
      }

      if (!package_announced) {
        cli_status(check_only ? "Checking" : "Compiling", build_package_display(graph),
                   check_only ? raz::terminal::cyan : raz::terminal::green);
        package_announced = true;
      }
      if (options.verbose)
        cli_status("Module", graph.manifest.name + "::" + module.logical_name, raz::terminal::cyan);

      SessionOptions session;
      const auto semantic_input = semantic_input_for_module(graph, module, options, cache_root);
      session.input = semantic_input.path;
      session.diagnostic_display_path = semantic_input.display_path;
      session.diagnostic_line_delta = semantic_input.line_delta;
      session.diagnostic_byte_delta = semantic_input.byte_delta;
      session.check_only = check_only;
      session.target_triple = options.target;
      session.optimization_level = profile.optimization_level;
      session.diagnostic_format = options.diagnostic_format;
      session.diagnostic_policy = options.diagnostic_policy;
      session.suppress_success_output = true;
      if (check_only && diagnostic_reports != nullptr && options.diagnostic_format == DiagnosticFormat::json)
        session.diagnostic_output = cache_root / (stem + ".diagnostics.json");
      if (!check_only) { session.emit_forge_ir = true; session.output = ir_path; }

      PendingModuleCompile pending;
      pending.module = &module;
      pending.interface_path = interface_path;
      pending.fingerprint_path = fingerprint_path;
      pending.stage_cache_path = stage_cache_path;
      pending.diagnostic_report_path = session.diagnostic_output;
      pending.fingerprint = fingerprint;
      pending.module_interface_hash = module_interface_hash;
      pending.status = std::async(std::launch::async, [session = std::move(session)]() mutable {
        return compile_module_worker(session);
      });
      active.push_back(std::move(pending));

      if (active.size() >= options.jobs && !finish_front()) return false;
    }

    while (!active.empty()) if (!finish_front()) return false;
  }

  if (!check_only) {
    const auto artifact = output_root / (graph.manifest.name + ".razpkg");
    std::ofstream output(artifact, std::ios::trunc);
    if (!output) {
      cli_error("failed to create package artifact");
      return false;
    }

    output << "raz-package-v1\nname=" << graph.manifest.name << '\n'
           << "version=" << graph.manifest.version << '\n'
           << "kind=" << raz::compiler::package_kind_name(graph.manifest.kind) << '\n'
           << "target=" << options.target << "\nprofile=" << options.profile << '\n'
           << "interface_hash=" << hex(package_interface_hash) << '\n';
    for (const auto& module : graph.modules) {
      output << "module=" << module.logical_name << '\n';
    }
    output.close();

    if (!build_native_artifact(graph, options, profile)) {
      return false;
    }
  }

  return true;
}

bool build_graph(const ProjectGraph& graph, const Options& options, bool check_only,
                 std::set<std::filesystem::path>& built, std::size_t& compiled,
                 std::size_t& fresh, std::vector<std::string>* diagnostic_reports = nullptr) {
  const auto workspace = raz::compiler::build_workspace_graph(graph);
  std::map<std::string, std::uint64_t> interface_fingerprints;
  collect_module_interface_fingerprints(graph, workspace, interface_fingerprints);
  return build_graph_impl(graph, options, check_only, built, compiled, fresh,
                          diagnostic_reports, workspace, interface_fingerprints);
}
