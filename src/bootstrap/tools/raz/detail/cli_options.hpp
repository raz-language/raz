// Copyright 2026 Mario Vinciguerra
// SPDX-License-Identifier: Apache-2.0

bool use_color(std::ostream& stream) { return raz::terminal::color_enabled(stream, g_color_mode); }

void cli_status(std::string_view label, std::string_view text,
                std::string_view color = raz::terminal::green) {
  if (g_quiet) return;
  const bool enabled = use_color(std::cout);
  if (enabled) std::cout << raz::terminal::bold << color;
  std::cout << std::setw(12) << std::right << label;
  if (enabled) std::cout << raz::terminal::reset;
  std::cout << ' ' << text << '\n';
}

void cli_error(std::string_view text) {
  const bool enabled = use_color(std::cerr);
  if (enabled) std::cerr << raz::terminal::bold << raz::terminal::red;
  std::cerr << "error";
  if (enabled) std::cerr << raz::terminal::reset;
  std::cerr << ": " << text << '\n';
}

void cli_hint(std::string_view text) {
  const bool enabled = use_color(std::cerr);
  std::cerr << "  ";
  if (enabled) std::cerr << raz::terminal::bold << raz::terminal::cyan;
  std::cerr << "help";
  if (enabled) std::cerr << raz::terminal::reset;
  std::cerr << ": " << text << '\n';
}

template <typename... Parts>
void cli_errorf(Parts&&... parts) {
  std::ostringstream message;
  (message << ... << std::forward<Parts>(parts));
  cli_error(message.str());
}

template <typename... Parts>
void cli_hintf(Parts&&... parts) {
  std::ostringstream message;
  (message << ... << std::forward<Parts>(parts));
  cli_hint(message.str());
}

void configure_child_color(raz::terminal::ColorMode mode) {
  const char* value = mode == raz::terminal::ColorMode::always ? "always" :
                      mode == raz::terminal::ColorMode::never ? "never" : "auto";
#if defined(_WIN32)
  _putenv_s("RAZ_COLOR", value);
#else
  setenv("RAZ_COLOR", value, 1);
#endif
}

void release_compiler_memory() {
#if defined(_WIN32)
  _heapmin();
#elif defined(__GLIBC__)
  malloc_trim(0);
#endif
}

std::string environment_value(const char* name) {
#if defined(_WIN32)
  char* value = nullptr;
  std::size_t length = 0;
  const errno_t status = _dupenv_s(&value, &length, name);
  if (status != 0 || value == nullptr) {
    std::free(value);
    return {};
  }

  std::string result(value, length > 0 ? length - 1 : 0);
  std::free(value);
  return result;
#else
  const char* value = std::getenv(name);
  return value == nullptr ? std::string{} : std::string(value);
#endif
}

struct CommandSpec final {
  std::string_view name;
  std::string_view usage;
  std::string_view description;
  std::string_view group;
};

static constexpr CommandSpec k_commands[] = {
    {"build", "raz build [path] [options]", "Compile a package and its dependencies", "Build"},
    {"check", "raz check [path] [options]", "Check a package without producing native artifacts", "Build"},
    {"run", "raz run [path] [options]", "Build and run an executable package", "Build"},
    {"test", "raz test [path] [options]", "Build and run package tests", "Build"},
    {"bench", "raz bench [path] [options]", "Run package benchmarks", "Build"},
    {"profile", "raz profile [path] [options]", "Profile package execution", "Build"},
    {"coverage", "raz coverage [path] [options]", "Generate package coverage information", "Build"},
    {"fuzz", "raz fuzz [path] [options]", "Run deterministic fuzz cases", "Build"},

    {"new", "raz new <path> [options]", "Create a new Raz package", "Project"},
    {"init", "raz init [path] [options]", "Create a Raz package in an existing directory", "Project"},
    {"clean", "raz clean [path] [options]", "Remove build and incremental artifacts", "Project"},
    {"metadata", "raz metadata [path] [options]", "Print package and module metadata", "Project"},
    {"graph", "raz graph [path] [options]", "Print the dependency graph", "Project"},
    {"doctor", "raz doctor [path] [options]", "Check the project and host toolchain", "Project"},
    {"cache", "raz cache [path] [options]", "Inspect or prune compiler caches", "Project"},
    {"lock", "raz lock [path] [options]", "Write the dependency lockfile", "Project"},
    {"verify", "raz verify [path] [options]", "Verify package and lockfile integrity", "Project"},

    {"fmt", "raz fmt [path] [options]", "Format Raz source files", "Tooling"},
    {"lint", "raz lint [path] [options]", "Run compiler and style lints", "Tooling"},
    {"doc", "raz doc [path] [options]", "Generate package documentation", "Tooling"},
    {"spec", "raz spec [path] [options]", "Emit the language specification view", "Tooling"},
    {"diagnostics", "raz diagnostics [options]", "List stable compiler diagnostic codes", "Tooling"},
    {"lsp", "raz lsp", "Run the Raz language server over stdio", "Tooling"},

    {"sbom", "raz sbom [path] [options]", "Generate a software bill of materials", "Package"},
    {"audit", "raz audit [path] [options]", "Audit package dependency metadata", "Package"},
    {"package", "raz package [path] [options]", "Create a distributable package", "Package"},
    {"publish", "raz publish [path] [options]", "Publish a package to a registry", "Package"},
    {"install", "raz install [path] [options]", "Install an executable package", "Package"},
    {"uninstall", "raz uninstall [path] [options]", "Uninstall an executable package", "Package"},

    {"version", "raz version", "Print Raz version information", "General"},
};

