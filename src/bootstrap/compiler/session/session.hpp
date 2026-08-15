// Copyright 2026 Mario Vinciguerra
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "compiler/diagnostics/diagnostic.hpp"

#include <cstdint>
#include <filesystem>
#include <string>

namespace raz::compiler {

struct SessionOptions final {
  std::filesystem::path input;
  std::filesystem::path output;
  std::string target_triple;
  unsigned optimization_level = 0;
  bool emit_forge_ir = false;
  bool emit_tokens = false;
  bool emit_ast = false;
  bool emit_hir = false;
  bool emit_mir = false;
  bool check_only = false;
  DiagnosticFormat diagnostic_format = DiagnosticFormat::human;
  DiagnosticPolicy diagnostic_policy;
  std::filesystem::path diagnostic_output;
  std::filesystem::path diagnostic_display_path;
  std::int64_t diagnostic_line_delta = 0;
  std::int64_t diagnostic_byte_delta = 0;
  bool suppress_success_output = false;
};

class Session final {
 public:
  explicit Session(SessionOptions options);
  [[nodiscard]] const SessionOptions& options() const noexcept;

 private:
  SessionOptions options_;
};

}  // namespace raz::compiler
