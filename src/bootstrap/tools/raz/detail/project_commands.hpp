// Copyright 2026 Mario Vinciguerra
// SPDX-License-Identifier: Apache-2.0

std::string read_text_file(const std::filesystem::path& path) {
  std::ifstream input(path, std::ios::binary);
  std::ostringstream out;
  out << input.rdbuf();
  return out.str();
}

bool write_text_file(const std::filesystem::path& path, std::string_view text) {
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  output.write(text.data(), static_cast<std::streamsize>(text.size()));
  return static_cast<bool>(output);
}

std::string trim_copy(std::string_view value) {
  const auto begin = value.find_first_not_of(" \t\r");
  if (begin == std::string_view::npos) return {};
  const auto end = value.find_last_not_of(" \t\r");
  return std::string(value.substr(begin, end - begin + 1));
}

std::string format_raz_source(std::string_view source) {
  std::istringstream input{std::string(source)};
  std::ostringstream output;
  std::string line;
  std::size_t indent = 0;
  std::size_t blank_run = 0;
  while (std::getline(input, line)) {
    auto text = trim_copy(line);
    if (text.empty()) {
      if (blank_run == 0) output << '\n';
      ++blank_run;
      continue;
    }
    blank_run = 0;
    if (!text.empty() && text.front() == '}') indent = indent == 0 ? 0 : indent - 1;
    output << std::string(indent * 2, ' ') << text << '\n';
    std::size_t opens = 0, closes = 0;
    bool string = false, character = false, escaped = false;
    for (const char c : text) {
      if (escaped) { escaped = false; continue; }
      if ((string || character) && c == '\\') { escaped = true; continue; }
      if (!character && c == '"') { string = !string; continue; }
      if (!string && c == '\'') { character = !character; continue; }
      if (string || character) continue;
      if (c == '{') ++opens;
      else if (c == '}') ++closes;
    }
    const auto leading_close = text.front() == '}' ? 1U : 0U;
    const auto effective_closes = closes > leading_close ? closes - leading_close : 0U;
    if (opens >= effective_closes) indent += opens - effective_closes;
    else indent = effective_closes - opens >= indent ? 0 : indent - (effective_closes - opens);
  }
  auto result = output.str();
  while (result.size() > 1 && result.ends_with("\n\n")) result.pop_back();
  if (result.empty() || result.back() != '\n') result.push_back('\n');
  return result;
}

std::vector<const raz::compiler::ModuleUnit*> project_modules(const ProjectGraph& graph) {
  std::vector<const raz::compiler::ModuleUnit*> modules;
  for (const auto& dependency : graph.dependencies) {
    auto nested = project_modules(dependency);
    modules.insert(modules.end(), nested.begin(), nested.end());
  }

  for (const auto& module : graph.modules) modules.push_back(&module);
  return modules;
}

int format_project(const ProjectGraph& graph, const Options& options) {
  std::size_t changed = 0;
  for (const auto* module : project_modules(graph)) {
    const auto original = read_text_file(module->source_path);
    const auto formatted = format_raz_source(original);
    if (original == formatted) continue;
    ++changed;
    if (options.tool_check) std::cout << "Would format " << module->source_path << '\n';
    else if (!write_text_file(module->source_path, formatted)) {
      cli_errorf("failed to write ", module->source_path);
      return 1;
    } else if (options.verbose) std::cout << "Formatted " << module->source_path << '\n';
  }

  if (options.tool_check && changed != 0) {
    std::cerr << "raz fmt: " << changed << " file(s) require formatting\n";
    return 1;
  }
  std::cout << (options.tool_check ? "Formatting clean" : "Formatted") << " (" << changed << " changed)\n";
  return 0;
}

struct LintFinding final { std::filesystem::path path; std::size_t line = 0; std::string code; std::string message; };

int lint_project(const ProjectGraph& graph, const Options& options) {
  std::vector<LintFinding> findings;
  for (const auto* module : project_modules(graph)) {
    std::istringstream input(read_text_file(module->source_path));
    std::string line;
    std::size_t number = 0, blank_run = 0;
    while (std::getline(input, line)) {
      ++number;
      if (!line.empty() && (line.back() == ' ' || line.back() == '\t'))
        findings.push_back({module->source_path, number, "L001", "trailing whitespace"});
      if (line.find('\t') != std::string::npos)
        findings.push_back({module->source_path, number, "L002", "tab character; use spaces"});
      if (trim_copy(line).empty()) ++blank_run; else blank_run = 0;
      if (blank_run == 3) findings.push_back({module->source_path, number, "L003", "more than two consecutive blank lines"});
      const auto trimmed = trim_copy(line);
      if (trimmed.starts_with("unsafe {") && trimmed.find('}') != std::string::npos)
        findings.push_back({module->source_path, number, "L100", "single-line unsafe block should document its invariant"});
    }
  }

  for (const auto& finding : findings)
    std::cout << finding.path << ':' << finding.line << ": warning[" << finding.code << "]: " << finding.message << '\n';
  std::cout << "Linted " << project_modules(graph).size() << " module(s), " << findings.size() << " warning(s)\n";
  return options.deny_warnings && !findings.empty() ? 1 : 0;
}

