// Copyright 2026 Mario Vinciguerra
// SPDX-License-Identifier: Apache-2.0

#include "forge/runtime/bindings.hpp"
#include "forge/target/data_layout.hpp"

#include <algorithm>
#include <unordered_map>
#include <utility>

namespace forge::runtime {
namespace {
void add_error(Diagnostics& diagnostics, std::string message) {
    diagnostics.push_back({DiagnosticSeverity::error, std::move(message), {}});
}
}

Diagnostics BindingRegistry::bind(std::string name, Binding binding) {
    Diagnostics diagnostics;
    if (name.empty()) {
        add_error(diagnostics, "runtime binding name cannot be empty");
        return diagnostics;
    }

    if (binding.native_address == 0 && !binding.interpreter) {
        add_error(diagnostics, "runtime binding @" + name + " has no native address or interpreter implementation");
        return diagnostics;
    }

    if (!bindings_.emplace(name, std::move(binding)).second)
        add_error(diagnostics, "duplicate runtime binding @" + name);
    return diagnostics;
}

Diagnostics BindingRegistry::bind_callback(std::string name, std::string signature_name, Binding binding) {
    Diagnostics diagnostics;
    if (signature_name.empty()) {
        add_error(diagnostics, "runtime callback @" + name + " requires a signature name");
        return diagnostics;
    }
    binding.signature_name = std::move(signature_name);
    return bind(std::move(name), std::move(binding));
}

Diagnostics BindingRegistry::bind_global(std::string name, GlobalBinding binding) {
    Diagnostics diagnostics;
    if (name.empty()) { add_error(diagnostics, "runtime global binding name cannot be empty"); return diagnostics; }
    if (!binding.address || binding.size == 0) { add_error(diagnostics, "runtime global binding @" + name + " requires non-empty storage"); return diagnostics; }
    if (binding.alignment == 0 || (binding.alignment & (binding.alignment - 1U)) != 0) {
        add_error(diagnostics, "runtime global binding @" + name + " requires power-of-two alignment"); return diagnostics;
    }

    if (reinterpret_cast<std::uintptr_t>(binding.address) % binding.alignment != 0) {
        add_error(diagnostics, "runtime global binding @" + name + " address is misaligned"); return diagnostics;
    }

    if (!globals_.emplace(name, std::move(binding)).second) add_error(diagnostics, "duplicate runtime global binding @" + name);
    return diagnostics;
}

bool BindingRegistry::contains_global(std::string_view name) const noexcept {
    return globals_.find(std::string(name)) != globals_.end();
}

bool BindingRegistry::contains(std::string_view name) const noexcept {
    return bindings_.find(std::string(name)) != bindings_.end();
}

std::optional<std::uintptr_t> BindingRegistry::resolve_native(std::string_view name) const noexcept {
    const auto found = bindings_.find(std::string(name));
    if (found == bindings_.end() || found->second.native_address == 0) return std::nullopt;
    return found->second.native_address;
}

interpreter::ExternalMap BindingRegistry::interpreter_map() const {
    interpreter::ExternalMap result;
    for (const auto& [name, binding] : bindings_)
        if (binding.interpreter) result.emplace(name, binding.interpreter);
    return result;
}

codegen::x86_64::ExternalResolver BindingRegistry::native_resolver() const {
    return [this](std::string_view name) { return resolve_native(name); };
}

codegen::x86_64::ExternalResolver BindingRegistry::native_global_resolver() const {
    return [this](std::string_view name) -> std::optional<std::uintptr_t> {
        const auto found = globals_.find(std::string(name));
        if (found == globals_.end()) return std::nullopt;
        return reinterpret_cast<std::uintptr_t>(found->second.address);
    };
}

interpreter::ExternalGlobalMap BindingRegistry::interpreter_globals() const {
    interpreter::ExternalGlobalMap result;
    for (const auto& [name, binding] : globals_)
        result.emplace(name, interpreter::ExternalGlobal{binding.address, binding.size, binding.read_only});
    return result;
}

Diagnostics BindingRegistry::validate(const ir::Module& module) const {
    Diagnostics diagnostics;
    for (const auto& global : module.globals()) {
        if (!global.is_external) continue;
        const auto found = globals_.find(global.name);
        if (found == globals_.end()) { add_error(diagnostics, "missing runtime binding for external global @" + global.name); continue; }
        const auto& binding = found->second;
        const auto scalar_size = global.type.kind() == ir::TypeKind::i64 ? 8U :
                                 global.type.kind() == ir::TypeKind::i32 ? 4U :
                                 global.type.kind() == ir::TypeKind::i16 ? 2U : 1U;
        const auto layout = target::DataLayout::host();
        const auto aggregate_size = global.is_named_aggregate()
            ? layout.aggregate_size(module, global.aggregate_kind, global.aggregate_name)
            : std::optional<std::size_t>{};
        const auto aggregate_alignment = global.is_named_aggregate()
            ? layout.aggregate_alignment(module, global.aggregate_kind, global.aggregate_name)
            : std::optional<std::size_t>{};
        const auto required = global.is_named_aggregate() ? aggregate_size.value_or(0)
            : (global.element_count != 1 ? global.element_count * scalar_size : scalar_size);
        if (!global.is_named_aggregate() && binding.type != global.type) add_error(diagnostics, "runtime global type mismatch for @" + global.name);
        if (binding.size < required) add_error(diagnostics, "runtime global storage is too small for @" + global.name);
        const auto required_alignment = global.alignment != 0 ? global.alignment : aggregate_alignment.value_or(0);
        if (required_alignment != 0 && binding.alignment < required_alignment)
            add_error(diagnostics, "runtime global alignment is too small for @" + global.name);
        if (global.is_constant && !binding.read_only) add_error(diagnostics, "external constant @" + global.name + " requires a read-only binding");
        if (!global.is_constant && binding.read_only) add_error(diagnostics, "external writable global @" + global.name + " cannot use read-only storage");
    }

    std::unordered_map<std::string_view, const ir::Function*> signatures;
    signatures.reserve(module.functions().size());
    for (const auto& function : module.functions())
        if (function.is_signature) signatures.emplace(function.name, &function);

    for (const auto& [name, binding] : bindings_) {
        if (binding.signature_name.empty()) continue;
        const auto signature_it = signatures.find(binding.signature_name);
        if (signature_it == signatures.end()) {
            add_error(diagnostics, "runtime callback @" + name + " references unknown signature @" + binding.signature_name);
            continue;
        }
        const auto& function = *signature_it->second;
        const auto& signature = binding.signature;
        if (signature.result != function.return_type || signature.result_owned != function.return_owned ||
            signature.result_borrow_mode != function.return_borrow_mode ||
            signature.result_borrow_parameter != function.return_borrow_parameter ||
            signature.result_aggregate_kind != function.return_aggregate_kind ||
            signature.result_aggregate_name != function.return_aggregate_name ||
            signature.parameters.size() != function.parameters.size()) {
            add_error(diagnostics, "runtime callback signature mismatch for @" + name);
            continue;
        }
        for (std::size_t index = 0; index < signature.parameters.size(); ++index) {
            const auto& parameter = function.parameters[index];
            const auto kind = signature.parameter_aggregate_kinds.empty() ? ir::AggregateRefKind::scalar : signature.parameter_aggregate_kinds[index];
            const auto aggregate_name = signature.parameter_aggregate_names.empty() ? std::string{} : signature.parameter_aggregate_names[index];
            const auto owned = signature.parameter_owned.empty() ? false : signature.parameter_owned[index];
            const auto borrow = signature.parameter_borrow_modes.empty() ? ir::BorrowMode::none : signature.parameter_borrow_modes[index];
            if (signature.parameters[index] != parameter.type || kind != parameter.aggregate_kind ||
                aggregate_name != parameter.aggregate_name || owned != parameter.owned || borrow != parameter.borrow_mode) {
                add_error(diagnostics, "runtime callback parameter mismatch for @" + name + " at index " + std::to_string(index));
            }
        }
    }

    for (const auto& function : module.functions()) {
        if (!function.is_external || function.is_signature) continue;
        const auto found = bindings_.find(function.name);
        if (found == bindings_.end()) {
            add_error(diagnostics, "missing runtime binding for external function @" + function.name);
            continue;
        }
        const auto& signature = found->second.signature;
        if (signature.result != function.return_type) {
            add_error(diagnostics, "runtime binding return type mismatch for @" + function.name);
        }
        if (signature.result_owned != function.return_owned) {
            add_error(diagnostics, "runtime binding return ownership mismatch for @" + function.name);
        }
        if (signature.result_borrow_mode != function.return_borrow_mode) {
            add_error(diagnostics, "runtime binding return borrow mode mismatch for @" + function.name);
        }
        if (signature.result_borrow_parameter != function.return_borrow_parameter) {
            add_error(diagnostics, "runtime binding return borrow source mismatch for @" + function.name);
        }
        if (function.returns_aggregate()) {
            if (signature.result_aggregate_kind != function.return_aggregate_kind ||
                signature.result_aggregate_name != function.return_aggregate_name) {
                add_error(diagnostics, "runtime binding aggregate return mismatch for @" + function.name);
            }
        } else if (signature.result_aggregate_kind != ir::AggregateRefKind::scalar || !signature.result_aggregate_name.empty()) {
            add_error(diagnostics, "runtime binding declares aggregate metadata for scalar return @" + function.name);
        }
        if (signature.parameters.size() != function.parameters.size()) {
            add_error(diagnostics, "runtime binding parameter count mismatch for @" + function.name);
            continue;
        }
        const bool has_aggregate_kinds = !signature.parameter_aggregate_kinds.empty();
        const bool has_aggregate_names = !signature.parameter_aggregate_names.empty();
        const bool has_ownership = !signature.parameter_owned.empty();
        const bool has_borrow_modes = !signature.parameter_borrow_modes.empty();
        if ((has_aggregate_kinds && signature.parameter_aggregate_kinds.size() != signature.parameters.size()) ||
            (has_aggregate_names && signature.parameter_aggregate_names.size() != signature.parameters.size()) ||
            (has_ownership && signature.parameter_owned.size() != signature.parameters.size()) ||
            (has_borrow_modes && signature.parameter_borrow_modes.size() != signature.parameters.size())) {
            add_error(diagnostics, "runtime binding aggregate parameter metadata count mismatch for @" + function.name);
            continue;
        }
        for (std::size_t index = 0; index < signature.parameters.size(); ++index) {
            const auto& parameter = function.parameters[index];
            if (signature.parameters[index] != parameter.type) {
                add_error(diagnostics, "runtime binding parameter type mismatch for @" + function.name +
                    " at index " + std::to_string(index));
            }
            const auto binding_kind = has_aggregate_kinds ? signature.parameter_aggregate_kinds[index] : ir::AggregateRefKind::scalar;
            const auto binding_name = has_aggregate_names ? signature.parameter_aggregate_names[index] : std::string{};
            const auto binding_owned = has_ownership ? signature.parameter_owned[index] : false;
            const auto binding_borrow_mode = has_borrow_modes ? signature.parameter_borrow_modes[index] : ir::BorrowMode::none;
            if (binding_kind != parameter.aggregate_kind || binding_name != parameter.aggregate_name) {
                add_error(diagnostics, "runtime binding aggregate parameter mismatch for @" + function.name +
                    " at index " + std::to_string(index));
            }
            if (binding_owned != parameter.owned) {
                add_error(diagnostics, "runtime binding parameter ownership mismatch for @" + function.name +
                    " at index " + std::to_string(index));
            }
            if (binding_borrow_mode != parameter.borrow_mode) {
                add_error(diagnostics, "runtime binding parameter borrow-mode mismatch for @" + function.name +
                    " at index " + std::to_string(index));
            }
        }
    }
    return diagnostics;
}

} // namespace forge::runtime
