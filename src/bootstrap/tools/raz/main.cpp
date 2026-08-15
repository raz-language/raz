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


// Command implementation is grouped by responsibility; main.cpp remains the entry point.
#include "detail/cli_options.hpp"
#include "detail/build_driver.hpp"
#include "detail/project_commands.hpp"
#include "detail/lsp_semantics.hpp"
#include "detail/auxiliary_commands.hpp"
#include "detail/lsp_server.hpp"
#include "detail/dispatch_helpers.hpp"

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
  if (!parse(argc, argv, options) || options.command == "help" || options.command == "--help") { usage(); return argc < 2 ? 2 : 0; }
  g_color_mode = options.color;
  g_quiet = options.quiet || options.diagnostic_format == DiagnosticFormat::json;
  configure_child_color(options.color);
  if (options.command == "version" || options.command == "--version") {
    std::cout << "raz 1.0.0\n";
    return 0;
  }

  if (options.command == "new") return create_project(options);
  if (options.command == "lsp") return run_lsp();
  if (options.command == "diagnostics") return diagnostic_catalog(options);
  ProjectGraph graph;
  ProjectError error;
  if (!raz::compiler::discover_project(options.project, graph, error)) { cli_error(error.message); return 1; }
  const auto workspace_state_path = graph.manifest.root / ".raz" / "cache" / "workspace-v1.state";
  const auto previous_workspace = WorkspaceGraph::load(workspace_state_path);
  const auto current_workspace = raz::compiler::build_workspace_graph(graph);
  const auto workspace_delta = previous_workspace.has_value()
      ? previous_workspace->compare(current_workspace)
      : raz::compiler::WorkspaceDelta{.added = [&] { std::set<std::string> values; for (const auto& [key, _] : current_workspace.modules) values.insert(key); return values; }(),
                                       .dirty = [&] { std::set<std::string> values; for (const auto& [key, _] : current_workspace.modules) values.insert(key); return values; }()};
  if (options.verbose && !workspace_delta.empty()) {
    cli_status("Workspace", std::to_string(workspace_delta.dirty.size()) + " dirty module(s), graph " +
        hex(current_workspace.structure_fingerprint), raz::terminal::magenta);
  }

  if (options.command == "metadata") return metadata(graph);
  if (options.command == "graph") return dependency_graph(graph);
  if (options.command == "doctor") return doctor_project(graph);
  if (options.command == "cache") return cache_project(graph, options);
  if (options.command == "lock") return write_lockfile(graph);
  if (options.command == "verify") return verify_project(graph);
  if (options.command == "sbom") return generate_sbom(graph, false);
  if (options.command == "audit") return generate_sbom(graph, true);
  if (options.command == "bench") return benchmark_project(graph, options);
  if (options.command == "profile") return profile_project(graph);
  if (options.command == "coverage") return coverage_project(graph);
  if (options.command == "fuzz") return fuzz_project(graph, options);
  if (options.command == "package") return package_project(graph, options);
  if (options.command == "publish") return publish_project(graph, options);
  if (options.command == "install") return install_project(graph, options, false);
  if (options.command == "uninstall") return install_project(graph, options, true);
  if (options.command == "fmt") return format_project(graph, options);
  if (options.command == "lint") {
    SessionOptions session; session.input = graph.manifest.entry; session.check_only = true;
    if (Compiler{}.run(Session(std::move(session))) != 0) return 1;
    return lint_project(graph, options);
  }

  if (options.command == "doc") return document_project(graph);
  if (options.command == "spec") return emit_language_spec(graph);
  if (options.command == "clean") {
    std::filesystem::remove_all(graph.manifest.root / "target");
    std::filesystem::remove_all(graph.manifest.root / ".raz");
    cli_status("Cleaned", graph.manifest.name, raz::terminal::green); return 0;
  }

  if (write_lockfile(graph, true) != 0) return 1;
  const bool check_only = options.command == "check";
  if (!check_only && options.command != "build" && options.command != "run" && options.command != "test") {
    cli_errorf("unknown command: ", options.command); usage(); return 2;
  }
  const auto start = std::chrono::steady_clock::now();
  std::set<std::filesystem::path> built;
  std::size_t compiled = 0, fresh = 0;
  std::vector<std::string> diagnostic_reports;
  auto* diagnostic_sink = check_only && options.diagnostic_format == DiagnosticFormat::json
      ? &diagnostic_reports : nullptr;
  const bool build_ok = build_graph(graph, options, check_only, built, compiled, fresh, diagnostic_sink);
  if (check_only && options.diagnostic_format == DiagnosticFormat::json) {
    std::cout << "{\"schema\":\"raz-project-diagnostics-v1\",\"package\":\""
              << json_escape(graph.manifest.name) << "\",\"success\":" << (build_ok ? "true" : "false")
              << ",\"reports\":[";
    for (std::size_t i = 0; i < diagnostic_reports.size(); ++i) {
      if (i != 0) std::cout << ',';
      const auto report = trim_copy(diagnostic_reports[i]);
      std::cout << (report.empty() ? "{\"schema\":\"raz-diagnostics-v1\",\"error_count\":0,\"diagnostics\":[]}" : report);
    }
    std::cout << "]}\n";
    if (build_ok && !current_workspace.save(workspace_state_path)) return 1;
    return build_ok ? 0 : 1;
  }

  if (!build_ok) return 1;
  if (!current_workspace.save(workspace_state_path)) {
    cli_errorf("failed to persist workspace graph ", workspace_state_path);
    return 1;
  }

  if (options.command == "test") {
    const auto tests = graph.manifest.root / "tests";
    std::vector<std::filesystem::path> discovered_tests;
    if (std::filesystem::is_directory(tests)) {
      for (const auto& candidate : std::filesystem::recursive_directory_iterator(tests))
        if (candidate.is_regular_file() && candidate.path().extension() == ".rz") discovered_tests.push_back(candidate.path());
      std::sort(discovered_tests.begin(), discovered_tests.end());
      if (options.list_tests) { for (const auto& test : discovered_tests) std::cout << test.lexically_relative(graph.manifest.root).generic_string() << '\n'; return 0; }
      struct TestResult { std::string name; long long elapsed_ms; bool passed; };
      std::vector<TestResult> test_results;
      for (const auto& test_path : discovered_tests) {
        const auto& entry_path = test_path;
        const auto test_root = graph.manifest.root / "target" / options.target / options.profile / "tests" / entry_path.stem();
        std::filesystem::create_directories(test_root);
        const auto test_ir = test_root / "test.fir";
#if defined(_WIN32)
        const auto test_object = test_root / "test.obj";
        const auto test_binary = test_root / "test.exe";
#else
        const auto test_object = test_root / "test.o";
        const auto test_binary = test_root / "test";
#endif
        SessionOptions session; session.input = entry_path; session.emit_forge_ir = true; session.output = test_ir;
        if (Compiler{}.run(Session(std::move(session))) != 0) return 1;
        const auto& profile = graph.manifest.profiles.at(options.profile);
        if (!emit_native_object(test_ir, test_object, profile.optimization_level)) return 1;
        const std::string command = native_link_command(test_object, test_binary);
        if (execute_shell_command(command) != 0) return 1;
        const auto test_start = std::chrono::steady_clock::now();
        const bool passed = execute_shell_command(shell_quote(test_binary)) == 0;
        const auto test_ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - test_start).count();
        test_results.push_back({entry_path.lexically_relative(graph.manifest.root).generic_string(), test_ms, passed});
        if (!g_quiet || !passed) cli_status(passed ? "PASS" : "FAIL", test_results.back().name + " (" + std::to_string(test_ms) + " ms)", passed ? raz::terminal::green : raz::terminal::red);
      }
      const auto result_root = graph.manifest.root / "target" / "test-results"; std::filesystem::create_directories(result_root);
      std::ostringstream json; json << "{\n  \"tests\": [\n";
      std::ostringstream junit; junit << "<?xml version=\"1.0\"?><testsuite tests=\"" << test_results.size() << "\">";
      bool all_passed = true;
      for (std::size_t i=0;i<test_results.size();++i) { const auto& r=test_results[i]; all_passed &= r.passed;
        json << "    {\"name\":\"" << json_escape(r.name) << "\",\"passed\":" << (r.passed?"true":"false") << ",\"elapsed_ms\":" << r.elapsed_ms << "}" << (i+1==test_results.size()?"":",") << '\n';
        junit << "<testcase name=\"" << html_escape(r.name) << "\" time=\"" << (r.elapsed_ms/1000.0) << "\">" << (r.passed?"":"<failure/>") << "</testcase>"; }
      json << "  ]\n}\n"; junit << "</testsuite>\n";
      write_text_file(result_root / "results.json", json.str()); write_text_file(result_root / "junit.xml", junit.str());
      if (!options.report_path.empty()) write_text_file(options.report_path, json.str());
      if (!all_passed) return 1;
    }
  }
  const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
  if (!g_quiet) {
    std::ostringstream summary;
    summary << graph.manifest.name << " [" << options.profile << ", " << options.target << "] ("
            << compiled << " compiled, " << fresh << " fresh) in " << elapsed << " ms";
    cli_status(check_only ? "Checked" : "Finished", summary.str(), raz::terminal::green);
  }

  if (options.command == "run") {
    if (graph.manifest.kind != raz::compiler::PackageKind::executable) {
      cli_error("only executable packages can be run");
      return 2;
    }
    return execute_shell_command(shell_quote(native_artifact_path(graph, options)));
  }
  return 0;
}
