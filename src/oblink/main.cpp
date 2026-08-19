// Copyright 2026 Mario Vinciguerra
// SPDX-License-Identifier: Apache-2.0

#include <charconv>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#include "oblink/link.hpp"

#ifndef OBLINK_VERSION
#define OBLINK_VERSION "0.0.0-dev"
#endif

namespace {
bool parse_u64(std::string_view text, std::uint64_t& value) {
  const char* begin = text.data();
  const char* end = begin + text.size();
  int base = 10;
  if (text.size() > 2U && text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) {
    begin += 2;
    base = 16;
  }
  auto [ptr, ec] = std::from_chars(begin, end, value, base);
  return ec == std::errc{} && ptr == end;
}

void usage() {
  std::cout << "ObLink " OBLINK_VERSION " - native linker for Forge objects\n\n"
               "usage: oblink <input.obj|input.lib>... -o <program.exe> [options]\n\n"
               "options:\n"
               "  -o <path>             output PE32+ executable\n"
               "  -L<path>, -L <path>  add COFF library search directory\n"
               "  -l<name>, -l <name>  link name.lib from search paths\n"
               "  --entry <symbol>      explicit PE entry symbol (disables inference)\n"
               "  --no-crt-startup       do not infer *CRTStartup from linked libraries\n"
               "  --stack <bytes>        PE stack reserve (default: 1048576)\n"
               "  --heap <bytes>         PE heap reserve (default: 1048576)\n"
               "  --subsystem console   console subsystem (default)\n"
               "  --verbose             report archive selection and section layout\n"
               "  --map <path>          write a section/symbol link map\n"
               "  --version             print version\n"
               "  --help                show this help\n";
}
}

int main(int argc, char** argv) {
  oblink::LinkOptions options;
  std::vector<std::filesystem::path> inputs;
  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];
    if (arg == "--help" || arg == "-h") { usage(); return 0; }
    if (arg == "--version") { std::cout << "oblink " OBLINK_VERSION "\n"; return 0; }
    if (arg == "-o") {
      if (++i >= argc) { std::cerr << "oblink: error: -o requires a path\n"; return 2; }
      options.output = argv[i]; continue;
    }
    if (arg == "-L") {
      if (++i >= argc) { std::cerr << "oblink: error: -L requires a path\n"; return 2; }
      options.library_paths.emplace_back(argv[i]); continue;
    }
    if (arg.rfind("-L", 0) == 0 && arg.size() > 2U) {
      options.library_paths.emplace_back(arg.substr(2)); continue;
    }
    if (arg == "-l") {
      if (++i >= argc) { std::cerr << "oblink: error: -l requires a name\n"; return 2; }
      options.libraries.emplace_back(argv[i]); continue;
    }
    if (arg.rfind("-l", 0) == 0 && arg.size() > 2U) {
      options.libraries.emplace_back(arg.substr(2)); continue;
    }
    if (arg == "--entry") {
      if (++i >= argc) { std::cerr << "oblink: error: --entry requires a symbol\n"; return 2; }
      options.entry = argv[i]; options.infer_crt_startup = false; continue;
    }
    if (arg == "--no-crt-startup") { options.infer_crt_startup = false; continue; }
    if (arg == "--verbose") { options.verbose = true; continue; }
    if (arg == "--map") {
      if (++i >= argc) { std::cerr << "oblink: error: --map requires a path\n"; return 2; }
      options.map_output = argv[i]; continue;
    }
    if (arg == "--stack" || arg == "--heap") {
      if (++i >= argc) { std::cerr << "oblink: error: " << arg << " requires a byte count\n"; return 2; }
      std::uint64_t value{};
      if (!parse_u64(argv[i], value) || value == 0U) {
        std::cerr << "oblink: error: invalid " << arg << " byte count: " << argv[i] << '\n'; return 2;
      }
      if (arg == "--stack") options.stack_reserve = value;
      else options.heap_reserve = value;
      continue;
    }
    if (arg == "--subsystem") {
      if (++i >= argc) { std::cerr << "oblink: error: --subsystem requires a value\n"; return 2; }
      std::string value = argv[i];
      if (value == "console") options.subsystem = 3;
      else if (value == "windows") options.subsystem = 2;
      else { std::cerr << "oblink: error: unsupported subsystem: " << value << '\n'; return 2; }
      continue;
    }
    if (!arg.empty() && arg[0] == '-') {
      std::cerr << "oblink: error: unknown option: " << arg << '\n'; return 2;
    }
    inputs.emplace_back(arg);
  }
  if (options.output.empty()) { std::cerr << "oblink: error: output path is required (-o)\n"; return 2; }
  auto result = oblink::link(inputs, options);
  for (const auto& line : result.trace) std::cerr << "oblink: " << line << '\n';
  if (!result.ok()) {
    for (const auto& diagnostic : result.diagnostics)
      std::cerr << "oblink: error: " << diagnostic.message << '\n';
    return 1;
  }
  // A linker that succeeded has nothing to say. link.exe and lld-link are silent
  // here too, and the build driver should own its progress output rather than
  // have the linker interleave a banner into the middle of it.
  if (options.verbose)
    std::cerr << "oblink: wrote PE32+ AMD64 " << result.output_bytes << " bytes to "
              << result.output.string() << '\n';
  return 0;
}
