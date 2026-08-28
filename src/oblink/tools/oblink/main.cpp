// Copyright 2026 Mario Vinciguerra
// SPDX-License-Identifier: Apache-2.0

#include <charconv>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "oblink/link.hpp"

#ifndef OBLINK_VERSION
#define OBLINK_VERSION "0.0.0-dev"
#endif

namespace {
std::vector<std::string> parse_response_file(const std::filesystem::path& path, std::string& error) {
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    error = "unable to open response file: " + path.string();
    return {};
  }
  std::string text((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
  std::vector<std::string> arguments;
  std::string current;
  bool quoted = false;
  auto flush = [&]() {
    if (!current.empty()) {
      arguments.push_back(current);
      current.clear();
    }
  };
  for (std::size_t index = 0; index < text.size(); ++index) {
    const char c = text[index];
    if (c == '"') {
      quoted = !quoted;
      continue;
    }
    // Preserve ordinary Windows path separators. Only treat a backslash as an
    // escape when it protects a quote inside a quoted response-file argument.
    if (quoted && c == '\\' && index + 1U < text.size() && text[index + 1U] == '"') {
      current.push_back('"');
      ++index;
      continue;
    }
    if (!quoted && (c == ' ' || c == '\t' || c == '\r' || c == '\n')) {
      flush();
      continue;
    }
    current.push_back(c);
  }
  if (quoted) {
    error = "unterminated quote in response file: " + path.string();
    return {};
  }
  flush();
  return arguments;
}

bool expand_response_arguments(const std::vector<std::string>& source,
                               std::vector<std::string>& destination,
                               std::string& error,
                               unsigned depth = 0) {
  if (depth > 8U) {
    error = "response-file nesting exceeds 8 levels";
    return false;
  }
  for (const auto& argument : source) {
    if (argument.size() <= 1U || argument.front() != '@') {
      destination.push_back(argument);
      continue;
    }
    auto nested = parse_response_file(std::filesystem::path(argument.substr(1)), error);
    if (!error.empty()) return false;
    if (!expand_response_arguments(nested, destination, error, depth + 1U)) return false;
  }
  return true;
}

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
               "  @<file>               read additional arguments from a response file\n"
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
  std::vector<std::string> raw_arguments;
  raw_arguments.reserve(argc > 1 ? static_cast<std::size_t>(argc - 1) : 0U);
  for (int i = 1; i < argc; ++i) raw_arguments.emplace_back(argv[i]);
  std::vector<std::string> arguments;
  std::string response_error;
  if (!expand_response_arguments(raw_arguments, arguments, response_error)) {
    std::cerr << "oblink: error: " << response_error << '\n';
    return 2;
  }
  for (std::size_t i = 0; i < arguments.size(); ++i) {
    std::string arg = arguments[i];
    if (arg == "--help" || arg == "-h") { usage(); return 0; }
    if (arg == "--version") { std::cout << "oblink " OBLINK_VERSION "\n"; return 0; }
    if (arg == "-o") {
      if (++i >= arguments.size()) { std::cerr << "oblink: error: -o requires a path\n"; return 2; }
      options.output = arguments[i]; continue;
    }
    if (arg == "-L") {
      if (++i >= arguments.size()) { std::cerr << "oblink: error: -L requires a path\n"; return 2; }
      options.library_paths.emplace_back(arguments[i]); continue;
    }
    if (arg.rfind("-L", 0) == 0 && arg.size() > 2U) {
      options.library_paths.emplace_back(arg.substr(2)); continue;
    }
    if (arg == "-l") {
      if (++i >= arguments.size()) { std::cerr << "oblink: error: -l requires a name\n"; return 2; }
      options.libraries.emplace_back(arguments[i]); continue;
    }
    if (arg.rfind("-l", 0) == 0 && arg.size() > 2U) {
      options.libraries.emplace_back(arg.substr(2)); continue;
    }
    if (arg == "--entry") {
      if (++i >= arguments.size()) { std::cerr << "oblink: error: --entry requires a symbol\n"; return 2; }
      options.entry = arguments[i]; options.infer_crt_startup = false; continue;
    }
    if (arg == "--no-crt-startup") { options.infer_crt_startup = false; continue; }
    // Compiler drivers conventionally forward -pthread to both compilation and
    // link steps. PE/COFF has no direct linker switch for it, so accept it as
    // a no-op compatibility flag rather than rejecting an otherwise valid link.
    if (arg == "-pthread") { continue; }
    if (arg == "--verbose") { options.verbose = true; continue; }
    if (arg == "--map") {
      if (++i >= arguments.size()) { std::cerr << "oblink: error: --map requires a path\n"; return 2; }
      options.map_output = arguments[i]; continue;
    }
    if (arg == "--stack" || arg == "--heap") {
      if (++i >= arguments.size()) { std::cerr << "oblink: error: " << arg << " requires a byte count\n"; return 2; }
      std::uint64_t value{};
      if (!parse_u64(arguments[i], value) || value == 0U) {
        std::cerr << "oblink: error: invalid " << arg << " byte count: " << arguments[i] << '\n'; return 2;
      }
      if (arg == "--stack") options.stack_reserve = value;
      else options.heap_reserve = value;
      continue;
    }
    if (arg == "--subsystem") {
      if (++i >= arguments.size()) { std::cerr << "oblink: error: --subsystem requires a value\n"; return 2; }
      std::string value = arguments[i];
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
