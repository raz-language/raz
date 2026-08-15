// Copyright 2026 Mario Vinciguerra
// SPDX-License-Identifier: Apache-2.0

#include "compiler/driver/compiler.hpp"
#include "compiler/session/session.hpp"

#include <filesystem>
#include <iostream>
#include <string_view>

namespace {

void print_usage() {
  std::cerr << "usage: razc [options] <input.rz>\n"
            << "  --tokens | --ast | --hir | --mir | --forge-ir | --check\n"
            << "  --diagnostic-format <human|short|json>\n"
            << "  --diagnostic-output <path>\n"
            << "  --allow <code|category>  Suppress matching warnings\n"
            << "  --warn <code|category>   Restore matching warnings\n"
            << "  --deny <code|category>   Promote matching warnings to errors\n"
            << "  --deny-warnings           Promote all warnings to errors\n";
}

}  // namespace

int main(int argc, char** argv) {
  if (argc == 2 && std::string_view(argv[1]) == "--version") {
    std::cout << "razc 1.0.0\n";
    return 0;
  }

  raz::compiler::SessionOptions options;
  for (int index = 1; index < argc; ++index) {
    const std::string_view argument(argv[index]);
    if (argument == "--tokens") {
      options.emit_tokens = true;
    } else if (argument == "--ast") {
      options.emit_ast = true;
    } else if (argument == "--hir") {
      options.emit_hir = true;
    } else if (argument == "--mir") {
      options.emit_mir = true;
    } else if (argument == "--forge-ir") {
      options.emit_forge_ir = true;
    } else if (argument == "--check") {
      options.check_only = true;
    } else if (argument == "--diagnostic-format" && index + 1 < argc) {
      const std::string_view format(argv[++index]);
      if (format == "human") options.diagnostic_format = raz::compiler::DiagnosticFormat::human;
      else if (format == "short") options.diagnostic_format = raz::compiler::DiagnosticFormat::short_;
      else if (format == "json") {
        options.diagnostic_format = raz::compiler::DiagnosticFormat::json;
        options.suppress_success_output = true;
      } else {
        std::cerr << "razc: --diagnostic-format must be human, short, or json\n";
        return 2;
      }
    } else if (argument == "--diagnostic-output" && index + 1 < argc) {
      options.diagnostic_output = std::filesystem::path(argv[++index]);
      options.suppress_success_output = true;
    } else if ((argument == "--allow" || argument == "--warn" || argument == "--deny") && index + 1 < argc) {
      raz::compiler::DiagnosticLevel level = raz::compiler::DiagnosticLevel::warn;
      if (argument == "--allow") level = raz::compiler::DiagnosticLevel::allow;
      else if (argument == "--deny") level = raz::compiler::DiagnosticLevel::deny;
      options.diagnostic_policy.overrides.push_back({argv[++index], level});
    } else if (argument == "--deny-warnings") {
      options.diagnostic_policy.deny_warnings = true;
    } else if (argument == "--help" || argument == "-h") {
      print_usage();
      return 0;
    } else if (!argument.empty() && argument.front() == '-') {
      std::cerr << "razc: unknown option: " << argument << '\n';
      print_usage();
      return 2;
    } else if (options.input.empty()) {
      options.input = std::filesystem::path(argument);
    } else {
      std::cerr << "razc: only one input file is currently supported\n";
      return 2;
    }
  }

  if (options.input.empty()) {
    print_usage();
    return 2;
  }

  raz::compiler::Session session(std::move(options));
  return raz::compiler::Compiler{}.run(session);
}
