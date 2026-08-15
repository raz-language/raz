// Copyright 2026 Mario Vinciguerra
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "compiler/diagnostics/diagnostic.hpp"

#include <cstddef>
#include <iosfwd>
#include <string>
#include <string_view>
#include <vector>

namespace raz::compiler {

class SourceManager;

class DiagnosticEngine final {
 public:
  void set_policy(DiagnosticPolicy policy);
  void report(Diagnostic diagnostic);
  void error(std::string code, SourceRange range, std::string message);
  void warning(std::string code, SourceRange range, std::string message);

  [[nodiscard]] bool has_errors() const noexcept;
  [[nodiscard]] std::size_t error_count() const noexcept;
  [[nodiscard]] const std::vector<Diagnostic>& diagnostics() const noexcept;
  void render(std::ostream& stream, const SourceManager& sources,
              DiagnosticFormat format = DiagnosticFormat::human) const;
  [[nodiscard]] std::string json(const SourceManager& sources) const;
  [[nodiscard]] std::string lsp_json(std::string_view uri,
                                     const SourceManager& sources) const;
  [[nodiscard]] std::string lsp_code_actions_json(
      std::string_view uri, const SourceManager& sources) const;
  void clear();

 private:
  std::vector<Diagnostic> diagnostics_;
  std::size_t error_count_ = 0;
  DiagnosticPolicy policy_;
};

}  // namespace raz::compiler