static constexpr std::string_view k_option_names[] = {
    "--help", "--release", "--verbose", "--quiet", "--color", "--diagnostic-format",
    "--jobs", "--force", "--check", "--allow", "--warn", "--deny", "--deny-warnings",
    "--list", "--prune", "--profile", "--target", "--prefix", "--report", "--registry",
    "--iterations", "--cases", "--seed",
};

const CommandSpec* find_command(std::string_view name) {
  for (const auto& command : k_commands)
    if (command.name == name) return &command;
  return nullptr;
}

std::size_t edit_distance(std::string_view lhs, std::string_view rhs) {
  std::vector<std::size_t> previous(rhs.size() + 1), current(rhs.size() + 1);
  for (std::size_t j = 0; j <= rhs.size(); ++j) previous[j] = j;
  for (std::size_t i = 1; i <= lhs.size(); ++i) {
    current[0] = i;
    for (std::size_t j = 1; j <= rhs.size(); ++j) {
      const std::size_t substitution = previous[j - 1] + (lhs[i - 1] == rhs[j - 1] ? 0U : 1U);
      current[j] = std::min({previous[j] + 1, current[j - 1] + 1, substitution});
    }
    previous.swap(current);
  }
  return previous[rhs.size()];
}

bool suggestion_is_close(std::string_view input, std::size_t distance) {
  if (distance <= 1) return true;
  if (input.size() >= 4 && distance <= 2) return true;
  return input.size() >= 8 && distance <= 3;
}

std::string_view suggest_command(std::string_view input) {
  std::string_view best;
  std::size_t best_distance = 1024;
  for (const auto& command : k_commands) {
    const auto distance = edit_distance(input, command.name);
    if (distance < best_distance) {
      best = command.name;
      best_distance = distance;
    }
  }
  return suggestion_is_close(input, best_distance) ? best : std::string_view{};
}

std::string_view suggest_option(std::string_view input) {
  std::string_view best;
  std::size_t best_distance = 1024;
  for (const auto option : k_option_names) {
    const auto distance = edit_distance(input, option);
    if (distance < best_distance) {
      best = option;
      best_distance = distance;
    }
  }
  return suggestion_is_close(input, best_distance) ? best : std::string_view{};
}

void cli_unknown_command(std::string_view command) {
  cli_errorf("no such command: '", command, "'");
  const auto suggestion = suggest_command(command);
  if (!suggestion.empty()) cli_hintf("a similar command exists: '", suggestion, "'");
  cli_hint("view all commands with 'raz --help'");
}

void cli_unknown_option(std::string_view option, std::string_view command) {
  cli_errorf("unexpected argument '", option, "'");
  const auto suggestion = suggest_option(option);
  if (!suggestion.empty()) cli_hintf("a similar option exists: '", suggestion, "'");
  if (!command.empty()) cli_hintf("run 'raz ", command, " --help' for command usage");
  else cli_hint("run 'raz --help' for usage");
}

