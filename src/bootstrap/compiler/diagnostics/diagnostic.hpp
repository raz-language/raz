// Copyright 2026 Mario Vinciguerra
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "compiler/source/source_location.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace raz::compiler {

enum class DiagnosticSeverity : std::uint8_t { note, warning, error, fatal };
enum class DiagnosticFormat : std::uint8_t { human, short_, json };
enum class DiagnosticLevel : std::uint8_t { allow, warn, deny };

struct DiagnosticOverride final {
  std::string pattern;
  DiagnosticLevel level = DiagnosticLevel::warn;
};

struct DiagnosticPolicy final {
  bool deny_warnings = false;
  std::vector<DiagnosticOverride> overrides;
};

struct DiagnosticLabel final {
  SourceRange range{};
  std::string message;
  bool primary = false;
};

struct DiagnosticFix final {
  SourceRange range{};
  std::string replacement;
  std::string message;
};

struct Diagnostic final {
  DiagnosticSeverity severity = DiagnosticSeverity::error;
  std::string code;
  std::string message;
  std::vector<DiagnosticLabel> labels;
  std::vector<std::string> notes;
  std::vector<std::string> help;
  std::vector<DiagnosticFix> fixes;
};

}  // namespace raz::compiler
