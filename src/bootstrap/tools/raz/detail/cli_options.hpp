// Copyright 2026 Mario Vinciguerra
// SPDX-License-Identifier: Apache-2.0

// Stage-0 is deliberately not the production Raz CLI.  It only exposes the
// commands and switches required to construct/check the self-hosted compiler.

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

struct Options final {
  std::string command;
  std::filesystem::path project = std::filesystem::current_path();
  std::string profile = "debug";
  std::string target = "host";
  bool verbose = false;
  unsigned jobs = std::max(1U, std::min(16U, std::thread::hardware_concurrency() == 0 ? 1U : std::thread::hardware_concurrency()));
  bool quiet = false;
  raz::terminal::ColorMode color = raz::terminal::ColorMode::auto_;
  DiagnosticFormat diagnostic_format = DiagnosticFormat::human;
  raz::compiler::DiagnosticPolicy diagnostic_policy;
  bool force = false;
  bool show_help = false;
  std::string help_command;
};

void usage() {
  std::cout << "Raz Stage-0 1.0.0\n"
            << "Bootstrap compiler used only to construct the Raz-written compiler.\n\n"
            << "Usage:\n"
            << "  raz-stage0 build [path] [options]\n"
            << "  raz-stage0 check [path] [options]\n"
            << "  raz-stage0 version\n\n"
            << "Build options:\n"
            << "      --profile <name>    Select a manifest build profile (default: debug)\n"
            << "      --release           Alias for --profile release\n"
            << "      --target <triple>   Select an explicit non-host target\n"
            << "  -j, --jobs <n>          Parallel compiler jobs (default: auto, max 64)\n"
            << "      --force             Rebuild all modules\n"
            << "      --diagnostic-format <human|short|json>\n"
            << "      --allow <code|category>\n"
            << "      --warn <code|category>\n"
            << "      --deny <code|category>\n"
            << "  -v, --verbose           Print per-module build actions\n"
            << "  -q, --quiet             Print only errors\n"
            << "      --color <mode>      Color output: auto, always, never\n";
}

void usage_command(std::string_view name) {
  if (name != "build" && name != "check" && name != "version") {
    cli_errorf("Stage-0 does not provide production command '", name, "'");
    cli_hint("use the Raz-written compiler for production tooling");
    return;
  }
  usage();
}

bool parse_unsigned(std::string_view option, std::string_view value, unsigned& output) {
  try {
    const std::string owned(value);
    std::size_t consumed = 0;
    const auto parsed = std::stoul(owned, &consumed, 10);
    if (consumed != owned.size() || parsed < 1 || parsed > 64) throw std::out_of_range("range");
    output = static_cast<unsigned>(parsed);
    return true;
  } catch (...) {
    cli_errorf("invalid value '", value, "' for '", option, "' (expected 1..64)");
    return false;
  }
}

bool parse_color(std::string_view value, Options& options) {
  if (value == "auto") options.color = raz::terminal::ColorMode::auto_;
  else if (value == "always") options.color = raz::terminal::ColorMode::always;
  else if (value == "never") options.color = raz::terminal::ColorMode::never;
  else { cli_errorf("invalid value '", value, "' for '--color'"); return false; }
  g_color_mode = options.color;
  return true;
}

bool parse_diagnostic_format(std::string_view value, Options& options) {
  if (value == "human") options.diagnostic_format = DiagnosticFormat::human;
  else if (value == "short") options.diagnostic_format = DiagnosticFormat::short_;
  else if (value == "json") options.diagnostic_format = DiagnosticFormat::json;
  else { cli_errorf("invalid value '", value, "' for '--diagnostic-format'"); return false; }
  return true;
}

bool parse(int argc, char** argv, Options& options) {
  if (argc < 2) { options.show_help = true; return true; }
  std::string_view first(argv[1]);
  if (first == "help" || first == "--help" || first == "-h") {
    options.show_help = true;
    if (argc >= 3) options.help_command = argv[2];
    return argc <= 3;
  }
  if (first == "--version" || first == "-V") first = "version";
  options.command = std::string(first);
  if (options.command != "build" && options.command != "check" && options.command != "version") {
    cli_errorf("Stage-0 does not provide production command '", options.command, "'");
    cli_hint("use the Raz-written compiler for production tooling");
    return false;
  }

  bool path_seen = false;
  auto needs_value = [&](int& index, std::string_view option) -> std::optional<std::string_view> {
    if (index + 1 >= argc) { cli_errorf("option '", option, "' requires a value"); return std::nullopt; }
    return std::string_view(argv[++index]);
  };

  for (int i = 2; i < argc; ++i) {
    const std::string_view arg(argv[i]);
    if (arg == "--help" || arg == "-h") { options.show_help = true; options.help_command = options.command; }
    else if (arg == "--release") options.profile = "release";
    else if (arg == "--verbose" || arg == "-v") options.verbose = true;
    else if (arg == "--quiet" || arg == "-q") options.quiet = true;
    else if (arg == "--force") options.force = true;
    else if (arg == "--profile") { auto v = needs_value(i, arg); if (!v) return false; options.profile = *v; }
    else if (arg == "--target") { auto v = needs_value(i, arg); if (!v) return false; options.target = *v; }
    else if (arg == "--jobs" || arg == "-j") { auto v = needs_value(i, arg); if (!v || !parse_unsigned("--jobs", *v, options.jobs)) return false; }
    else if (arg == "--color") { auto v = needs_value(i, arg); if (!v || !parse_color(*v, options)) return false; }
    else if (arg == "--diagnostic-format") { auto v = needs_value(i, arg); if (!v || !parse_diagnostic_format(*v, options)) return false; }
    else if (arg == "--allow" || arg == "--warn" || arg == "--deny") {
      auto v = needs_value(i, arg); if (!v) return false;
      auto level = raz::compiler::DiagnosticLevel::warn;
      if (arg == "--allow") level = raz::compiler::DiagnosticLevel::allow;
      else if (arg == "--deny") level = raz::compiler::DiagnosticLevel::deny;
      options.diagnostic_policy.overrides.push_back({std::string(*v), level});
    } else if (!arg.empty() && arg.front() == '-') {
      cli_errorf("unexpected Stage-0 option '", arg, "'");
      return false;
    } else if (!path_seen && options.command != "version") {
      options.project = std::filesystem::path(arg);
      path_seen = true;
    } else {
      cli_errorf("unexpected argument '", arg, "'");
      return false;
    }
  }
  return true;
}
