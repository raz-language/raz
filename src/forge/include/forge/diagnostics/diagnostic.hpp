// Copyright 2026 Mario Vinciguerra
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include <cstddef>
#include <string>
#include <vector>
#include <utility>

namespace forge {

enum class DiagnosticSeverity { note, warning, error };

struct SourceRange {
    std::size_t begin{};
    std::size_t end{};
};

struct Diagnostic {
    Diagnostic() = default;
    Diagnostic(DiagnosticSeverity diagnostic_severity, std::string diagnostic_message,
               SourceRange diagnostic_range = {})
        : severity(diagnostic_severity), message(std::move(diagnostic_message)), range(diagnostic_range) {}
    DiagnosticSeverity severity{DiagnosticSeverity::error};
    std::string message;
    SourceRange range{};
    std::string source_file;
    std::size_t source_line{};
    std::size_t source_column{};
    std::size_t source_end_line{};
    std::size_t source_end_column{};
};

using Diagnostics = std::vector<Diagnostic>;

} // namespace forge
