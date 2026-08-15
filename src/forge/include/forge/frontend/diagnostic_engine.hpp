// Copyright 2026 Mario Vinciguerra
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include <string>
#include <string_view>
#include <vector>
#include "forge/diagnostics/diagnostic.hpp"
#include "forge/frontend/source_manager.hpp"

namespace forge::frontend {

struct FixIt {
    SourceSpan span;
    std::string replacement;
};

struct FrontendDiagnostic {
    DiagnosticSeverity severity{DiagnosticSeverity::error};
    std::string code;
    std::string message;
    SourceSpan span;
    std::vector<std::string> notes;
    std::vector<FixIt> fixes;
};

class DiagnosticEngine {
public:
    explicit DiagnosticEngine(const SourceManager& sources) noexcept : sources_(&sources) {}
    FrontendDiagnostic& report(DiagnosticSeverity severity, std::string code,
                               SourceSpan span, std::string message);
    void note(FrontendDiagnostic& diagnostic, std::string message);
    void fix(FrontendDiagnostic& diagnostic, SourceSpan span, std::string replacement);
    [[nodiscard]] bool has_errors() const noexcept;
    [[nodiscard]] const std::vector<FrontendDiagnostic>& diagnostics() const noexcept { return diagnostics_; }
    [[nodiscard]] std::string render(bool color = false) const;
    void clear() noexcept { diagnostics_.clear(); }

private:
    const SourceManager* sources_{};
    std::vector<FrontendDiagnostic> diagnostics_;
};

} // namespace forge::frontend
