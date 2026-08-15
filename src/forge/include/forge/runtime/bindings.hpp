// Copyright 2026 Mario Vinciguerra
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "forge/codegen/x86_64/encoder.hpp"
#include "forge/diagnostics/diagnostic.hpp"
#include "forge/interpreter/interpreter.hpp"
#include "forge/ir/module.hpp"

namespace forge::runtime {

struct Signature {
    std::vector<ir::Type> parameters;
    std::vector<ir::AggregateRefKind> parameter_aggregate_kinds;
    std::vector<std::string> parameter_aggregate_names;
    std::vector<bool> parameter_owned;
    std::vector<ir::BorrowMode> parameter_borrow_modes;
    ir::Type result{ir::TypeKind::void_};
    ir::AggregateRefKind result_aggregate_kind{ir::AggregateRefKind::scalar};
    std::string result_aggregate_name;
    bool result_owned{};
    ir::BorrowMode result_borrow_mode{ir::BorrowMode::none};
    std::int32_t result_borrow_parameter{-1};
};

struct Binding {
    Signature signature;
    std::string signature_name;
    std::uintptr_t native_address{};
    interpreter::ExternalFunction interpreter;
};

struct GlobalBinding {
    ir::Type type{ir::TypeKind::void_};
    void* address{};
    std::size_t size{};
    std::size_t alignment{1};
    bool read_only{};
};

class BindingRegistry {
public:
    [[nodiscard]] Diagnostics bind(std::string name, Binding binding);
    [[nodiscard]] Diagnostics bind_callback(std::string name, std::string signature_name, Binding binding);
    [[nodiscard]] Diagnostics bind_global(std::string name, GlobalBinding binding);
    [[nodiscard]] bool contains(std::string_view name) const noexcept;
    [[nodiscard]] bool contains_global(std::string_view name) const noexcept;
    [[nodiscard]] std::optional<std::uintptr_t> resolve_native(std::string_view name) const noexcept;
    [[nodiscard]] interpreter::ExternalMap interpreter_map() const;
    [[nodiscard]] codegen::x86_64::ExternalResolver native_resolver() const;
    [[nodiscard]] codegen::x86_64::ExternalResolver native_global_resolver() const;
    [[nodiscard]] interpreter::ExternalGlobalMap interpreter_globals() const;
    [[nodiscard]] Diagnostics validate(const ir::Module& module) const;

private:
    std::unordered_map<std::string, Binding> bindings_;
    std::unordered_map<std::string, GlobalBinding> globals_;
};

} // namespace forge::runtime
