// Copyright 2026 Mario Vinciguerra
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>
#include "forge/frontend/source_manager.hpp"
#include "forge/ir/type.hpp"

namespace forge::frontend {

enum class SymbolKind { variable, parameter, function, type };

struct Symbol {
    SymbolKind kind{SymbolKind::variable};
    ir::Type type{ir::TypeKind::void_};
    std::string ir_value;
    SourceSpan declaration;
};

class SymbolTable {
public:
    SymbolTable() { push_scope(); }
    void push_scope() { scopes_.emplace_back(); }
    void pop_scope();
    [[nodiscard]] bool declare(std::string name, Symbol symbol);
    [[nodiscard]] const Symbol* lookup(std::string_view name) const noexcept;
    [[nodiscard]] Symbol* lookup(std::string_view name) noexcept;
    [[nodiscard]] std::size_t depth() const noexcept { return scopes_.size(); }
private:
    using Scope = std::unordered_map<std::string, Symbol>;
    std::vector<Scope> scopes_;
};

class ScopeGuard {
public:
    explicit ScopeGuard(SymbolTable& table) : table_(&table) { table_->push_scope(); }
    ~ScopeGuard() { if (table_) table_->pop_scope(); }
    ScopeGuard(const ScopeGuard&) = delete;
    ScopeGuard& operator=(const ScopeGuard&) = delete;
    ScopeGuard(ScopeGuard&& other) noexcept : table_(other.table_) { other.table_ = nullptr; }
private:
    SymbolTable* table_{};
};

} // namespace forge::frontend