std::string html_escape(std::string_view value) {
  std::string result;
  for (const char c : value) {
    if (c == '&') result += "&amp;";
    else if (c == '<') result += "&lt;";
    else if (c == '>') result += "&gt;";
    else if (c == '"') result += "&quot;";
    else result += c;
  }
  return result;
}

int document_project(const ProjectGraph& graph) {
  const auto root = graph.manifest.root / "target" / "doc";
  std::filesystem::create_directories(root);
  std::ostringstream index;
  index << "<!doctype html><meta charset=\"utf-8\"><title>" << html_escape(graph.manifest.name)
        << " documentation</title><h1>" << html_escape(graph.manifest.name) << "</h1><ul>";
  for (const auto* module : project_modules(graph)) {
    const auto filename = sanitize(module->logical_name) + ".html";
    index << "<li><a href=\"" << filename << "\">" << html_escape(module->logical_name) << "</a></li>";
    std::istringstream input(read_text_file(module->source_path));
    std::ostringstream page;
    page << "<!doctype html><meta charset=\"utf-8\"><title>" << html_escape(module->logical_name)
         << "</title><h1>Module " << html_escape(module->logical_name) << "</h1>";
    std::string line, docs;
    while (std::getline(input, line)) {
      const auto trimmed = trim_copy(line);
      if (trimmed.starts_with("///")) { docs += trim_copy(std::string_view(trimmed).substr(3)) + "\n"; continue; }
      if (trimmed.starts_with("public ")) {
        page << "<section><pre>" << html_escape(trimmed) << "</pre>";
        if (!docs.empty()) page << "<p>" << html_escape(docs) << "</p>";
        page << "</section>";
      }
      if (!trimmed.empty()) docs.clear();
    }

    if (!write_text_file(root / filename, page.str())) return 1;
  }
  index << "</ul>";
  if (!write_text_file(root / "index.html", index.str())) return 1;
  cli_status("Generated", std::string("documentation at ") + (root / "index.html").string(), raz::terminal::green);
  return 0;
}

void collect_lock_entries(const ProjectGraph& graph, const std::filesystem::path& root,
                          std::vector<std::string>& entries) {
  std::error_code error;
  auto relative = std::filesystem::relative(graph.manifest.root, root, error);
  if (error) relative = graph.manifest.root;
  const auto manifest_path = graph.manifest.root / "raz.toml";
  std::ostringstream entry;
  entry << "[[package]]\n"
        << "name = \"" << graph.manifest.name << "\"\n"
        << "version = \"" << graph.manifest.version << "\"\n"
        << "kind = \"" << raz::compiler::package_kind_name(graph.manifest.kind) << "\"\n"
        << "path = \"" << relative.generic_string() << "\"\n"
        << "manifest_hash = \"" << hex(hash_file(manifest_path, "raz-lock-v1")) << "\"\n";
  entries.push_back(entry.str());
  for (const auto& dependency : graph.dependencies) collect_lock_entries(dependency, root, entries);
}

int write_lockfile(const ProjectGraph& graph, bool quiet = false) {
  std::vector<std::string> entries;
  collect_lock_entries(graph, graph.manifest.root, entries);
  std::sort(entries.begin(), entries.end());
  std::ostringstream output;
  output << "# Generated by Raz. Do not edit manually.\nversion = 1\n\n";
  for (const auto& entry : entries) output << entry << '\n';
  const auto path = graph.manifest.root / "raz.lock";
  const auto contents = output.str();
  if (read_text_file(path) == contents) {
    if (!quiet) std::cout << "Lockfile is current: " << path << '\n';
    return 0;
  }

  if (!write_text_file(path, contents)) {
    cli_errorf("failed to write ", path);
    return 1;
  }

  if (!quiet) std::cout << "Wrote " << path << '\n';
  return 0;
}

bool link_native_executable(const std::filesystem::path& object, const std::filesystem::path& binary) {
  const std::string command = native_link_command(object, binary);
  return execute_shell_command(command) == 0 && std::filesystem::exists(binary);
}