void print_command_group(std::string_view group) {
  for (const auto& command : k_commands) {
    if (command.group != group) continue;
    std::cout << "  " << std::left << std::setw(13) << command.name << command.description << '\n';
  }
}

void usage() {
  std::cout << "Raz 1.0.0\n"
            << "The Raz package manager, build tool, and project driver.\n\n"
            << "Usage:\n"
            << "  raz <COMMAND> [PATH] [OPTIONS]\n\n"
            << "Build commands:\n";
  print_command_group("Build");
  std::cout << "\nProject commands:\n";
  print_command_group("Project");
  std::cout << "\nTooling commands:\n";
  print_command_group("Tooling");
  std::cout << "\nPackage commands:\n";
  print_command_group("Package");
  std::cout << "\nGeneral commands:\n";
  print_command_group("General");
  std::cout << "\nCommon options:\n"
            << "  -h, --help              Print help for Raz or a command\n"
            << "  -V, --version           Print version information\n"
            << "      --profile <name>    Select a manifest build profile (default: debug)\n"
            << "      --release           Alias for --profile release\n"
            << "      --target <triple>   Select output target (default: host)\n"
            << "  -j, --jobs <n>          Parallel compiler jobs (default: auto, max 64)\n"
            << "  -v, --verbose           Print per-module build actions\n"
            << "  -q, --quiet             Print only errors and requested program output\n"
            << "      --color <mode>      Color output: auto, always, or never\n\n"
            << "Run 'raz <COMMAND> --help' for command-specific usage.\n";
}

void usage_command(std::string_view name) {
  const auto* command = find_command(name);
  if (command == nullptr) {
    cli_unknown_command(name);
    return;
  }

  std::cout << command->description << ".\n\n"
            << "Usage:\n  " << command->usage << "\n\n";

  const bool build_command = name == "build" || name == "check" || name == "run" ||
                             name == "test" || name == "package" || name == "publish" ||
                             name == "install";
  if (build_command) {
    std::cout << "Build options:\n"
              << "      --profile <name>    Select a manifest build profile (default: debug)\n"
              << "      --release           Alias for --profile release\n"
              << "      --target <triple>   Select output target (default: host)\n"
              << "  -j, --jobs <n>          Parallel compiler jobs (default: auto, max 64)\n"
              << "      --force             Rebuild all modules\n"
              << "      --diagnostic-format <human|short|json>\n"
              << "      --allow <code|category>\n"
              << "      --warn <code|category>\n"
              << "      --deny <code|category>\n";
  } else if (name == "bench") {
    std::cout << "Benchmark build options:\n"
              << "      --profile <name>    Select a manifest build profile (default: debug)\n"
              << "      --release           Alias for --profile release\n"
              << "      --target <triple>   Select output target (default: host)\n";
  }
  if (name == "run") {
    std::cout << "      -- <args...>        Pass remaining arguments to the executable\n";
  } else if (name == "test") {
    std::cout << "      --list              List discovered tests without executing\n"
              << "      --report <path>     Write an additional JSON test report\n";
  } else if (name == "bench") {
    std::cout << "      --iterations <n>    Benchmark iterations (default: 10)\n";
  } else if (name == "fuzz") {
    std::cout << "      --cases <n>         Fuzz cases (default: 100)\n"
              << "      --seed <n>          Deterministic fuzz seed\n";
  } else if (name == "fmt") {
    std::cout << "      --check             Check formatting without writing\n";
  } else if (name == "lint") {
    std::cout << "      --deny-warnings     Make lint warnings fail the command\n";
  } else if (name == "cache") {
    std::cout << "      --prune             Remove project compiler caches\n";
  } else if (name == "install" || name == "uninstall") {
    std::cout << "      --prefix <path>     Installation prefix\n";
  } else if (name == "publish") {
    std::cout << "      --registry <path>   Local package registry override\n";
  } else if (name == "diagnostics") {
    std::cout << "      --diagnostic-format <human|json>\n";
  }

  std::cout << "\nOutput options:\n"
            << "  -v, --verbose           Print additional command detail\n"
            << "  -q, --quiet             Suppress non-error status output\n"
            << "      --color <mode>      Color output: auto, always, or never\n"
            << "  -h, --help              Print this help\n";
}

