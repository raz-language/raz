// Copyright 2026 Mario Vinciguerra
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>
#include "forge/frontend/diagnostic_engine.hpp"
#include "forge/frontend/symbol_table.hpp"

namespace forge::frontend {

struct FunctionSignature {
    ir::Type return_type{ir::TypeKind::void_};
    std::vector<ir::Type> parameters;
    SourceSpan declaration;
};

class SemanticContext {
public:
    SemanticContext(SourceManager& sources, DiagnosticEngine& diagnostics)
        : sources_(&sources), diagnostics_(&diagnostics) {}
    [[nodiscard]] bool declare_function(std::string name, FunctionSignature signature);
    [[nodiscard]] const FunctionSignature* find_function(std::string_view name) const noexcept;
    [[nodiscard]] SymbolTable& symbols() noexcept { return symbols_; }
    [[nodiscard]] SourceManager& sources() noexcept { return *sources_; }
    [[nodiscard]] DiagnosticEngine& diagnostics() noexcept { return *diagnostics_; }
private:
    SourceManager* sources_{};
    DiagnosticEngine* diagnostics_{};
    SymbolTable symbols_;
    std::unordered_map<std::string, FunctionSignature> functions_;
};

} // namespace forge::frontend