int benchmark_project(const ProjectGraph& graph, const Options& options) {
  const auto bench_root = graph.manifest.root / "benches";
  if (!std::filesystem::is_directory(bench_root)) {
    std::cerr << "raz bench: no benches directory at " << bench_root << '\n';
    return 1;
  }
  const auto output_root = graph.manifest.root / "target" / options.target / options.profile / "benches";
  std::filesystem::create_directories(output_root);
  std::size_t count = 0;
  for (const auto& entry : std::filesystem::recursive_directory_iterator(bench_root)) {
    if (!entry.is_regular_file() || entry.path().extension() != ".rz") continue;
    ++count;
    const auto name = entry.path().stem().string();
    const auto root = output_root / name;
    std::filesystem::create_directories(root);
    const auto ir = root / "bench.fir";
#if defined(_WIN32)
    const auto object = root / "bench.obj";
    const auto binary = root / "bench.exe";
#else
    const auto object = root / "bench.o";
    const auto binary = root / "bench";
#endif
    SessionOptions session; session.input = entry.path(); session.emit_forge_ir = true; session.output = ir;
    if (Compiler{}.run(Session(std::move(session))) != 0) return 1;
    const auto profile_it = graph.manifest.profiles.find(options.profile);
    if (profile_it == graph.manifest.profiles.end()) { cli_errorf("unknown profile ", options.profile); return 1; }
    if (!emit_native_object(ir, object, profile_it->second.optimization_level) || !link_native_executable(object, binary)) return 1;
    std::vector<double> samples;
    samples.reserve(options.iterations);
    for (unsigned iteration = 0; iteration < options.iterations; ++iteration) {
      const auto start = std::chrono::steady_clock::now();
      if (execute_shell_command(shell_quote(binary)) != 0) { std::cerr << "raz bench: " << name << " failed\n"; return 1; }
      const auto end = std::chrono::steady_clock::now();
      samples.push_back(std::chrono::duration<double, std::milli>(end - start).count());
    }

    std::sort(samples.begin(), samples.end());
    const double median = samples[samples.size() / 2];
    double total = 0.0; for (const double value : samples) total += value;
    std::cout << std::fixed << std::setprecision(3) << "bench " << name
              << ": median " << median << " ms, mean " << total / samples.size()
              << " ms, min " << samples.front() << " ms, max " << samples.back()
              << " ms (" << samples.size() << " runs)\n";
  }

  if (count == 0) { std::cerr << "raz bench: no .rz benchmarks found\n"; return 1; }
  return 0;
}

int package_project(const ProjectGraph& graph, Options options) {
  options.profile = "release";
  if (write_lockfile(graph, true) != 0) return 1;
  std::set<std::filesystem::path> built;
  std::size_t compiled = 0, fresh = 0;
  if (!build_graph(graph, options, false, built, compiled, fresh)) return 1;
  const auto package_root = graph.manifest.root / "target" / "package";
  const auto staging = package_root / (graph.manifest.name + "-" + graph.manifest.version);
  std::filesystem::remove_all(staging);
  std::filesystem::create_directories(staging);
  std::filesystem::copy_file(graph.manifest.root / "raz.toml", staging / "raz.toml", std::filesystem::copy_options::overwrite_existing);
  std::filesystem::copy_file(graph.manifest.root / "raz.lock", staging / "raz.lock", std::filesystem::copy_options::overwrite_existing);
  std::filesystem::copy(graph.manifest.root / graph.manifest.source_directory, staging / graph.manifest.source_directory,
                        std::filesystem::copy_options::recursive | std::filesystem::copy_options::overwrite_existing);
  const auto native = native_artifact_path(graph, options);
  if (std::filesystem::exists(native)) {
    std::filesystem::create_directories(staging / "bin");
    std::filesystem::copy_file(native, staging / "bin" / native.filename(), std::filesystem::copy_options::overwrite_existing);
  }

  std::ofstream manifest(staging / "PACKAGE-MANIFEST.txt", std::ios::trunc);
  for (const auto& entry : std::filesystem::recursive_directory_iterator(staging)) {
    if (!entry.is_regular_file() || entry.path().filename() == "PACKAGE-MANIFEST.txt") continue;
    manifest << hex(hash_file(entry.path(), "raz-package-v1")) << "  "
             << std::filesystem::relative(entry.path(), staging).generic_string() << '\n';
  }
  manifest.close();
  const auto archive = package_root / (graph.manifest.name + "-" + graph.manifest.version + ".zip");
  std::filesystem::remove(archive);
#if defined(_WIN32)
  const std::string command = "cd /d " + shell_quote(package_root) + " && cmake -E tar cf " + shell_quote(archive.filename()) + " --format=zip " + shell_quote(staging.filename());
#else
  const std::string command = "cd " + shell_quote(package_root) + " && cmake -E tar cf " + shell_quote(archive.filename()) + " --format=zip " + shell_quote(staging.filename());
#endif
  if (execute_shell_command(command) != 0 || !std::filesystem::exists(archive)) { std::cerr << "raz package: archive creation failed\n"; return 1; }
  std::cout << "Packaged " << archive << '\n';
  return 0;
}