struct Options final {
  std::string command;
  std::filesystem::path project = std::filesystem::current_path();
  bool project_explicit = false;
  std::string profile = "debug";
  std::string target = "host";
  bool verbose = false;
  unsigned jobs = std::max(1U, std::min(16U, std::thread::hardware_concurrency() == 0 ? 1U : std::thread::hardware_concurrency()));
  bool quiet = false;
  raz::terminal::ColorMode color = raz::terminal::ColorMode::auto_;
  DiagnosticFormat diagnostic_format = DiagnosticFormat::human;
  raz::compiler::DiagnosticPolicy diagnostic_policy;
  bool force = false;
  bool tool_check = false;
  bool deny_warnings = false;
  unsigned iterations = 10;
  unsigned cases = 100;
  std::uint64_t seed = 0xD45A90ULL;
  std::filesystem::path prefix;
  bool list_tests = false;
  bool prune_cache = false;
  std::filesystem::path report_path;
  std::filesystem::path registry;
  std::vector<std::string> program_args;
  bool show_help = false;
  std::string help_command;
};

bool parse_unsigned_option(std::string_view option, std::string_view value,
                           unsigned minimum, unsigned maximum, unsigned& output) {
  try {
    const std::string owned(value);
    std::size_t consumed = 0;
    const auto parsed = std::stoul(owned, &consumed, 10);
    if (consumed != owned.size() || parsed < minimum || parsed > maximum) throw std::out_of_range("range");
    output = static_cast<unsigned>(parsed);
    return true;
  } catch (...) {
    cli_errorf("invalid value '", value, "' for '", option, "' (expected ", minimum, "..", maximum, ")");
    return false;
  }
}

bool parse_color_option(std::string_view value, Options& options) {
  if (value == "auto") options.color = raz::terminal::ColorMode::auto_;
  else if (value == "always") options.color = raz::terminal::ColorMode::always;
  else if (value == "never") options.color = raz::terminal::ColorMode::never;
  else {
    cli_errorf("invalid value '", value, "' for '--color'");
    cli_hint("expected one of: auto, always, never");
    return false;
  }
  g_color_mode = options.color;
  return true;
}

bool parse_diagnostic_format_option(std::string_view value, Options& options) {
  if (value == "human") options.diagnostic_format = DiagnosticFormat::human;
  else if (value == "short") options.diagnostic_format = DiagnosticFormat::short_;
  else if (value == "json") options.diagnostic_format = DiagnosticFormat::json;
  else {
    cli_errorf("invalid value '", value, "' for '--diagnostic-format'");
    cli_hint("expected one of: human, short, json");
    return false;
  }
  return true;
}

bool split_inline_option(std::string_view arg, std::string_view prefix, std::string_view& value) {
  if (!arg.starts_with(prefix)) return false;
  value = arg.substr(prefix.size());
  return true;
}

std::string_view canonical_option_name(std::string_view arg) {
  if (arg == "-h") return "--help";
  if (arg == "-v") return "--verbose";
  if (arg == "-q") return "--quiet";
  if (arg == "-j") return "--jobs";
  const auto equals = arg.find('=');
  return equals == std::string_view::npos ? arg : arg.substr(0, equals);
}

bool known_option(std::string_view option) {
  for (const auto known : k_option_names)
    if (known == option) return true;
  return false;
}

bool option_allowed_for_command(std::string_view command, std::string_view option) {
  if (option == "--help" || option == "--verbose" || option == "--quiet" || option == "--color") return true;

  const bool native_build = command == "build" || command == "check" || command == "run" ||
                            command == "test" || command == "package" || command == "publish" ||
                            command == "install";
  if (option == "--profile" || option == "--release" || option == "--target")
    return native_build || command == "bench";
  if (option == "--jobs" || option == "--force" || option == "--allow" ||
      option == "--warn" || option == "--deny") return native_build;
  if (option == "--diagnostic-format") return native_build || command == "diagnostics";
  if (option == "--check") return command == "fmt";
  if (option == "--deny-warnings") return command == "lint";
  if (option == "--list" || option == "--report") return command == "test";
  if (option == "--prune") return command == "cache";
  if (option == "--prefix") return command == "install" || command == "uninstall";
  if (option == "--registry") return command == "publish";
  if (option == "--iterations") return command == "bench";
  if (option == "--cases" || option == "--seed") return command == "fuzz";
  return false;
}

