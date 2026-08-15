// Copyright 2026 Mario Vinciguerra
// SPDX-License-Identifier: Apache-2.0

#include "forge/diagnostics/format.hpp"
#include <algorithm>
#include <sstream>

namespace forge::diagnostics {
namespace {
struct Position { std::size_t line{1}; std::size_t column{1}; std::size_t line_begin{}; std::size_t line_end{}; };

Position locate(std::string_view source, std::size_t offset) {
    Position position;
    offset = std::min(offset, source.size());
    for (std::size_t index = 0; index < offset; ++index) {
        if (source[index] == '\n') {
            ++position.line;
            position.column = 1;
            position.line_begin = index + 1;
        } else {
            ++position.column;
        }
    }
    position.line_end = source.find('\n', position.line_begin);
    if (position.line_end == std::string_view::npos) position.line_end = source.size();
    if (position.line_end > position.line_begin && source[position.line_end - 1] == '\r') --position.line_end;
    return position;
}

bool has_source_range(const Diagnostic& diagnostic, std::string_view source) {
    if (source.empty()) return false;
    if (diagnostic.range.begin > source.size() || diagnostic.range.end > source.size()) return false;
    if (diagnostic.range.end < diagnostic.range.begin) return false;
    return diagnostic.range.begin != 0 || diagnostic.range.end != 0;
}
}

std::string_view severity_name(DiagnosticSeverity severity) noexcept {
    switch (severity) {
    case DiagnosticSeverity::note: return "note";
    case DiagnosticSeverity::warning: return "warning";
    case DiagnosticSeverity::error: return "error";
    }
    return "error";
}

std::string render(const Diagnostic& diagnostic, RenderOptions options) {
    std::ostringstream out;
    const bool ranged = has_source_range(diagnostic, options.source);
    if (!options.file_name.empty()) out << options.file_name;
    if (ranged) {
        const auto position = locate(options.source, diagnostic.range.begin);
        if (!options.file_name.empty()) out << ':';
        out << position.line << ':' << position.column;
    }

    if (!options.file_name.empty() || ranged) out << ": ";
    out << severity_name(diagnostic.severity) << ": " << diagnostic.message << '\n';
    if (!ranged || !options.show_source) return out.str();

    const auto position = locate(options.source, diagnostic.range.begin);
    const auto line = options.source.substr(position.line_begin, position.line_end - position.line_begin);
    const auto digits = std::to_string(position.line).size();
    out << std::string(digits, ' ') << " |\n";
    out << position.line << " | " << line << '\n';
    out << std::string(digits, ' ') << " | " << std::string(position.column - 1, ' ');
    const auto clamped_end = std::min(diagnostic.range.end, position.line_end);
    const auto width = std::max<std::size_t>(1, clamped_end > diagnostic.range.begin ? clamped_end - diagnostic.range.begin : 1);
    out << '^' << std::string(width - 1, '~') << '\n';
    return out.str();
}

std::string render_all(const Diagnostics& diagnostics, RenderOptions options) {
    std::string output;
    for (const auto& diagnostic : diagnostics) output += render(diagnostic, options);
    return output;
}

} // namespace forge::diagnostics
