// Copyright 2026 Mario Vinciguerra
// SPDX-License-Identifier: Apache-2.0

#include "compiler/driver/compiler.hpp"
#include "compiler/project/project.hpp"
#include "compiler/project/workspace.hpp"
#include "compiler/session/session.hpp"
#include "common/terminal.hpp"

#include <forge/diagnostics/format.hpp>
#include <forge/ir/parser.hpp>
#include <forge/ir/verifier.hpp>
#include <forge/machine/lower.hpp>
#include <forge/object/archive.hpp>
#include <forge/object/coff.hpp>
#include <forge/object/elf.hpp>
#include <forge/object/macho.hpp>
#include <forge/pass/pipeline.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdlib>
#include <deque>
#include <filesystem>
#include <fstream>
#include <functional>
#include <future>
#include <iomanip>
#include <iostream>
#include <map>
#include <set>
#include <thread>
#include <tuple>
#include <sstream>
#include <unordered_map>
#include <optional>
#include <cctype>
#include <string>
#include <string_view>
#include <vector>
#if defined(_WIN32) || defined(__GLIBC__)
#include <malloc.h>
#endif
#if !defined(_WIN32)
#include <sys/wait.h>
#endif

namespace {
using raz::compiler::BuildProfile;
using raz::compiler::Compiler;
using raz::compiler::DiagnosticFormat;
using raz::compiler::ProjectError;
using raz::compiler::ProjectGraph;
using raz::compiler::WorkspaceGraph;
using raz::compiler::Session;
using raz::compiler::SessionOptions;
using raz::compiler::TokenKind;

std::filesystem::path g_self_executable;
raz::terminal::ColorMode g_color_mode = raz::terminal::ColorMode::auto_;
bool g_quiet = false;


// Stage-0 keeps only the bootstrap build/check driver. Production commands live in Raz.
#include "detail/cli_options.hpp"
#include "detail/stage0_io.hpp"
#include "detail/build_driver.hpp"

}  // namespace

int main(int argc, char** argv) {
  if (argc > 0 && argv[0] != nullptr) g_self_executable = std::filesystem::path(argv[0]);
  if (argc == 13 && std::string_view(argv[1]) == "__compile-module") {
    SessionOptions session;
    session.input = std::filesystem::path(argv[2]);
    const std::string_view output = argv[3];
    session.target_triple = argv[4];
    try {
      session.optimization_level = std::stoi(argv[5]);
      session.check_only = std::stoi(argv[6]) != 0;
      const std::string_view format(argv[7]);
      if (format == "human") session.diagnostic_format = DiagnosticFormat::human;
      else if (format == "short") session.diagnostic_format = DiagnosticFormat::short_;
      else if (format == "json") session.diagnostic_format = DiagnosticFormat::json;
      else throw std::invalid_argument("diagnostic format");
      if (std::string_view(argv[8]) != "-") session.diagnostic_output = std::filesystem::path(argv[8]);
      if (std::string_view(argv[9]) != "-") session.diagnostic_display_path = std::filesystem::path(argv[9]);
      session.diagnostic_line_delta = std::stoll(argv[10]);
      session.diagnostic_byte_delta = std::stoll(argv[11]);
      session.diagnostic_policy = parse_diagnostic_policy(argv[12]);
      session.suppress_success_output = true;
    } catch (...) {
      cli_error("invalid internal compile-worker request");
      return 2;
    }
    if (!session.check_only) {
      session.emit_forge_ir = true;
      if (output != "-") session.output = std::filesystem::path(output);
    }
    return Compiler{}.run(Session(std::move(session)));
  }
  Options options;
  if (!parse(argc, argv, options)) return 2;
  g_color_mode = options.color;
  g_quiet = options.quiet || options.diagnostic_format == DiagnosticFormat::json;
  configure_child_color(options.color);
  if (options.show_help) {
    if (options.help_command.empty()) usage();
    else usage_command(options.help_command);
    return 0;
  }
  if (options.command == "version") {
    std::cout << "raz-stage0 1.0.0\n";
    return 0;
  }

  ProjectGraph graph;
  ProjectError error;
  if (!raz::compiler::discover_project(options.project, graph, error)) {
    cli_error(error.message);
    return 1;
  }

  const bool check_only = options.command == "check";
  const auto start = std::chrono::steady_clock::now();
  std::set<std::filesystem::path> built;
  std::size_t compiled = 0, fresh = 0;
  std::vector<std::string> diagnostic_reports;
  auto* diagnostic_sink = check_only && options.diagnostic_format == DiagnosticFormat::json
      ? &diagnostic_reports : nullptr;
  const bool build_ok = build_graph(graph, options, check_only, built, compiled, fresh, diagnostic_sink);

  if (check_only && options.diagnostic_format == DiagnosticFormat::json) {
    std::cout << "{\"schema\":\"raz-stage0-diagnostics-v1\",\"package\":\""
              << json_escape(graph.manifest.name) << "\",\"success\":" << (build_ok ? "true" : "false")
              << ",\"reports\":[";
    for (std::size_t i = 0; i < diagnostic_reports.size(); ++i) {
      if (i != 0) std::cout << ',';
      const auto report = diagnostic_reports[i];
      std::cout << (report.empty() ? "{\"schema\":\"raz-diagnostics-v1\",\"error_count\":0,\"diagnostics\":[]}" : report);
    }
    std::cout << "]}\n";
    return build_ok ? 0 : 1;
  }

  if (!build_ok) {
    cli_errorf("could not ", check_only ? "check" : "compile", " package '", graph.manifest.name, "'");
    return 1;
  }

  const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now() - start).count();
  if (!g_quiet) {
    std::cout << "    Finished `" << options.profile << "` profile ["
              << compiled << " compiled, " << fresh << " fresh] in "
              << std::fixed << std::setprecision(2) << (elapsed / 1000.0) << "s\n";
  }
  return 0;
}