bool validate_command_option(std::string_view command, std::string_view arg) {
  const auto option = canonical_option_name(arg);
  if (!known_option(option) || option_allowed_for_command(command, option)) return true;
  cli_errorf("option '", option, "' is not valid for command '", command, "'");
  cli_hintf("run 'raz ", command, " --help' to see the options accepted by this command");
  return false;
}

bool parse(int argc, char** argv, Options& options) {
  if (argc < 2) {
    options.show_help = true;
    return true;
  }

  const std::string_view first(argv[1]);
  if (first == "help" || first == "--help" || first == "-h") {
    options.command = "help";
    options.show_help = true;
    if (argc >= 3) {
      options.help_command = argv[2];
      if (find_command(options.help_command) == nullptr) {
        cli_unknown_command(options.help_command);
        return false;
      }
    }
    if (argc > 3) {
      cli_errorf("unexpected argument '", argv[3], "'");
      cli_hint("usage: raz help [command]");
      return false;
    }
    return true;
  }

  if (first == "--version" || first == "-V") options.command = "version";
  else options.command = std::string(first);

  if (find_command(options.command) == nullptr) {
    cli_unknown_command(options.command);
    return false;
  }

  bool path_seen = false;
  for (int i = 2; i < argc; ++i) {
    const std::string_view arg(argv[i]);
    std::string_view inline_value;

    if (arg == "--") {
      if (options.command != "run") {
        cli_errorf("argument separator '--' is only valid for command 'run'");
        cli_hintf("run 'raz ", options.command, " --help' for command usage");
        return false;
      }
      for (++i; i < argc; ++i) options.program_args.emplace_back(argv[i]);
      break;
    }

    if (!arg.empty() && arg.front() == '-' && !validate_command_option(options.command, arg)) return false;

    auto value_after = [&](std::string_view option) -> std::optional<std::string_view> {
      if (i + 1 >= argc) {
        cli_errorf("option '", option, "' requires a value");
        cli_hintf("run 'raz ", options.command, " --help' for usage");
        return std::nullopt;
      }
      const std::string_view value(argv[i + 1]);
      if (!value.empty() && value.front() == '-') {
        cli_errorf("option '", option, "' requires a value");
        cli_hintf("run 'raz ", options.command, " --help' for usage");
        return std::nullopt;
      }
      ++i;
      return value;
    };

    if (arg == "--help" || arg == "-h") {
      options.show_help = true;
      options.help_command = options.command;
    } else if (arg == "--release") options.profile = "release";
    else if (arg == "--verbose" || arg == "-v") options.verbose = true;
    else if (arg == "--quiet" || arg == "-q") options.quiet = true;
    else if (arg == "--force") options.force = true;
    else if (arg == "--check") options.tool_check = true;
    else if (arg == "--deny-warnings") { options.deny_warnings = true; options.diagnostic_policy.deny_warnings = true; }
    else if (arg == "--list") options.list_tests = true;
    else if (arg == "--prune") options.prune_cache = true;
    else if (arg == "--color") {
      const auto value = value_after(arg); if (!value.has_value() || !parse_color_option(*value, options)) return false;
    } else if (split_inline_option(arg, "--color=", inline_value)) {
      if (inline_value.empty() || !parse_color_option(inline_value, options)) return false;
    } else if (arg == "--diagnostic-format") {
      const auto value = value_after(arg); if (!value.has_value() || !parse_diagnostic_format_option(*value, options)) return false;
    } else if (split_inline_option(arg, "--diagnostic-format=", inline_value)) {
      if (inline_value.empty() || !parse_diagnostic_format_option(inline_value, options)) return false;
    } else if (arg == "--jobs" || arg == "-j") {
      const auto value = value_after(arg); if (!value.has_value() || !parse_unsigned_option("--jobs", *value, 1, 64, options.jobs)) return false;
    } else if (split_inline_option(arg, "--jobs=", inline_value)) {
      if (!parse_unsigned_option("--jobs", inline_value, 1, 64, options.jobs)) return false;
    } else if ((arg == "--allow" || arg == "--warn" || arg == "--deny")) {
      const auto value = value_after(arg); if (!value.has_value()) return false;
      raz::compiler::DiagnosticLevel level = raz::compiler::DiagnosticLevel::warn;
      if (arg == "--allow") level = raz::compiler::DiagnosticLevel::allow;
      else if (arg == "--deny") level = raz::compiler::DiagnosticLevel::deny;
      options.diagnostic_policy.overrides.push_back({std::string(*value), level});
    } else if (arg == "--profile") {
      const auto value = value_after(arg); if (!value.has_value()) return false; options.profile = *value;
    } else if (split_inline_option(arg, "--profile=", inline_value)) {
      if (inline_value.empty()) { cli_error("option '--profile' requires a value"); return false; } options.profile = inline_value;
    } else if (arg == "--target") {
      const auto value = value_after(arg); if (!value.has_value()) return false; options.target = *value;
    } else if (split_inline_option(arg, "--target=", inline_value)) {
      if (inline_value.empty()) { cli_error("option '--target' requires a value"); return false; } options.target = inline_value;
    } else if (arg == "--prefix") {
      const auto value = value_after(arg); if (!value.has_value()) return false; options.prefix = std::filesystem::path(*value);
    } else if (split_inline_option(arg, "--prefix=", inline_value)) {
      if (inline_value.empty()) { cli_error("option '--prefix' requires a value"); return false; } options.prefix = std::filesystem::path(inline_value);
    } else if (arg == "--report") {
      const auto value = value_after(arg); if (!value.has_value()) return false; options.report_path = std::filesystem::path(*value);
    } else if (split_inline_option(arg, "--report=", inline_value)) {
      if (inline_value.empty()) { cli_error("option '--report' requires a value"); return false; } options.report_path = std::filesystem::path(inline_value);
    } else if (arg == "--registry") {
      const auto value = value_after(arg); if (!value.has_value()) return false; options.registry = std::filesystem::path(*value);
    } else if (split_inline_option(arg, "--registry=", inline_value)) {
      if (inline_value.empty()) { cli_error("option '--registry' requires a value"); return false; } options.registry = std::filesystem::path(inline_value);
    } else if (arg == "--iterations") {
      const auto value = value_after(arg); if (!value.has_value() || !parse_unsigned_option(arg, *value, 1, 100000, options.iterations)) return false;
    } else if (split_inline_option(arg, "--iterations=", inline_value)) {
      if (!parse_unsigned_option("--iterations", inline_value, 1, 100000, options.iterations)) return false;
    } else if (arg == "--cases") {
      const auto value = value_after(arg); if (!value.has_value() || !parse_unsigned_option(arg, *value, 1, 100000, options.cases)) return false;
    } else if (split_inline_option(arg, "--cases=", inline_value)) {
      if (!parse_unsigned_option("--cases", inline_value, 1, 100000, options.cases)) return false;
    } else if (arg == "--seed") {
      const auto value = value_after(arg); if (!value.has_value()) return false;
      try {
        const std::string owned(*value); std::size_t consumed = 0;
        options.seed = std::stoull(owned, &consumed, 0);
        if (consumed != owned.size()) throw std::invalid_argument("seed");
      } catch (...) { cli_errorf("invalid value '", *value, "' for '--seed'"); return false; }
    } else if (split_inline_option(arg, "--seed=", inline_value)) {
      try {
        const std::string owned(inline_value); std::size_t consumed = 0;
        options.seed = std::stoull(owned, &consumed, 0);
        if (consumed != owned.size()) throw std::invalid_argument("seed");
      } catch (...) { cli_errorf("invalid value '", inline_value, "' for '--seed'"); return false; }
    } else if (!arg.empty() && arg.front() == '-') {
      cli_unknown_option(arg, options.command);
      return false;
    } else if (!path_seen) {
      options.project = std::filesystem::path(arg);
      options.project_explicit = true;
      path_seen = true;
    } else {
      cli_errorf("unexpected argument '", arg, "'");
      cli_hintf("usage: ", find_command(options.command)->usage);
      return false;
    }
  }

  if (options.command == "new" && !path_seen && !options.show_help) {
    cli_error("missing required package path");
    cli_hint("usage: raz new <path>");
    return false;
  }
  if ((options.command == "version" || options.command == "lsp" || options.command == "diagnostics") && path_seen) {
    cli_errorf("unexpected argument '", options.project.string(), "'");
    cli_hintf("usage: ", find_command(options.command)->usage);
    return false;
  }
  return true;
}
