// Copyright 2026 Mario Vinciguerra
// SPDX-License-Identifier: Apache-2.0

#include "forge/frontend/symbol_table.hpp"
#include <stdexcept>

namespace forge::frontend {
void SymbolTable::pop_scope() {
    if (scopes_.size() <= 1) throw std::logic_error("cannot pop the root Forge frontend scope");
    scopes_.pop_back();
}

bool SymbolTable::declare(std::string name, Symbol symbol) {
    return scopes_.back().emplace(std::move(name), std::move(symbol)).second;
}

const Symbol* SymbolTable::lookup(std::string_view name) const noexcept {
    for (auto it = scopes_.rbegin(); it != scopes_.rend(); ++it) {
        const auto found = it->find(std::string(name));
        if (found != it->end()) return &found->second;
    }
    return nullptr;
}

Symbol* SymbolTable::lookup(std::string_view name) noexcept {
    for (auto it = scopes_.rbegin(); it != scopes_.rend(); ++it) {
        const auto found = it->find(std::string(name));
        if (found != it->end()) return &found->second;
    }
    return nullptr;
}
} // namespace forge::frontend
