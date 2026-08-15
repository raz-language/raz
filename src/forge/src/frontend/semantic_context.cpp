// Copyright 2026 Mario Vinciguerra
// SPDX-License-Identifier: Apache-2.0

#include "forge/frontend/semantic_context.hpp"

namespace forge::frontend {
bool SemanticContext::declare_function(std::string name, FunctionSignature signature) {
    const auto [it, inserted] = functions_.emplace(name, signature);
    if (!inserted) {
        auto& diagnostic = diagnostics_->report(DiagnosticSeverity::error, "F1001", signature.declaration,
                                                "duplicate function '" + name + "'");
        diagnostics_->note(diagnostic, "the function was already declared in this module");
    }
    return inserted;
}

const FunctionSignature* SemanticContext::find_function(std::string_view name) const noexcept {
    const auto found = functions_.find(std::string(name));
    return found == functions_.end() ? nullptr : &found->second;
}
} // namespace forge::frontend
