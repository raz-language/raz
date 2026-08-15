// Copyright 2026 Mario Vinciguerra
// SPDX-License-Identifier: Apache-2.0

bool use_color(std::ostream& stream) { return raz::terminal::color_enabled(stream, g_color_mode); }

void cli_status(std::string_view label, std::string_view text,
                std::string_view color = raz::terminal::green) {
  if (g_quiet) return;
  const bool enabled = use_color(std::cout);
  if (enabled) std::cout << raz::terminal::bold << color;
  std::cout << std::setw(10) << std::right << label;
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

template <typename... Parts>
void cli_errorf(Parts&&... parts) {
  std::ostringstream message;
  (message << ... << std::forward<Parts>(parts));
  cli_error(message.str());
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
};

void usage() {
  std::cout << "Raz 1.0.0 project driver\n\n"
            << "usage: raz <version|new|check|build|run|test|bench|profile|coverage|fuzz|clean|metadata|graph|doctor|cache|fmt|lint|doc|spec|diagnostics|lock|verify|sbom|audit|package|publish|install|uninstall|lsp> [path] [options]\n\n"
            << "options:\n"
            << "  --profile <name>   Select manifest build profile (default: debug)\n"
            << "  --release          Alias for --profile release\n"
            << "  --target <triple>  Select output target (default: host)\n"
            << "  --force            Rebuild all modules\n"
            << "  --verbose          Print every module action\n"
            << "  --jobs <n>         Parallel module/compiler jobs (default: auto, max 64)\n"
            << "  --quiet, -q        Print only errors and requested program output\n"
            << "  --color <mode>     Color output: auto, always, or never\n"
            << "  --diagnostic-format <human|short|json>\n"
            << "  --allow <code|category>  Suppress matching compiler warnings\n"
            << "  --warn <code|category>   Restore matching compiler warnings\n"
            << "  --deny <code|category>   Promote matching compiler warnings to errors\n"
            << "  --check            Check formatting without writing\n"
            << "  --deny-warnings    Make lint warnings fail the command\n"
            << "  --iterations <n>   Benchmark iterations (default: 10)\n"
            << "  --cases <n>        Fuzz cases (default: 100)\n"
            << "  --seed <n>         Deterministic fuzz seed\n"
            << "  --prefix <path>    Installation prefix for install/uninstall\n"
            << "  --list             List discovered tests without executing\n"
            << "  --prune            Remove project compiler caches\n"
            << "  --report <path>    Write an additional JSON test report\n"
            << "  --registry <path>  Local package registry for publish\n";
}

bool parse(int argc, char** argv, Options& options) {
  if (argc < 2) return false;
  options.command = argv[1];
  bool path_seen = false;
  for (int i = 2; i < argc; ++i) {
    const std::string_view arg(argv[i]);
    if (arg == "--release") options.profile = "release";
    else if (arg == "--verbose" || arg == "-v") options.verbose = true;
    else if (arg == "--quiet" || arg == "-q") options.quiet = true;
    else if (arg == "--color" && i + 1 < argc) {
      const std::string_view mode(argv[++i]);
      if (mode == "auto") options.color = raz::terminal::ColorMode::auto_;
      else if (mode == "always") options.color = raz::terminal::ColorMode::always;
      else if (mode == "never") options.color = raz::terminal::ColorMode::never;
      else { cli_error("--color must be auto, always, or never"); return false; }
    }

    else if (arg == "--diagnostic-format" && i + 1 < argc) {
      const std::string_view format(argv[++i]);
      if (format == "human") options.diagnostic_format = DiagnosticFormat::human;
      else if (format == "short") options.diagnostic_format = DiagnosticFormat::short_;
      else if (format == "json") options.diagnostic_format = DiagnosticFormat::json;
      else { cli_error("--diagnostic-format must be human, short, or json"); return false; }
    }

    else if (arg == "--jobs" && i + 1 < argc) {
      try { options.jobs = static_cast<unsigned>(std::stoul(argv[++i])); }
      catch (...) { cli_error("invalid --jobs value"); return false; }
      if (options.jobs == 0 || options.jobs > 64) { cli_error("--jobs must be 1..64"); return false; }
    }

    else if (arg == "--force") options.force = true;
    else if (arg == "--check") options.tool_check = true;
    else if ((arg == "--allow" || arg == "--warn" || arg == "--deny") && i + 1 < argc) {
      raz::compiler::DiagnosticLevel level = raz::compiler::DiagnosticLevel::warn;
      if (arg == "--allow") level = raz::compiler::DiagnosticLevel::allow;
      else if (arg == "--deny") level = raz::compiler::DiagnosticLevel::deny;
      options.diagnostic_policy.overrides.push_back({argv[++i], level});
    }

    else if (arg == "--deny-warnings") { options.deny_warnings = true; options.diagnostic_policy.deny_warnings = true; }
    else if (arg == "--list") options.list_tests = true;
    else if (arg == "--prune") options.prune_cache = true;
    else if (arg == "--profile" && i + 1 < argc) options.profile = argv[++i];
    else if (arg == "--target" && i + 1 < argc) options.target = argv[++i];
    else if (arg == "--prefix" && i + 1 < argc) options.prefix = std::filesystem::path(argv[++i]);
    else if (arg == "--report" && i + 1 < argc) options.report_path = std::filesystem::path(argv[++i]);
    else if (arg == "--registry" && i + 1 < argc) options.registry = std::filesystem::path(argv[++i]);
    else if (arg == "--iterations" && i + 1 < argc) {
      try { options.iterations = static_cast<unsigned>(std::stoul(argv[++i])); }
      catch (...) { cli_error("invalid --iterations value"); return false; }
      if (options.iterations == 0 || options.iterations > 100000) { cli_error("--iterations must be 1..100000"); return false; }
    }

    else if (arg == "--cases" && i + 1 < argc) {
      try { options.cases = static_cast<unsigned>(std::stoul(argv[++i])); }
      catch (...) { cli_error("invalid --cases value"); return false; }
      if (options.cases == 0 || options.cases > 100000) { cli_error("--cases must be 1..100000"); return false; }
    }

    else if (arg == "--seed" && i + 1 < argc) {
      try { options.seed = std::stoull(argv[++i], nullptr, 0); }
      catch (...) { cli_error("invalid --seed value"); return false; }
    }

    else if (!arg.empty() && arg.front() == '-') { cli_error(std::string("unknown option: ") + std::string(arg)); return false; }
    else if (!path_seen) { options.project = std::filesystem::path(arg); path_seen = true; }
    else { cli_error(std::string("unexpected argument: ") + std::string(arg)); return false; }
  }
  return true;
}
