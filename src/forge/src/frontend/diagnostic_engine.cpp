// Copyright 2026 Mario Vinciguerra
// SPDX-License-Identifier: Apache-2.0

#include "forge/frontend/diagnostic_engine.hpp"
#include <algorithm>
#include <sstream>

namespace forge::frontend {
namespace {
std::string_view label(DiagnosticSeverity severity) {
    switch (severity) {
    case DiagnosticSeverity::note: return "note";
    case DiagnosticSeverity::warning: return "warning";
    case DiagnosticSeverity::error: return "error";
    }
    return "error";
}
}

FrontendDiagnostic& DiagnosticEngine::report(DiagnosticSeverity severity, std::string code,
                                             SourceSpan span, std::string message) {
    diagnostics_.push_back({severity, std::move(code), std::move(message), span, {}, {}});
    return diagnostics_.back();
}

void DiagnosticEngine::note(FrontendDiagnostic& diagnostic, std::string message) {
    diagnostic.notes.push_back(std::move(message));
}

void DiagnosticEngine::fix(FrontendDiagnostic& diagnostic, SourceSpan span, std::string replacement) {
    diagnostic.fixes.push_back({span, std::move(replacement)});
}

bool DiagnosticEngine::has_errors() const noexcept {
    return std::any_of(diagnostics_.begin(), diagnostics_.end(), [](const auto& d) {
        return d.severity == DiagnosticSeverity::error;
    });
}

std::string DiagnosticEngine::render(bool) const {
    std::ostringstream out;
    for (const auto& diagnostic : diagnostics_) {
        const auto start = sources_->position(diagnostic.span.source, diagnostic.span.begin);
        const auto finish = sources_->position(diagnostic.span.source, diagnostic.span.end);
        out << sources_->name(diagnostic.span.source) << ':' << start.line << ':' << start.column
            << ": " << label(diagnostic.severity);
        if (!diagnostic.code.empty()) out << '[' << diagnostic.code << ']';
        out << ": " << diagnostic.message << '\n';
        const auto line = sources_->line_text(diagnostic.span.source, start.line);
        if (!line.empty()) {
            out << "  |\n" << start.line << " | " << line << "\n  | ";
            for (std::size_t i = 1; i < start.column; ++i) out << ' ';
            const auto width = start.line == finish.line && finish.column > start.column
                                   ? finish.column - start.column : 1;
            for (std::size_t i = 0; i < width; ++i) out << '^';
            out << '\n';
        }
        for (const auto& note_text : diagnostic.notes) out << "  = note: " << note_text << '\n';
        for (const auto& fix : diagnostic.fixes)
            out << "  = help: replace `" << sources_->slice(fix.span) << "` with `" << fix.replacement << "`\n";
    }
    return out.str();
}

} // namespace forge::frontend
