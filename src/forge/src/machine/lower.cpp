// Copyright 2026 Mario Vinciguerra
// SPDX-License-Identifier: Apache-2.0

#include "forge/machine/lower.hpp"
#include "forge/machine/optimize.hpp"
#include "forge/target/data_layout.hpp"
#include "forge/target/abi.hpp"
#include "forge/machine/verifier.hpp"

#include <algorithm>
#include <charconv>
#include <cstring>
#include <cstdlib>
#include <functional>
#include <optional>
#include <cstdint>
#include <limits>
#include <string_view>
#include <unordered_map>

namespace forge::machine {
namespace {
void error(Diagnostics& diagnostics, std::string message) {
    diagnostics.push_back({DiagnosticSeverity::error, std::move(message), {}});
}

bool parse_i64(std::string_view text, std::int64_t& value) {
    bool negative = false;
    if (!text.empty() && (text.front() == '-' || text.front() == '+')) {
        negative = text.front() == '-';
        text.remove_prefix(1);
    }
    if (text.size() > 2 && text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) {
        text.remove_prefix(2);
        std::uint64_t raw{};
        const auto [pointer, ec] = std::from_chars(text.data(), text.data() + text.size(), raw, 16);
        if (ec != std::errc{} || pointer != text.data() + text.size()) return false;
        if (negative) {
            if (raw > (std::uint64_t{1} << 63)) return false;
            value = raw == (std::uint64_t{1} << 63) ? std::numeric_limits<std::int64_t>::min()
                                                     : -static_cast<std::int64_t>(raw);
        } else {
            if (raw > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) return false;
            value = static_cast<std::int64_t>(raw);
        }
        return true;
    }
    std::string owned;
    if (negative) owned.push_back('-');
    owned.append(text);
    const auto [pointer, ec] = std::from_chars(owned.data(), owned.data() + owned.size(), value);
    return ec == std::errc{} && pointer == owned.data() + owned.size();
}

bool parse_i32(std::string_view text, std::int64_t& value) {
    if (!parse_i64(text, value)) return false;
    return value >= std::numeric_limits<std::int32_t>::min() && value <= std::numeric_limits<std::int32_t>::max();
}

bool parse_u32(std::string_view text, std::uint32_t& value) {
    if (text.size() > 2 && text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) {
        text.remove_prefix(2);
        const auto [pointer, ec] = std::from_chars(text.data(), text.data() + text.size(), value, 16);
        return ec == std::errc{} && pointer == text.data() + text.size();
    }
    const auto [pointer, ec] = std::from_chars(text.data(), text.data() + text.size(), value);
    return ec == std::errc{} && pointer == text.data() + text.size();
}

bool parse_float_bits(std::string_view text, bool wide, std::int64_t& bits) {
    // Raz can preserve an integer source literal as a floating FIR constant
    // before an explicit numeric cast. Accept hexadecimal integer spelling here
    // so printed FIR round-trips through parse -> machine lowering.
    bool negative_hex = false;
    auto hex = text;
    if (!hex.empty() && (hex.front() == '-' || hex.front() == '+')) {
        negative_hex = hex.front() == '-';
        hex.remove_prefix(1);
    }
    double parsed{};
    if (hex.size() > 2 && hex[0] == '0' && (hex[1] == 'x' || hex[1] == 'X') &&
        hex.find_first_of(".pP") == std::string_view::npos) {
        hex.remove_prefix(2);
        std::uint64_t raw{};
        const auto [pointer, ec] = std::from_chars(hex.data(), hex.data() + hex.size(), raw, 16);
        if (ec != std::errc{} || pointer != hex.data() + hex.size()) return false;
        parsed = static_cast<double>(raw);
        if (negative_hex) parsed = -parsed;
    } else {
        std::string owned(text);
        char* tail = nullptr;
        parsed = std::strtod(owned.c_str(), &tail);
        if (tail != owned.c_str() + owned.size()) return false;
    }
    if (wide) {
        std::uint64_t raw{};
        std::memcpy(&raw, &parsed, sizeof(raw));
        bits = static_cast<std::int64_t>(raw);
    } else {
        const float narrowed = static_cast<float>(parsed);
        std::uint32_t raw{};
        std::memcpy(&raw, &narrowed, sizeof(raw));
        bits = static_cast<std::int64_t>(raw);
    }
    return true;
}

bool append_inputs(const ir::Operation& operation,
                   const std::unordered_map<std::string, VirtualRegister>& registers,
                   Instruction& instruction, Diagnostics& diagnostics, const std::string& function_name) {
    for (const auto& operand : operation.operands) {
        const auto found = registers.find(operand);
        if (found == registers.end()) {
            error(diagnostics, "undefined lowering operand '" + operand + "' in @" + function_name);
            return false;
        }
        instruction.inputs.push_back(found->second);
    }
    return true;
}

Opcode comparison_opcode(std::string_view opcode, bool wide) {
    if (wide) {
        if (opcode == "cmp.eq") return Opcode::cmp_eq_i64;
        if (opcode == "cmp.ne") return Opcode::cmp_ne_i64;
        if (opcode == "cmp.lt") return Opcode::cmp_lt_i64;
        if (opcode == "cmp.le") return Opcode::cmp_le_i64;
        if (opcode == "cmp.gt") return Opcode::cmp_gt_i64;
        if (opcode == "cmp.ge") return Opcode::cmp_ge_i64;
        if (opcode == "cmp.ult") return Opcode::cmp_ult_i64;
        if (opcode == "cmp.ule") return Opcode::cmp_ule_i64;
        if (opcode == "cmp.ugt") return Opcode::cmp_ugt_i64;
        return Opcode::cmp_uge_i64;
    }
    if (opcode == "cmp.eq") return Opcode::cmp_eq_i32;
    if (opcode == "cmp.ne") return Opcode::cmp_ne_i32;
    if (opcode == "cmp.lt") return Opcode::cmp_lt_i32;
    if (opcode == "cmp.le") return Opcode::cmp_le_i32;
    if (opcode == "cmp.gt") return Opcode::cmp_gt_i32;
    if (opcode == "cmp.ge") return Opcode::cmp_ge_i32;
    if (opcode == "cmp.ult") return Opcode::cmp_ult_i32;
    if (opcode == "cmp.ule") return Opcode::cmp_ule_i32;
    if (opcode == "cmp.ugt") return Opcode::cmp_ugt_i32;
    return Opcode::cmp_uge_i32;
}

struct StackAddress {
    std::uint32_t object_base{};
    std::uint32_t object_size{};
    std::uint32_t offset{};
};

constexpr TargetArchitecture host_architecture() noexcept {
#if defined(__aarch64__) || defined(_M_ARM64)
    return TargetArchitecture::aarch64;
#else
    return TargetArchitecture::x86_64;
#endif
}

constexpr target::NativeAbi host_native_abi() noexcept {
#if defined(_WIN32)
    return target::NativeAbi::windows_x64;
#elif defined(__aarch64__) || defined(_M_ARM64)
    return target::NativeAbi::aapcs64;
#else
    return target::NativeAbi::system_v_x86_64;
#endif
}

constexpr TargetArchitecture resolved_architecture(TargetArchitecture architecture) noexcept {
    return architecture == TargetArchitecture::host ? host_architecture() : architecture;
}

bool uses_native_aggregate_abi(const ir::Function& function) noexcept {
    return function.calling_convention == ir::CallingConvention::c ||
           function.calling_convention == ir::CallingConvention::system_v ||
           function.calling_convention == ir::CallingConvention::windows_x64;
}

target::NativeAbi function_native_abi(const ir::Function& function, TargetArchitecture architecture) noexcept {
    if (function.calling_convention == ir::CallingConvention::windows_x64)
        return target::NativeAbi::windows_x64;
    if (function.calling_convention == ir::CallingConvention::system_v)
        return target::NativeAbi::system_v_x86_64;
    const auto resolved = resolved_architecture(architecture);
    if (resolved == TargetArchitecture::aarch64 &&
        (function.calling_convention == ir::CallingConvention::platform ||
         function.calling_convention == ir::CallingConvention::c))
        return target::NativeAbi::aapcs64;
    return host_native_abi();
}

struct AggregateParameterLowering {
    bool direct{};
    target::AggregateAbiClassification classification;
};

RegisterClass abi_register_class(target::AbiValueClass value) noexcept {
    return value == target::AbiValueClass::sse ? RegisterClass::floating : RegisterClass::integer;
}

std::uint32_t encode_aggregate_return(const target::AggregateAbiClassification& classification) noexcept {
    std::uint32_t encoded = classification.register_count;
    for (std::size_t index = 0; index < classification.register_count; ++index) {
        if (classification.classes[index] == target::AbiValueClass::sse) encoded |= 1U << (8U + index);
        if (classification.piece_widths[index] == 4U) encoded |= 1U << (16U + index);
    }
    return encoded;
}

unsigned integer_width(ir::Type type) {
    switch (type.kind()) {
    case ir::TypeKind::i1: return 1;
    case ir::TypeKind::i8: return 8;
    case ir::TypeKind::i16: return 16;
    case ir::TypeKind::i32: return 32;
    case ir::TypeKind::i64: return 64;
    default: return 0;
    }
}
}

LowerResult lower_module(const ir::Module& source, LowerOptions options) {
    const auto target_architecture = resolved_architecture(options.architecture);
    LowerResult result;
    Module output;
    output.name = source.name();
    std::unordered_map<std::string, std::uint32_t> struct_field_offsets;
    std::unordered_map<std::string, std::uint32_t> struct_sizes;
    std::unordered_map<std::string, std::uint32_t> struct_alignments;
    std::unordered_map<std::string, std::uint32_t> array_strides;
    std::unordered_map<std::string, std::uint32_t> array_sizes;
    std::unordered_map<std::string, std::uint32_t> array_alignments;
    const auto host_layout = target::DataLayout::host();
    for (const auto& structure : source.structs()) {
        const auto layout = host_layout.struct_layout(source, structure);
        if (!layout) {
            error(result.diagnostics, "cannot lower structure @" + structure.name);
            continue;
        }
        struct_sizes.emplace(structure.name, static_cast<std::uint32_t>(layout->size));
        struct_alignments.emplace(structure.name, static_cast<std::uint32_t>(layout->alignment));
        for (std::size_t index = 0; index < layout->fields.size(); ++index) {
            struct_field_offsets.emplace(structure.name + "#" + std::to_string(index), static_cast<std::uint32_t>(layout->fields[index].offset));
            struct_field_offsets.emplace(structure.name + "." + structure.fields[index].name, static_cast<std::uint32_t>(layout->fields[index].offset));
        }
    }

    for (const auto& array : source.arrays()) {
        const auto layout = host_layout.array_layout(source, array);
        if (!layout || layout->stride > std::numeric_limits<std::uint32_t>::max() || layout->size > std::numeric_limits<std::uint32_t>::max()) {
            error(result.diagnostics, "cannot lower array @" + array.name);
            continue;
        }
        array_strides.emplace(array.name, static_cast<std::uint32_t>(layout->stride));
        array_sizes.emplace(array.name, static_cast<std::uint32_t>(layout->size));
        array_alignments.emplace(array.name, static_cast<std::uint32_t>(layout->alignment));
    }

    for (const auto& global : source.globals()) {
        const bool aggregate = global.element_count != 1 || global.is_named_aggregate();
        const bool callback_storage = !global.function_signature_name.empty();
        const bool wide = global.type == ir::Type(ir::TypeKind::i64) || global.type == ir::Type(ir::TypeKind::ptr);
        const auto named_size = global.is_named_aggregate()
            ? host_layout.aggregate_size(source, global.aggregate_kind, global.aggregate_name)
            : std::optional<std::size_t>{};
        const auto named_alignment = global.is_named_aggregate()
            ? host_layout.aggregate_alignment(source, global.aggregate_kind, global.aggregate_name)
            : std::optional<std::size_t>{};
        if (global.is_named_aggregate() && (!named_size || !named_alignment)) {
            error(result.diagnostics, "invalid named aggregate global @" + global.name);
            continue;
        }
        if (aggregate && !global.is_named_aggregate() && !callback_storage && global.type != ir::Type(ir::TypeKind::i8)) {
            error(result.diagnostics, "native byte-array globals require i8 elements in @" + global.name);
            continue;
        }
        if (!aggregate && !wide && global.type != ir::Type(ir::TypeKind::i32)) {
            error(result.diagnostics, "native scalar globals currently support i32/i64 only in @" + global.name);
            continue;
        }
        const auto size = global.is_named_aggregate() ? static_cast<std::uint32_t>(*named_size)
            : (callback_storage ? global.element_count * 8U : (aggregate ? global.element_count : (wide ? 8U : 4U)));
        std::vector<std::uint8_t> bytes(size, 0);
        if (!global.is_external && aggregate && !global.zero_initialized) {
            if (global.bytes.size() != size) {
                error(result.diagnostics, "invalid byte initializer in @" + global.name);
                continue;
            }
            bytes = global.bytes;
        } else if (!global.is_external && !aggregate && !global.zero_initialized) {
            std::int64_t initializer{};
            if (!(wide ? parse_i64(global.initializer, initializer) : parse_i32(global.initializer, initializer))) {
                error(result.diagnostics, "invalid native global initializer in @" + global.name);
                continue;
            }
            const auto bits = static_cast<std::uint64_t>(initializer);
            for (std::uint32_t index = 0; index < size; ++index)
                bytes[index] = static_cast<std::uint8_t>(bits >> (index * 8U));
        }
        const auto alignment = global.alignment != 0 ? global.alignment
            : (global.is_named_aggregate() ? static_cast<std::uint32_t>(*named_alignment) : (aggregate ? 1U : size));
        output.globals.push_back({global.name, size, alignment, global.is_constant, global.is_external, global.is_thread_local, global.linkage == ir::SymbolLinkage::internal, std::move(bytes)});
    }

    std::unordered_map<std::string, const ir::Function*> function_table;
    for (const auto& item : source.functions()) function_table.emplace(item.name, &item);

    for (const auto& function : source.functions()) {
        if (function.is_external || function.is_signature) continue;
        if (function.return_type != ir::Type(ir::TypeKind::i1) &&
            function.return_type != ir::Type(ir::TypeKind::i8) &&
            function.return_type != ir::Type(ir::TypeKind::i16) &&
            function.return_type != ir::Type(ir::TypeKind::i32) &&
            function.return_type != ir::Type(ir::TypeKind::i64) &&
            function.return_type != ir::Type(ir::TypeKind::ptr) &&
            function.return_type != ir::Type(ir::TypeKind::f32) &&
            function.return_type != ir::Type(ir::TypeKind::f64) &&
            function.return_type != ir::Type(ir::TypeKind::void_)) {
            error(result.diagnostics, "machine lowering currently supports integer, floating, ptr, or void return type in @" + function.name);
            continue;
        }

        bool failed = false;
        Function lowered;
        lowered.name = function.name;
        lowered.target_feature = function.target_feature;
        if (target_architecture == TargetArchitecture::x86_64 && lowered.target_feature == "neon") {
            error(result.diagnostics, "function @" + function.name + " requires neon on an x86-64 target");
            continue;
        }
        if (target_architecture == TargetArchitecture::aarch64 &&
            (lowered.target_feature == "sse2" || lowered.target_feature == "avx2")) {
            error(result.diagnostics, "function @" + function.name + " requires x86 SIMD on an AArch64 target");
            continue;
        }
        std::vector<AggregateParameterLowering> aggregate_parameters(function.parameters.size());
        std::optional<target::AggregateAbiClassification> aggregate_return;
        bool direct_native_return = false;
        if (function.return_owned) {
            aggregate_return = target::classify_aggregate(source, function.return_aggregate_kind,
                function.return_aggregate_name, function_native_abi(function, target_architecture), host_layout);
            if (!aggregate_return) {
                error(result.diagnostics, "cannot classify aggregate return in @" + function.name);
                continue;
            }
            direct_native_return = uses_native_aggregate_abi(function) && aggregate_return->register_passed() &&
                                   !aggregate_return->returned_indirectly;
            lowered.indirect_result_parameter = !direct_native_return &&
                function_native_abi(function, target_architecture) == target::NativeAbi::aapcs64;
            if (!direct_native_return) {
                lowered.argument_widths.push_back(8U);
                lowered.argument_classes.push_back(RegisterClass::integer);
            }
        }
        for (std::size_t parameter_index = 0; parameter_index < function.parameters.size(); ++parameter_index) {
            const auto& parameter = function.parameters[parameter_index];
            if (parameter.is_aggregate()) {
                const auto classified = target::classify_aggregate(source, parameter.aggregate_kind,
                    parameter.aggregate_name, function_native_abi(function, target_architecture), host_layout);
                if (!classified) {
                    error(result.diagnostics, "cannot classify aggregate parameter " + parameter.name + " in @" + function.name);
                    failed = true;
                    break;
                }
                aggregate_parameters[parameter_index].classification = *classified;
                aggregate_parameters[parameter_index].direct = uses_native_aggregate_abi(function) && classified->register_passed();
                // Internal Raz aggregate ABI passes aggregate values by pointer.
                // Only native-ABI direct aggregates should be decomposed into
                // register classes; otherwise Result<f64, E> can incorrectly
                // consume an XMM argument slot while the callee expects a pointer.
                if (aggregate_parameters[parameter_index].direct) {
                    for (std::size_t piece = 0; piece < classified->register_count; ++piece) {
                        lowered.argument_widths.push_back(8U);
                        lowered.argument_classes.push_back(abi_register_class(classified->classes[piece]));
                    }
                    continue;
                }
            }
            lowered.argument_widths.push_back(parameter.type == ir::Type(ir::TypeKind::f64) || parameter.type == ir::Type(ir::TypeKind::i64) || parameter.type == ir::Type(ir::TypeKind::ptr) ? 8U : 4U);
            lowered.argument_classes.push_back(parameter.type.is_float() ? RegisterClass::floating : RegisterClass::integer);
        }
        if (failed) continue;
        lowered.argument_count = static_cast<std::uint32_t>(lowered.argument_widths.size());
        std::unordered_map<std::string, VirtualRegister> registers;
        std::unordered_map<std::string, ir::Type> value_types;
        std::unordered_map<std::string, StackAddress> addresses;
        std::unordered_map<std::string, std::uint32_t> callback_storage_counts;
        for (const auto& parameter : function.parameters) value_types.emplace(parameter.name, parameter.type);
        for (const auto& block : function.blocks) {
            for (const auto& parameter : block.parameters) value_types.emplace(parameter.name, parameter.type);
            for (const auto& operation : block.operations) {
                if (!operation.result.empty()) value_types.emplace(operation.result,
                    operation.opcode.starts_with("cmp.") ? ir::Type(ir::TypeKind::i1) : operation.type);
            }
        }
        const auto allocate_register = [&](ir::Type type) {
            const auto reg = lowered.register_count++;
            const bool wide = type == ir::Type(ir::TypeKind::i64) || type == ir::Type(ir::TypeKind::ptr) || type == ir::Type(ir::TypeKind::f64);
            lowered.register_widths.push_back(wide ? 8U : 4U);
            lowered.register_classes.push_back(type.is_float() ? RegisterClass::floating : RegisterClass::integer);
            return reg;
        };
        // Owned aggregate return storage is always disjoint from the callee's
        // local aggregate image. Copy fixed-size return payloads directly in
        // machine IR so emitted standalone objects do not acquire a private
        // __forge_memmove runtime dependency merely to satisfy the backend ABI.
        const auto emit_aggregate_return_copy = [&](Block& block, VirtualRegister destination, VirtualRegister source, std::uint32_t byte_count) {
            std::uint32_t offset = 0;
            while (offset < byte_count) {
                const std::uint32_t remaining = byte_count - offset;
                const std::uint32_t width = remaining >= 8U ? 8U : remaining >= 4U ? 4U : remaining >= 2U ? 2U : 1U;
                const auto type = width == 8U ? ir::Type(ir::TypeKind::i64) : width == 4U ? ir::Type(ir::TypeKind::i32) : width == 2U ? ir::Type(ir::TypeKind::i16) : ir::Type(ir::TypeKind::i8);
                const auto load_opcode = width == 8U ? Opcode::load_ptr_i64 : width == 4U ? Opcode::load_ptr_i32 : width == 2U ? Opcode::load_ptr_i16 : Opcode::load_ptr_i8;
                const auto store_opcode = width == 8U ? Opcode::store_ptr_i64 : width == 4U ? Opcode::store_ptr_i32 : width == 2U ? Opcode::store_ptr_i16 : Opcode::store_ptr_i8;
                const auto value = allocate_register(type);
                block.instructions.push_back({load_opcode, value, {source}, static_cast<std::int64_t>(offset), 0, {}, {}});
                block.instructions.push_back({store_opcode, 0, {value, destination}, static_cast<std::int64_t>(offset), 0, {}, {}});
                offset += width;
            }
        };
        std::optional<VirtualRegister> owned_result_register;
        if (function.return_owned && !direct_native_return) owned_result_register = allocate_register(ir::Type(ir::TypeKind::ptr));

        for (std::size_t index = 0; index < function.parameters.size(); ++index) {
            const auto& parameter = function.parameters[index];
            if (!parameter.is_aggregate() && parameter.type != ir::Type(ir::TypeKind::i8) && parameter.type != ir::Type(ir::TypeKind::i16) && parameter.type != ir::Type(ir::TypeKind::i32) && parameter.type != ir::Type(ir::TypeKind::i64) && parameter.type != ir::Type(ir::TypeKind::i1) && parameter.type != ir::Type(ir::TypeKind::ptr) && parameter.type != ir::Type(ir::TypeKind::f32) && parameter.type != ir::Type(ir::TypeKind::f64)) {
                error(result.diagnostics, "machine lowering supports only scalar or named aggregate parameters in @" + function.name);
                failed = true;
                break;
            }
            registers.emplace(parameter.name, allocate_register(parameter.is_aggregate() ? ir::Type(ir::TypeKind::ptr) : parameter.type));
        }
        if (failed) continue;

        for (const auto& block : function.blocks) {
            Block machine_block;
            machine_block.name = block.name;
            for (const auto& parameter : block.parameters) {
                if (parameter.type != ir::Type(ir::TypeKind::i8) && parameter.type != ir::Type(ir::TypeKind::i16) && parameter.type != ir::Type(ir::TypeKind::i32) && parameter.type != ir::Type(ir::TypeKind::i64) && parameter.type != ir::Type(ir::TypeKind::i1) && parameter.type != ir::Type(ir::TypeKind::ptr) && parameter.type != ir::Type(ir::TypeKind::f32) && parameter.type != ir::Type(ir::TypeKind::f64)) {
                    error(result.diagnostics, "machine lowering supports only scalar block parameters in @" + function.name);
                    failed = true;
                    break;
                }
                const auto reg = allocate_register(parameter.type);
                registers.emplace(parameter.name, reg);
                machine_block.parameters.push_back(reg);
            }
            if (failed) break;
            lowered.blocks.push_back(std::move(machine_block));
        }
        if (failed) continue;

        std::vector<std::vector<std::optional<StackAddress>>> owned_block_storage(function.blocks.size());
        for (std::size_t block_index = 0; block_index < function.blocks.size() && !failed; ++block_index) {
            owned_block_storage[block_index].resize(function.blocks[block_index].parameters.size());
            for (std::size_t parameter_index = 0; parameter_index < function.blocks[block_index].parameters.size(); ++parameter_index) {
                const auto& parameter = function.blocks[block_index].parameters[parameter_index];
                if (!parameter.owned) continue;
                const auto& sizes = parameter.aggregate_kind == ir::AggregateRefKind::structure ? struct_sizes : array_sizes;
                const auto& alignments = parameter.aggregate_kind == ir::AggregateRefKind::structure ? struct_alignments : array_alignments;
                const auto size_it = sizes.find(parameter.aggregate_name);
                const auto alignment_it = alignments.find(parameter.aggregate_name);
                if (size_it == sizes.end() || alignment_it == alignments.end()) {
                    error(result.diagnostics, "invalid owned aggregate block parameter layout in @" + function.name);
                    failed = true;
                    break;
                }
                const auto base = (lowered.local_stack_size + alignment_it->second - 1U) & ~(alignment_it->second - 1U);
                const auto allocated = (size_it->second + 3U) & ~3U;
                owned_block_storage[block_index][parameter_index] = StackAddress{base, allocated, 0};
                lowered.local_stack_size = base + allocated;
            }
        }
        if (failed) continue;

        // IR blocks are not required to be stored in dominance order.  Build a
        // reverse-postorder traversal for lowering so values defined in a
        // dominating block are materialized before uses in dominated blocks.
        // Preserve the original block vector for emission; only the lowering
        // visitation order changes.
        std::unordered_map<std::string, std::size_t> block_indices;
        for (std::size_t index = 0; index < function.blocks.size(); ++index)
            block_indices.emplace(function.blocks[index].name, index);
        std::vector<std::uint8_t> lowering_visited(function.blocks.size(), 0U);
        std::vector<std::size_t> lowering_order;
        const auto append_rpo_component = [&](std::size_t root) {
            std::vector<std::size_t> postorder;
            std::function<void(std::size_t)> visit = [&](std::size_t index) {
                if (index >= function.blocks.size() || lowering_visited[index]) return;
                lowering_visited[index] = 1U;
                for (const auto& operation : function.blocks[index].operations) {
                    for (const auto& successor : operation.successors) {
                        const auto found = block_indices.find(successor);
                        if (found != block_indices.end()) visit(found->second);
                    }
                }
                postorder.push_back(index);
            };
            visit(root);
            for (auto index = postorder.rbegin(); index != postorder.rend(); ++index)
                lowering_order.push_back(*index);
        };
        if (!function.blocks.empty()) append_rpo_component(0);
        for (std::size_t index = 0; index < function.blocks.size(); ++index)
            if (!lowering_visited[index]) append_rpo_component(index);

        auto& entry = lowered.blocks.front();
        if (owned_result_register)
            entry.instructions.push_back({Opcode::load_argument_i64, *owned_result_register, {}, 0, 0, {}, {}});
        std::uint32_t argument_index = function.return_owned && !direct_native_return ? 1U : 0U;
        for (std::size_t index = 0; index < function.parameters.size(); ++index) {
            const auto& parameter = function.parameters[index];
            const auto& aggregate = aggregate_parameters[index];
            if (aggregate.direct) {
                const auto alignment = static_cast<std::uint32_t>(aggregate.classification.alignment);
                const auto allocated = static_cast<std::uint32_t>((aggregate.classification.size + 7U) & ~7U);
                const auto base = (lowered.local_stack_size + alignment - 1U) & ~(alignment - 1U);
                lowered.local_stack_size = base + allocated;
                const auto destination = registers.at(parameter.name);
                entry.instructions.push_back({Opcode::load_stack_address, destination, {},
                    -static_cast<std::int64_t>(base + allocated), 0, {}, {}});
                std::uint32_t piece_offset = 0;
                for (std::size_t piece = 0; piece < aggregate.classification.register_count; ++piece) {
                    const bool floating = aggregate.classification.classes[piece] == target::AbiValueClass::sse;
                    const auto width = aggregate.classification.piece_widths[piece] == 0U
                        ? std::uint8_t{8} : aggregate.classification.piece_widths[piece];
                    const auto value_type = floating && width == 4U ? ir::Type(ir::TypeKind::f32)
                        : floating ? ir::Type(ir::TypeKind::f64) : ir::Type(ir::TypeKind::i64);
                    const auto value = allocate_register(value_type);
                    entry.instructions.push_back({floating ? (width == 4U ? Opcode::load_argument_f32 : Opcode::load_argument_f64)
                                                                : Opcode::load_argument_i64,
                        value, {}, 0, argument_index++, {}, {}});
                    entry.instructions.push_back({floating ? (width == 4U ? Opcode::store_ptr_f32 : Opcode::store_ptr_f64)
                                                               : Opcode::store_ptr_i64,
                        0, {value, destination}, static_cast<std::int64_t>(piece_offset), 0, {}, {}});
                    piece_offset += width;
                }
                continue;
            }
            const auto type = parameter.type;
            const auto opcode = type == ir::Type(ir::TypeKind::f32) ? Opcode::load_argument_f32 :
                                type == ir::Type(ir::TypeKind::f64) ? Opcode::load_argument_f64 :
                                (type == ir::Type(ir::TypeKind::i64) || type == ir::Type(ir::TypeKind::ptr)) ? Opcode::load_argument_i64 : Opcode::load_argument;
            entry.instructions.push_back({opcode, registers.at(parameter.name), {}, 0, argument_index++, {}, {}});
        }
        // Entry argument loads form parallel ABI copies. Emit each contiguous
        // pure load run in reverse ABI order so high argument registers (r8/r9
        // on x86-64) are consumed before earlier loads can be allocated into
        // and overwrite them. Aggregate reconstruction instructions naturally
        // split the runs, preserving their dependencies. The encoder retains a
        // selective snapshot fallback for the rare cycles this ordering cannot
        // resolve.
        const auto is_argument_load = [](Opcode opcode) {
            return opcode == Opcode::load_argument || opcode == Opcode::load_argument_i64;
        };
        for (std::size_t begin = 0; begin < entry.instructions.size();) {
            if (!is_argument_load(entry.instructions[begin].opcode)) {
                ++begin;
                continue;
            }
            auto end = begin + 1U;
            while (end < entry.instructions.size() && is_argument_load(entry.instructions[end].opcode)) ++end;
            std::stable_sort(entry.instructions.begin() + static_cast<std::ptrdiff_t>(begin),
                             entry.instructions.begin() + static_cast<std::ptrdiff_t>(end),
                             [](const Instruction& left, const Instruction& right) {
                                 return left.argument_index > right.argument_index;
                             });
            begin = end;
        }

        for (std::size_t parameter_index = 0; parameter_index < function.parameters.size(); ++parameter_index) {
            const auto& parameter = function.parameters[parameter_index];
            if (!parameter.owned || aggregate_parameters[parameter_index].direct) continue;
            const auto& sizes = parameter.aggregate_kind == ir::AggregateRefKind::structure ? struct_sizes : array_sizes;
            const auto& alignments = parameter.aggregate_kind == ir::AggregateRefKind::structure ? struct_alignments : array_alignments;
            const auto size_it = sizes.find(parameter.aggregate_name);
            const auto alignment_it = alignments.find(parameter.aggregate_name);
            if (size_it == sizes.end() || alignment_it == alignments.end()) {
                error(result.diagnostics, "invalid owned aggregate parameter layout in @" + function.name);
                failed = true;
                break;
            }
            const auto base = (lowered.local_stack_size + alignment_it->second - 1U) & ~(alignment_it->second - 1U);
            const auto allocated = (size_it->second + 3U) & ~3U;
            lowered.local_stack_size = base + allocated;
            const auto destination = allocate_register(ir::Type(ir::TypeKind::ptr));
            entry.instructions.push_back({Opcode::load_stack_address, destination, {}, -static_cast<std::int64_t>(base + allocated), 0, {}, {}});
            const auto count = allocate_register(ir::Type(ir::TypeKind::i64));
            entry.instructions.push_back({Opcode::load_immediate_i64, count, {}, static_cast<std::int64_t>(size_it->second), 0, {}, {}});
            entry.instructions.push_back({Opcode::call_void, 0, {destination, registers.at(parameter.name), count}, 0, 0, "__forge_memmove", {}});
            registers[parameter.name] = destination;
        }
        if (failed) continue;

        for (std::size_t lowering_index = 0; lowering_index < lowering_order.size() && !failed; ++lowering_index) {
            const auto block_index = lowering_order[lowering_index];
            const auto& block = function.blocks[block_index];
            auto& machine_block = lowered.blocks[block_index];
            const auto append_control_successors = [&](const ir::Operation& operation, Instruction& target_instruction) -> bool {
                for (std::size_t successor_index = 0; successor_index < operation.successors.size(); ++successor_index) {
                    const auto target_lookup = block_indices.find(operation.successors[successor_index]);
                    if (target_lookup == block_indices.end()) {
                        error(result.diagnostics, "unknown lowering successor '" + operation.successors[successor_index] + "' in @" + function.name);
                        return false;
                    }
                    const auto target_index = target_lookup->second;
                    const auto& target_block = function.blocks[target_index];
                    Successor successor;
                    successor.block = operation.successors[successor_index];
                    for (std::size_t argument_index = 0; argument_index < operation.successor_arguments[successor_index].size(); ++argument_index) {
                        const auto& argument = operation.successor_arguments[successor_index][argument_index];
                        VirtualRegister edge_source{};
                        if (const auto found = registers.find(argument); found != registers.end()) edge_source = found->second;
                        else if (const auto address = addresses.find(argument); address != addresses.end()) {
                            edge_source = allocate_register(ir::Type(ir::TypeKind::ptr));
                            machine_block.instructions.push_back({Opcode::load_stack_address, edge_source, {},
                                -static_cast<std::int64_t>(address->second.object_base + address->second.object_size - address->second.offset), 0, {}, {}});
                        } else {
                            error(result.diagnostics, "undefined edge argument '" + argument + "' in @" + function.name);
                            return false;
                        }
                        if (argument_index < target_block.parameters.size() && target_block.parameters[argument_index].owned) {
                            const auto& parameter = target_block.parameters[argument_index];
                            const auto& sizes = parameter.aggregate_kind == ir::AggregateRefKind::structure ? struct_sizes : array_sizes;
                            const auto size_it = sizes.find(parameter.aggregate_name);
                            const auto& storage = owned_block_storage[target_index][argument_index];
                            if (size_it == sizes.end() || !storage) {
                                error(result.diagnostics, "invalid owned aggregate edge copy in @" + function.name);
                                return false;
                            }
                            const auto destination = allocate_register(ir::Type(ir::TypeKind::ptr));
                            machine_block.instructions.push_back({Opcode::load_stack_address, destination, {},
                                -static_cast<std::int64_t>(storage->object_base + storage->object_size), 0, {}, {}});
                            const auto count = allocate_register(ir::Type(ir::TypeKind::i64));
                            machine_block.instructions.push_back({Opcode::load_immediate_i64, count, {}, static_cast<std::int64_t>(size_it->second), 0, {}, {}});
                            machine_block.instructions.push_back({Opcode::call_void, 0, {destination, edge_source, count}, 0, 0, "__forge_memmove", {}});
                            edge_source = destination;
                        }
                        successor.arguments.push_back(edge_source);
                    }
                    target_instruction.successors.push_back(std::move(successor));
                }
                return true;
            };
            for (const auto& operation : block.operations) {
                Instruction instruction;
                bool emit = true;
                if (operation.opcode == "return") {
                    if (function.return_owned) {
                        const auto& size_table = function.return_aggregate_kind == ir::AggregateRefKind::structure ? struct_sizes : array_sizes;
                        const auto resolved = size_table.find(function.return_aggregate_name);
                        const auto source_register = operation.operands.size() == 1 ? registers.find(operation.operands[0]) : registers.end();
                        const auto source_address = operation.operands.size() == 1 ? addresses.find(operation.operands[0]) : addresses.end();
                        if (resolved == size_table.end() || (source_register == registers.end() && source_address == addresses.end()) ||
                            (!direct_native_return && !owned_result_register)) {
                            error(result.diagnostics, "invalid owned aggregate return in @" + function.name); failed = true;
                        } else {
                            VirtualRegister return_source{};
                            if (source_register != registers.end()) return_source = source_register->second;
                            else {
                                return_source = allocate_register(ir::Type(ir::TypeKind::ptr));
                                machine_block.instructions.push_back({Opcode::load_stack_address, return_source, {}, -static_cast<std::int64_t>(source_address->second.object_base + source_address->second.object_size - source_address->second.offset), 0, {}, {}});
                            }
                            if (direct_native_return) {
                                instruction.opcode = Opcode::return_aggregate;
                                instruction.inputs = {return_source};
                                instruction.immediate = static_cast<std::int64_t>(aggregate_return->size);
                                instruction.argument_index = encode_aggregate_return(*aggregate_return);
                            } else {
                                emit_aggregate_return_copy(machine_block, *owned_result_register, return_source, resolved->second);
                                instruction.opcode = Opcode::return_void;
                            }
                        }
                    } else if (function.return_type == ir::Type(ir::TypeKind::void_)) {
                        instruction.opcode = Opcode::return_void;
                        failed = !operation.operands.empty();
                    } else {
                        instruction.opcode = function.return_type == ir::Type(ir::TypeKind::f32) ? Opcode::return_f32 :
                                             function.return_type == ir::Type(ir::TypeKind::f64) ? Opcode::return_f64 :
                                             (function.return_type == ir::Type(ir::TypeKind::i64) || function.return_type == ir::Type(ir::TypeKind::ptr)) ? Opcode::return_i64 : Opcode::return_i32;
                        if (operation.operands.size() != 1) {
                            failed = true;
                        } else if (const auto found = registers.find(operation.operands[0]); found != registers.end()) {
                            instruction.inputs = {found->second};
                        } else if (
                            function.return_type == ir::Type(ir::TypeKind::ptr) &&
                            addresses.find(operation.operands[0]) != addresses.end()
                        ) {
                            const auto& address = addresses.at(operation.operands[0]);
                            const auto return_address = allocate_register(ir::Type(ir::TypeKind::ptr));
                            machine_block.instructions.push_back({Opcode::load_stack_address, return_address, {},
                                -static_cast<std::int64_t>(address.object_base + address.object_size - address.offset), 0, {}, {}});
                            instruction.inputs = {return_address};
                        } else {
                            error(result.diagnostics, "undefined lowering operand '" + operation.operands[0] + "' in @" + function.name);
                            failed = true;
                        }
                    }
                } else if (operation.opcode == "unreachable") {
                    if (function.return_type == ir::Type(ir::TypeKind::void_)) {
                        instruction.opcode = Opcode::return_void;
                    } else {
                        const bool wide = function.return_type == ir::Type(ir::TypeKind::i64) || function.return_type == ir::Type(ir::TypeKind::ptr);
                        const bool f32 = function.return_type == ir::Type(ir::TypeKind::f32);
                        const bool f64 = function.return_type == ir::Type(ir::TypeKind::f64);
                        const auto zero = allocate_register(function.return_type);
                        machine_block.instructions.push_back({f64 ? Opcode::load_immediate_f64 : f32 ? Opcode::load_immediate_f32 : wide ? Opcode::load_immediate_i64 : Opcode::load_immediate, zero, {}, 0, 0, {}, {}});
                        instruction.opcode = f64 ? Opcode::return_f64 : f32 ? Opcode::return_f32 : wide ? Opcode::return_i64 : Opcode::return_i32;
                        instruction.inputs = {zero};
                    }
                } else if (operation.opcode == "jump") {
                    instruction.opcode = Opcode::jump;
                    failed = !append_control_successors(operation, instruction) || instruction.successors.size() != 1;
                } else if (operation.opcode == "branch") {
                    instruction.opcode = Opcode::branch_i1;
                    failed = !append_inputs(operation, registers, instruction, result.diagnostics, function.name) ||
                             !append_control_successors(operation, instruction) ||
                             instruction.inputs.size() != 1 || instruction.successors.size() != 2;
                } else if (operation.opcode == "global.address") {
                    if ((operation.type != ir::Type(ir::TypeKind::ptr) && operation.type != ir::Type(ir::TypeKind::i64)) || operation.result.empty() ||
                        operation.operands.size() != 1 || !operation.operands[0].starts_with("@")) {
                        error(result.diagnostics, "invalid global address in @" + function.name);
                        failed = true;
                    } else {
                        instruction.opcode = Opcode::load_global_address;
                        instruction.symbol = operation.operands[0].substr(1);
                        instruction.result = allocate_register(operation.type);
                        registers.emplace(operation.result, instruction.result);
                        const auto global = std::find_if(source.globals().begin(), source.globals().end(), [&](const ir::Global& item) {
                            return item.name == instruction.symbol;
                        });
                        if (global != source.globals().end() && !global->function_signature_name.empty())
                            callback_storage_counts.emplace(operation.result, global->element_count);
                    }
                } else if (operation.opcode == "tls.address") {
                    if (operation.type != ir::Type(ir::TypeKind::ptr) || operation.result.empty() ||
                        operation.operands.size() != 1 || !operation.operands[0].starts_with("@")) {
                        error(result.diagnostics, "invalid TLS address in @" + function.name);
                        failed = true;
                    } else {
                        instruction.opcode = Opcode::load_tls_address;
                        instruction.symbol = operation.operands[0].substr(1);
                        const auto global = std::find_if(source.globals().begin(), source.globals().end(), [&](const ir::Global& item) {
                            return item.name == instruction.symbol;
                        });
                        if (global == source.globals().end() || !global->is_thread_local) {
                            error(result.diagnostics, "tls.address references non-TLS global @" + instruction.symbol);
                            failed = true;
                        } else {
                            instruction.result = allocate_register(operation.type);
                            registers.emplace(operation.result, instruction.result);
                        }
                    }
                } else if (operation.opcode == "func.address" || operation.opcode == "callback.address") {
                    if (operation.type != ir::Type(ir::TypeKind::ptr) || operation.result.empty() ||
                        operation.operands.empty() || operation.operands.size() > 2 || !operation.operands[0].starts_with("@")) {
                        error(result.diagnostics, "invalid function address in @" + function.name);
                        failed = true;
                    } else {
                        instruction.opcode = Opcode::load_function_address;
                        instruction.symbol = operation.operands[0].substr(1);
                        instruction.result = allocate_register(operation.type);
                        registers.emplace(operation.result, instruction.result);
                    }
                } else if (operation.opcode == "call.indirect") {
                    if (operation.operands.size() < 2 || !operation.operands[1].starts_with("@")) {
                        error(result.diagnostics, "invalid typed indirect call in @" + function.name);
                        failed = true;
                    } else {
                        const auto target_register = registers.find(operation.operands.front());
                        const auto signature_it = function_table.find(operation.operands[1].substr(1));
                        if (target_register == registers.end() || signature_it == function_table.end()) {
                            error(result.diagnostics, "undefined indirect call target or signature in @" + function.name);
                            failed = true;
                        } else {
                            const auto& signature = *signature_it->second;
                            instruction.inputs.push_back(target_register->second);

                            const auto append_indirect_arguments = [&](std::size_t first_argument) -> bool {
                                const auto argument_count = operation.operands.size() - first_argument;
                                for (std::size_t index = 0; index < argument_count; ++index) {
                                    const auto& operand = operation.operands[first_argument + index];
                                    auto source_it = registers.find(operand);
                                    VirtualRegister materialized_source{};
                                    if (source_it == registers.end()) {
                                        const auto address = addresses.find(operand);
                                        if (address == addresses.end()) {
                                            error(result.diagnostics, "undefined lowering operand '" + operand + "' in @" + function.name);
                                            return false;
                                        }
                                        materialized_source = allocate_register(ir::Type(ir::TypeKind::ptr));
                                        machine_block.instructions.push_back({Opcode::load_stack_address, materialized_source, {},
                                            -static_cast<std::int64_t>(address->second.object_base + address->second.object_size - address->second.offset), 0, {}, {}});
                                    }
                                    const auto source_register = source_it == registers.end() ? materialized_source : source_it->second;
                                    if (index >= signature.parameters.size() || !signature.parameters[index].owned) {
                                        instruction.inputs.push_back(source_register);
                                        continue;
                                    }
                                    const auto& parameter = signature.parameters[index];
                                    const auto& sizes = parameter.aggregate_kind == ir::AggregateRefKind::structure ? struct_sizes : array_sizes;
                                    const auto& alignments = parameter.aggregate_kind == ir::AggregateRefKind::structure ? struct_alignments : array_alignments;
                                    const auto size_it = sizes.find(parameter.aggregate_name);
                                    const auto alignment_it = alignments.find(parameter.aggregate_name);
                                    if (size_it == sizes.end() || alignment_it == alignments.end()) {
                                        error(result.diagnostics, "invalid owned aggregate indirect argument layout in @" + function.name);
                                        return false;
                                    }
                                    const auto base = (lowered.local_stack_size + alignment_it->second - 1U) & ~(alignment_it->second - 1U);
                                    const auto allocated = (size_it->second + 3U) & ~3U;
                                    lowered.local_stack_size = base + allocated;
                                    const auto destination = allocate_register(ir::Type(ir::TypeKind::ptr));
                                    machine_block.instructions.push_back({Opcode::load_stack_address, destination, {}, -static_cast<std::int64_t>(base + allocated), 0, {}, {}});
                                    const auto count = allocate_register(ir::Type(ir::TypeKind::i64));
                                    machine_block.instructions.push_back({Opcode::load_immediate_i64, count, {}, static_cast<std::int64_t>(size_it->second), 0, {}, {}});
                                    machine_block.instructions.push_back({Opcode::call_void, 0, {destination, source_register, count}, 0, 0, "__forge_memmove", {}});
                                    instruction.inputs.push_back(destination);
                                }
                                return true;
                            };

                            if (signature.return_owned) {
                                const auto& sizes = signature.return_aggregate_kind == ir::AggregateRefKind::structure ? struct_sizes : array_sizes;
                                const auto& alignments = signature.return_aggregate_kind == ir::AggregateRefKind::structure ? struct_alignments : array_alignments;
                                const auto size_it = sizes.find(signature.return_aggregate_name);
                                const auto alignment_it = alignments.find(signature.return_aggregate_name);
                                if (operation.type != ir::Type(ir::TypeKind::ptr) || operation.result.empty() ||
                                    size_it == sizes.end() || alignment_it == alignments.end()) {
                                    error(result.diagnostics, "invalid owned aggregate indirect return in @" + function.name);
                                    failed = true;
                                } else {
                                    const auto base = (lowered.local_stack_size + alignment_it->second - 1U) & ~(alignment_it->second - 1U);
                                    const auto allocated = (size_it->second + 3U) & ~3U;
                                    addresses.emplace(operation.result, StackAddress{base, allocated, 0});
                                    lowered.local_stack_size = base + allocated;
                                    const auto destination = allocate_register(ir::Type(ir::TypeKind::ptr));
                                    machine_block.instructions.push_back({Opcode::load_stack_address, destination, {}, -static_cast<std::int64_t>(base + allocated), 0, {}, {}});
                                    instruction.opcode = Opcode::call_indirect_void;
                                    instruction.inputs.push_back(destination);
                                    if (function_native_abi(signature, target_architecture) == target::NativeAbi::aapcs64)
                                        instruction.indirect_result = true;
                                    failed = !append_indirect_arguments(2);
                                }
                            } else {
                                instruction.opcode = operation.type == ir::Type(ir::TypeKind::void_) ? Opcode::call_indirect_void
                                    : operation.type == ir::Type(ir::TypeKind::f32) ? Opcode::call_indirect_f32
                                    : operation.type == ir::Type(ir::TypeKind::f64) ? Opcode::call_indirect_f64
                                    : (operation.type == ir::Type(ir::TypeKind::i64) || operation.type == ir::Type(ir::TypeKind::ptr))
                                        ? Opcode::call_indirect_i64 : Opcode::call_indirect_i32;
                                failed = !append_indirect_arguments(2);
                                if (!failed && operation.type != ir::Type(ir::TypeKind::void_)) {
                                    if (operation.result.empty() || (operation.type != ir::Type(ir::TypeKind::i32) &&
                                        operation.type != ir::Type(ir::TypeKind::i64) && operation.type != ir::Type(ir::TypeKind::ptr) &&
                                        operation.type != ir::Type(ir::TypeKind::f32) && operation.type != ir::Type(ir::TypeKind::f64))) {
                                        error(result.diagnostics, "invalid indirect call result in @" + function.name);
                                        failed = true;
                                    } else {
                                        instruction.result = allocate_register(operation.type);
                                        registers.emplace(operation.result, instruction.result);
                                    }
                                } else if (!failed && !operation.result.empty()) {
                                    error(result.diagnostics, "void indirect call cannot define a result in @" + function.name);
                                    failed = true;
                                }
                            }
                        }
                    }
                } else if (operation.opcode == "call") {
                    const auto append_call_arguments = [&](Instruction& call_instruction, const ir::Function* target) -> bool {
                        const auto argument_count = operation.operands.size() - 1U;
                        for (std::size_t index = 0; index < argument_count; ++index) {
                            const auto& operand = operation.operands[index + 1U];
                            auto source_it = registers.find(operand);
                            VirtualRegister materialized_source{};
                            if (source_it == registers.end()) {
                                const auto address = addresses.find(operand);
                                if (address == addresses.end()) {
                                    error(result.diagnostics, "undefined lowering operand '" + operand + "' in @" + function.name);
                                    return false;
                                }
                                materialized_source = allocate_register(ir::Type(ir::TypeKind::ptr));
                                machine_block.instructions.push_back({Opcode::load_stack_address, materialized_source, {},
                                    -static_cast<std::int64_t>(address->second.object_base + address->second.object_size - address->second.offset), 0, {}, {}});
                            }
                            const auto source_register = source_it == registers.end() ? materialized_source : source_it->second;
                            if (target && index < target->parameters.size() && target->parameters[index].is_aggregate()) {
                                const auto& aggregate_parameter = target->parameters[index];
                                const auto classified = target::classify_aggregate(source, aggregate_parameter.aggregate_kind,
                                    aggregate_parameter.aggregate_name, function_native_abi(*target, target_architecture), host_layout);
                                if (!classified) {
                                    error(result.diagnostics, "cannot classify aggregate call argument in @" + function.name);
                                    return false;
                                }
                                if (uses_native_aggregate_abi(*target) && classified->register_passed()) {
                                    std::uint32_t piece_offset = 0;
                                    for (std::size_t piece = 0; piece < classified->register_count; ++piece) {
                                        const bool floating = classified->classes[piece] == target::AbiValueClass::sse;
                                        const auto width = classified->piece_widths[piece] == 0U
                                            ? std::uint8_t{8} : classified->piece_widths[piece];
                                        const auto value_type = floating && width == 4U ? ir::Type(ir::TypeKind::f32)
                                            : floating ? ir::Type(ir::TypeKind::f64) : ir::Type(ir::TypeKind::i64);
                                        const auto value = allocate_register(value_type);
                                        machine_block.instructions.push_back({floating ? (width == 4U ? Opcode::load_ptr_f32 : Opcode::load_ptr_f64)
                                                                                  : Opcode::load_ptr_i64,
                                            value, {source_register}, static_cast<std::int64_t>(piece_offset), 0, {}, {}});
                                        call_instruction.inputs.push_back(value);
                                        piece_offset += width;
                                    }
                                    continue;
                                }
                            }
                            if (!target || index >= target->parameters.size() || !target->parameters[index].owned) {
                                call_instruction.inputs.push_back(source_register);
                                continue;
                            }
                            const auto& parameter = target->parameters[index];
                            const auto& sizes = parameter.aggregate_kind == ir::AggregateRefKind::structure ? struct_sizes : array_sizes;
                            const auto& alignments = parameter.aggregate_kind == ir::AggregateRefKind::structure ? struct_alignments : array_alignments;
                            const auto size_it = sizes.find(parameter.aggregate_name);
                            const auto alignment_it = alignments.find(parameter.aggregate_name);
                            if (size_it == sizes.end() || alignment_it == alignments.end()) {
                                error(result.diagnostics, "invalid owned aggregate call argument layout in @" + function.name);
                                return false;
                            }
                            const auto base = (lowered.local_stack_size + alignment_it->second - 1U) & ~(alignment_it->second - 1U);
                            const auto allocated = (size_it->second + 3U) & ~3U;
                            lowered.local_stack_size = base + allocated;
                            const auto destination = allocate_register(ir::Type(ir::TypeKind::ptr));
                            machine_block.instructions.push_back({Opcode::load_stack_address, destination, {}, -static_cast<std::int64_t>(base + allocated), 0, {}, {}});
                            const auto count = allocate_register(ir::Type(ir::TypeKind::i64));
                            machine_block.instructions.push_back({Opcode::load_immediate_i64, count, {}, static_cast<std::int64_t>(size_it->second), 0, {}, {}});
                            machine_block.instructions.push_back({Opcode::call_void, 0, {destination, source_register, count}, 0, 0, "__forge_memmove", {}});
                            call_instruction.inputs.push_back(destination);
                        }
                        return true;
                    };
                    if (operation.operands.empty() || !operation.operands.front().starts_with("@")) {
                        error(result.diagnostics, "invalid call in @" + function.name);
                        failed = true;
                    } else if (const auto target_it = function_table.find(operation.operands.front().substr(1)); target_it != function_table.end() && target_it->second->return_owned) {
                        const auto& target = *target_it->second;
                        const auto& sizes = target.return_aggregate_kind == ir::AggregateRefKind::structure ? struct_sizes : array_sizes;
                        const auto& alignments = target.return_aggregate_kind == ir::AggregateRefKind::structure ? struct_alignments : array_alignments;
                        const auto size_it = sizes.find(target.return_aggregate_name);
                        const auto alignment_it = alignments.find(target.return_aggregate_name);
                        if (operation.type != ir::Type(ir::TypeKind::ptr) || operation.result.empty() || size_it == sizes.end() || alignment_it == alignments.end()) {
                            error(result.diagnostics, "invalid owned aggregate call in @" + function.name); failed = true;
                        } else {
                            const auto base = (lowered.local_stack_size + alignment_it->second - 1U) & ~(alignment_it->second - 1U);
                            const auto allocated = (size_it->second + 3U) & ~3U;
                            addresses.emplace(operation.result, StackAddress{base, allocated, 0});
                            lowered.local_stack_size = base + allocated;
                            const auto destination = allocate_register(ir::Type(ir::TypeKind::ptr));
                            machine_block.instructions.push_back({Opcode::load_stack_address, destination, {}, -static_cast<std::int64_t>(base + allocated), 0, {}, {}});
                            const auto return_classification = target::classify_aggregate(source, target.return_aggregate_kind,
                                target.return_aggregate_name, function_native_abi(target, target_architecture), host_layout);
                            const bool direct_return = return_classification && uses_native_aggregate_abi(target) &&
                                return_classification->register_passed() && !return_classification->returned_indirectly;
                            instruction.opcode = direct_return ? Opcode::call_aggregate : Opcode::call_void;
                            instruction.symbol = target.name;
                            instruction.inputs.push_back(destination);
                            if (direct_return) {
                                instruction.immediate = static_cast<std::int64_t>(return_classification->size);
                                instruction.argument_index = encode_aggregate_return(*return_classification);
                            } else if (function_native_abi(target, target_architecture) == target::NativeAbi::aapcs64) {
                                instruction.indirect_result = true;
                            }
                            failed = !append_call_arguments(instruction, &target);
                        }
                    } else if (operation.type == ir::Type(ir::TypeKind::void_)) {
                        if (!operation.result.empty()) {
                            error(result.diagnostics, "void call cannot define a result in @" + function.name);
                            failed = true;
                        } else {
                            instruction.opcode = Opcode::call_void;
                            instruction.symbol = operation.operands.front().substr(1);
                            const auto call_target_it = function_table.find(instruction.symbol);
                            failed = !append_call_arguments(instruction, call_target_it == function_table.end() ? nullptr : call_target_it->second);
                        }
                    } else if (operation.result.empty() || (operation.type != ir::Type(ir::TypeKind::i1) && operation.type != ir::Type(ir::TypeKind::i8) && operation.type != ir::Type(ir::TypeKind::i16) && operation.type != ir::Type(ir::TypeKind::i32) && operation.type != ir::Type(ir::TypeKind::i64) && operation.type != ir::Type(ir::TypeKind::ptr) && operation.type != ir::Type(ir::TypeKind::f32) && operation.type != ir::Type(ir::TypeKind::f64))) {
                        error(result.diagnostics, "invalid value call in @" + function.name);
                        failed = true;
                    } else {
                        instruction.opcode = operation.type == ir::Type(ir::TypeKind::f32) ? Opcode::call_f32
                            : operation.type == ir::Type(ir::TypeKind::f64) ? Opcode::call_f64
                            : (operation.type == ir::Type(ir::TypeKind::i64) || operation.type == ir::Type(ir::TypeKind::ptr)) ? Opcode::call_i64 : Opcode::call_i32;
                        instruction.symbol = operation.operands.front().substr(1);
                        const auto call_target_it = function_table.find(instruction.symbol);
                        failed = !append_call_arguments(instruction, call_target_it == function_table.end() ? nullptr : call_target_it->second);
                        instruction.result = allocate_register(operation.type);
                        registers.emplace(operation.result, instruction.result);
                    }
                } else if (operation.opcode == "stack.alloc.struct" || operation.opcode == "stack.alloc.array") {
                    const bool structure = operation.opcode == "stack.alloc.struct";
                    const auto& sizes = structure ? struct_sizes : array_sizes;
                    const auto& alignments = structure ? struct_alignments : array_alignments;
                    const auto name = operation.operands.size() == 1 && operation.operands[0].starts_with("@") ? operation.operands[0].substr(1) : std::string{};
                    const auto size_it = sizes.find(name); const auto alignment_it = alignments.find(name);
                    if (operation.type != ir::Type(ir::TypeKind::ptr) || operation.result.empty() || size_it == sizes.end() || alignment_it == alignments.end()) {
                        error(result.diagnostics, "invalid typed stack allocation in @" + function.name); failed = true;
                    } else {
                        const auto alignment = alignment_it->second;
                        const auto base = (lowered.local_stack_size + alignment - 1U) & ~(alignment - 1U);
                        const auto allocated = (size_it->second + 3U) & ~3U;
                        addresses.emplace(operation.result, StackAddress{base, allocated, 0});
                        lowered.local_stack_size = base + allocated;
                    }
                    emit = false;
                } else if (operation.opcode == "stack.alloc") {
                    std::uint32_t size = 0;
                    if (operation.type != ir::Type(ir::TypeKind::ptr) || operation.result.empty() ||
                        operation.operands.size() != 1 || !parse_u32(operation.operands[0], size) || size == 0 || size > 1U << 20U) {
                        error(result.diagnostics, "invalid fixed stack allocation in @" + function.name);
                        failed = true;
                    } else {
                        const auto alignment = operation.alignment == 0 ? 4U : operation.alignment;
                        const auto base = (lowered.local_stack_size + alignment - 1U) & ~(alignment - 1U);
                        const auto allocated = (size + 3U) & ~3U;
                        addresses.emplace(operation.result, StackAddress{base, allocated, 0});
                        lowered.local_stack_size = base + allocated;
                    }
                    emit = false;
                } else if (operation.opcode == "ptr.offset" || operation.opcode == "field.address" || operation.opcode == "struct.field.address" || operation.opcode == "struct.field.name.address" || operation.opcode == "array.element.address" || operation.opcode == "callback.element.address") {
                    std::uint32_t offset = 0;
                    const auto static_base = operation.operands.empty() ? addresses.end() : addresses.find(operation.operands[0]);
                    bool offset_ok = false;
                    if (operation.opcode == "callback.element.address" && operation.operands.size() == 3 && operation.operands[1].starts_with("@")) {
                        std::uint32_t index = 0;
                        if (parse_u32(operation.operands[2], index) && index <= std::numeric_limits<std::uint32_t>::max() / 8U) {
                            offset = index * 8U; offset_ok = true;
                        } else if (operation.operands[2].starts_with('%')) {
                            const auto base = registers.find(operation.operands[0]);
                            const auto dynamic_index = registers.find(operation.operands[2]);
                            const auto count = callback_storage_counts.find(operation.operands[0]);
                            if (base != registers.end() && dynamic_index != registers.end() && count != callback_storage_counts.end()) {
                                const auto count_register = allocate_register(ir::Type(ir::TypeKind::i64));
                                machine_block.instructions.push_back({Opcode::load_immediate_i64, count_register, {}, static_cast<std::int64_t>(count->second), 0, {}, {}});
                                instruction.opcode = Opcode::call_i64;
                                instruction.symbol = "__forge_callback_element_address";
                                instruction.inputs = {base->second, dynamic_index->second, count_register};
                                instruction.result = allocate_register(ir::Type(ir::TypeKind::ptr));
                                registers.emplace(operation.result, instruction.result);
                                callback_storage_counts.emplace(operation.result, 1U);
                                offset_ok = true;
                                emit = true;
                            }
                        }
                    } else if (operation.opcode == "array.element.address" && operation.operands.size() == 3 && operation.operands[1].starts_with("@")) {
                        std::uint32_t index = 0;
                        const auto found = array_strides.find(operation.operands[1].substr(1));
                        if (found != array_strides.end() && parse_u32(operation.operands[2], index) &&
                            index <= std::numeric_limits<std::uint32_t>::max() / found->second) {
                            offset = index * found->second; offset_ok = true;
                        } else if (found != array_strides.end() && operation.operands[2].starts_with('%')) {
                            auto base = registers.find(operation.operands[0]);
                            const auto dynamic_index = registers.find(operation.operands[2]);
                            std::uint32_t base_register = 0;
                            bool have_base = false;
                            if (base != registers.end()) {
                                base_register = base->second;
                                have_base = true;
                            } else if (static_base != addresses.end()) {
                                base_register = allocate_register(ir::Type(ir::TypeKind::ptr));
                                machine_block.instructions.push_back({Opcode::load_stack_address, base_register, {},
                                    -static_cast<std::int64_t>(static_base->second.object_base + static_base->second.object_size - static_base->second.offset), 0, {}, {}});
                                have_base = true;
                            }
                            if (have_base && dynamic_index != registers.end()) {
                                const auto stride_register = allocate_register(ir::Type(ir::TypeKind::i64));
                                machine_block.instructions.push_back({Opcode::load_immediate_i64, stride_register, {}, static_cast<std::int64_t>(found->second), 0, {}, {}});
                                const auto scaled_index = allocate_register(ir::Type(ir::TypeKind::i64));
                                machine_block.instructions.push_back({Opcode::mul_i64, scaled_index, {dynamic_index->second, stride_register}, 0, 0, {}, {}});
                                instruction.opcode = Opcode::add_i64;
                                instruction.inputs = {base_register, scaled_index};
                                instruction.result = allocate_register(ir::Type(ir::TypeKind::ptr));
                                registers.emplace(operation.result, instruction.result);
                                offset_ok = true;
                                emit = true;
                            }
                        }
                    } else if ((operation.opcode == "struct.field.address" || operation.opcode == "struct.field.name.address") && operation.operands.size() == 3 && operation.operands[1].starts_with("@")) {
                        const auto separator = operation.opcode == "struct.field.address" ? "#" : ".";
                        const auto found = struct_field_offsets.find(operation.operands[1].substr(1) + separator + operation.operands[2]);
                        if (found != struct_field_offsets.end()) { offset = found->second; offset_ok = true; }
                    } else if (operation.operands.size() == 2) {
                        offset_ok = parse_u32(operation.operands[1], offset);
                        if (!offset_ok && operation.operands[1].starts_with('%')) {
                            auto base = registers.find(operation.operands[0]);
                            const auto dynamic_offset = registers.find(operation.operands[1]);
                            std::uint32_t base_register = 0;
                            bool have_base = false;
                            if (base != registers.end()) {
                                base_register = base->second;
                                have_base = true;
                            } else if (static_base != addresses.end()) {
                                base_register = allocate_register(ir::Type(ir::TypeKind::ptr));
                                machine_block.instructions.push_back({Opcode::load_stack_address, base_register, {},
                                    -static_cast<std::int64_t>(static_base->second.object_base + static_base->second.object_size - static_base->second.offset), 0, {}, {}});
                                have_base = true;
                            }
                            if (have_base && dynamic_offset != registers.end()) {
                                instruction.opcode = Opcode::add_i64;
                                instruction.inputs = {base_register, dynamic_offset->second};
                                instruction.result = allocate_register(ir::Type(ir::TypeKind::ptr));
                                registers.emplace(operation.result, instruction.result);
                                offset_ok = true;
                                emit = true;
                            }
                        }
                    }
                    const bool dynamic_address = registers.contains(operation.result) &&
                        ((operation.opcode == "callback.element.address" && operation.operands.size() == 3 && operation.operands[2].starts_with('%')) ||
                         (operation.opcode == "array.element.address" && operation.operands.size() == 3 && operation.operands[2].starts_with('%')) ||
                         (operation.operands.size() == 2 && operation.operands[1].starts_with('%')));
                    if (dynamic_address) {
                        // Dynamic address lowering above fully materialized the result.
                    } else if (operation.type != ir::Type(ir::TypeKind::ptr) || operation.result.empty() || !offset_ok) {
                        error(result.diagnostics, "invalid aggregate pointer offset in @" + function.name);
                        failed = true;
                    } else if (static_base != addresses.end()) {
                        if (static_base->second.offset + offset > static_base->second.object_size) {
                            error(result.diagnostics, "invalid stack pointer offset in @" + function.name);
                            failed = true;
                        } else {
                            auto address = static_base->second;
                            address.offset += offset;
                            addresses.emplace(operation.result, address);
                            emit = false;
                        }
                    } else {
                        const auto base = registers.find(operation.operands[0]);
                        if (base == registers.end()) {
                            error(result.diagnostics, "undefined pointer base in @" + function.name);
                            failed = true;
                        } else {
                            instruction.opcode = Opcode::ptr_offset;
                            instruction.inputs.push_back(base->second);
                                                        instruction.immediate = offset;
                            instruction.result = allocate_register(operation.type);
                            registers.emplace(operation.result, instruction.result);
                            if (operation.opcode == "callback.element.address") callback_storage_counts.emplace(operation.result, 1U);
                        }
                    }
                } else if (operation.opcode == "load") {
                    const auto address = operation.operands.empty() ? addresses.end() : addresses.find(operation.operands[0]);
                    const auto access_size = (operation.type == ir::Type(ir::TypeKind::i1) || operation.type == ir::Type(ir::TypeKind::i8)) ? 1U :
                                             operation.type == ir::Type(ir::TypeKind::i16) ? 2U :
                                             operation.type == ir::Type(ir::TypeKind::i32) ? 4U :
                                             (operation.type == ir::Type(ir::TypeKind::i64) || operation.type == ir::Type(ir::TypeKind::ptr) || operation.type == ir::Type(ir::TypeKind::f64)) ? 8U :
                                             operation.type == ir::Type(ir::TypeKind::f32) ? 4U : 0U;
                    if (access_size == 0 || operation.result.empty() || operation.operands.size() != 1) {
                        error(result.diagnostics, "invalid integer load in @" + function.name);
                        failed = true;
                    } else {
                        instruction.result = allocate_register(operation.type);
                        const auto stack_opcode = operation.type == ir::Type(ir::TypeKind::f32) ? Opcode::load_stack_f32 : operation.type == ir::Type(ir::TypeKind::f64) ? Opcode::load_stack_f64 : access_size == 1 ? Opcode::load_stack_i8 : access_size == 2 ? Opcode::load_stack_i16 : access_size == 4 ? Opcode::load_stack_i32 : Opcode::load_stack_i64;
                        const auto pointer_opcode = operation.type == ir::Type(ir::TypeKind::f32) ? Opcode::load_ptr_f32 : operation.type == ir::Type(ir::TypeKind::f64) ? Opcode::load_ptr_f64 : access_size == 1 ? Opcode::load_ptr_i8 : access_size == 2 ? Opcode::load_ptr_i16 : access_size == 4 ? Opcode::load_ptr_i32 : Opcode::load_ptr_i64;
                        if (address != addresses.end()) {
                            const auto absolute = address->second.object_base + address->second.offset;
                            if (operation.alignment != 0 && absolute % operation.alignment != 0) { error(result.diagnostics, "stack load does not satisfy alignment in @" + function.name); failed = true; }
                            else if (address->second.offset + access_size > address->second.object_size) { error(result.diagnostics, "invalid stack load in @" + function.name); failed = true; }
                            else { instruction.opcode = stack_opcode; instruction.immediate = -static_cast<std::int64_t>(address->second.object_base + address->second.object_size - address->second.offset); }
                        } else {
                            const auto pointer = registers.find(operation.operands[0]);
                            if (pointer == registers.end()) { error(result.diagnostics, "undefined load pointer in @" + function.name); failed = true; }
                            else { instruction.opcode = pointer_opcode; instruction.inputs.push_back(pointer->second); }
                        }
                        if (!failed) registers.emplace(operation.result, instruction.result);
                    }
                } else if (operation.opcode == "aggregate.move.struct" || operation.opcode == "aggregate.move.array" ||
                           operation.opcode == "aggregate.borrow.struct" || operation.opcode == "aggregate.borrow.array" ||
                           operation.opcode == "aggregate.borrow.mut.struct" || operation.opcode == "aggregate.borrow.mut.array") {
                    if (operation.type != ir::Type(ir::TypeKind::ptr) || operation.result.empty() || operation.operands.size() != 2) {
                        error(result.diagnostics, "invalid typed aggregate move in @" + function.name); failed = true;
                    } else if (const auto source_register_it = registers.find(operation.operands[0]); source_register_it != registers.end()) {
                        registers.emplace(operation.result, source_register_it->second);
                        emit = false;
                    } else if (const auto source_address_it = addresses.find(operation.operands[0]); source_address_it != addresses.end()) {
                        addresses.emplace(operation.result, source_address_it->second);
                        emit = false;
                    } else {
                        error(result.diagnostics, "undefined typed aggregate move source in @" + function.name); failed = true;
                    }
                } else if (operation.opcode == "aggregate.borrow.end.struct" || operation.opcode == "aggregate.borrow.end.array" ||
                           operation.opcode == "aggregate.end.struct" || operation.opcode == "aggregate.end.array") {
                    emit = false;
                } else if (operation.opcode == "aggregate.copy.struct" || operation.opcode == "aggregate.copy.array" ||
                           operation.opcode == "aggregate.zero.struct" || operation.opcode == "aggregate.zero.array") {
                    const bool copy = operation.opcode.starts_with("aggregate.copy");
                    const bool structure = operation.opcode.ends_with("struct");
                    const auto& sizes = structure ? struct_sizes : array_sizes;
                    const auto name_index = copy ? 2U : 1U;
                    const auto name = operation.operands.size() > name_index && operation.operands[name_index].starts_with("@") ? operation.operands[name_index].substr(1) : std::string{};
                    const auto size_it = sizes.find(name);
                    const auto materialize_pointer = [&](const std::string& value) -> std::optional<VirtualRegister> {
                        if (const auto found = registers.find(value); found != registers.end()) return found->second;
                        if (const auto address = addresses.find(value); address != addresses.end()) {
                            const auto pointer = allocate_register(ir::Type(ir::TypeKind::ptr));
                            machine_block.instructions.push_back({Opcode::load_stack_address, pointer, {},
                                -static_cast<std::int64_t>(address->second.object_base + address->second.object_size - address->second.offset), 0, {}, {}});
                            return pointer;
                        }
                        return std::nullopt;
                    };
                    const auto destination = operation.operands.empty() ? std::optional<VirtualRegister>{} : materialize_pointer(operation.operands[0]);
                    const auto source_pointer = copy && operation.operands.size() > 1 ? materialize_pointer(operation.operands[1]) : std::optional<VirtualRegister>{};
                    if (operation.type != ir::Type(ir::TypeKind::void_) || !operation.result.empty() || size_it == sizes.end() || !destination || (copy && !source_pointer)) {
                        error(result.diagnostics, "invalid typed aggregate memory operation in @" + function.name); failed = true;
                    } else {
                        const auto count = allocate_register(ir::Type(ir::TypeKind::i64));
                        machine_block.instructions.push_back({Opcode::load_immediate_i64, count, {}, static_cast<std::int64_t>(size_it->second), 0, {}, {}});
                        instruction.opcode = Opcode::call_void;
                        instruction.symbol = copy ? "__forge_memmove" : "__forge_memset";
                        instruction.inputs.push_back(*destination);
                        if (copy) instruction.inputs.push_back(*source_pointer);
                        else {
                            const auto zero = allocate_register(ir::Type(ir::TypeKind::i32));
                            machine_block.instructions.push_back({Opcode::load_immediate, zero, {}, 0, 0, {}, {}});
                            instruction.inputs.push_back(zero);
                        }
                        instruction.inputs.push_back(count);
                    }
                } else if (operation.opcode == "memory.copy" || operation.opcode == "memory.set") {
                    if (operation.type != ir::Type(ir::TypeKind::void_) || !operation.result.empty() || operation.operands.size() != 3) {
                        error(result.diagnostics, "invalid bulk memory operation in @" + function.name);
                        failed = true;
                    } else {
                        instruction.opcode = Opcode::call_void;
                        instruction.symbol = operation.opcode == "memory.copy" ? "__forge_memmove" : "__forge_memset";
                        for (const auto& operand : operation.operands) {
                            const auto found = registers.find(operand);
                            if (found != registers.end()) {
                                instruction.inputs.push_back(found->second);
                                continue;
                            }
                            // stack.alloc and statically-resolved aggregate address
                            // operations intentionally live in the address table
                            // until a consumer needs a physical pointer register.
                            // Bulk memory operations are such consumers, just like
                            // calls and pointer stores, so materialize the address
                            // here instead of rejecting otherwise valid ptr IR.
                            const auto address = addresses.find(operand);
                            if (address != addresses.end()) {
                                const auto pointer = allocate_register(ir::Type(ir::TypeKind::ptr));
                                machine_block.instructions.push_back({Opcode::load_stack_address, pointer, {},
                                    -static_cast<std::int64_t>(address->second.object_base + address->second.object_size - address->second.offset), 0, {}, {}});
                                instruction.inputs.push_back(pointer);
                                continue;
                            }
                            error(result.diagnostics, "undefined bulk memory operand in @" + function.name);
                            failed = true;
                            break;
                        }
                    }
                } else if (operation.opcode == "store") {
                    auto value = operation.operands.size() == 2 ? registers.find(operation.operands[0]) : registers.end();
                    VirtualRegister materialized_value{};
                    bool has_materialized_value = false;
                    if (value == registers.end() && operation.type == ir::Type(ir::TypeKind::ptr) && operation.operands.size() == 2) {
                        const auto source_address = addresses.find(operation.operands[0]);
                        if (source_address != addresses.end()) {
                            materialized_value = allocate_register(ir::Type(ir::TypeKind::ptr));
                            has_materialized_value = true;
                            machine_block.instructions.push_back({Opcode::load_stack_address, materialized_value, {},
                                -static_cast<std::int64_t>(source_address->second.object_base + source_address->second.object_size - source_address->second.offset), 0, {}, {}});
                        }
                    }
                    const auto address = operation.operands.size() == 2 ? addresses.find(operation.operands[1]) : addresses.end();
                    const auto access_size = (operation.type == ir::Type(ir::TypeKind::i1) || operation.type == ir::Type(ir::TypeKind::i8)) ? 1U :
                                             operation.type == ir::Type(ir::TypeKind::i16) ? 2U :
                                             operation.type == ir::Type(ir::TypeKind::i32) ? 4U :
                                             (operation.type == ir::Type(ir::TypeKind::i64) || operation.type == ir::Type(ir::TypeKind::ptr) || operation.type == ir::Type(ir::TypeKind::f64)) ? 8U :
                                             operation.type == ir::Type(ir::TypeKind::f32) ? 4U : 0U;
                    const bool has_value = value != registers.end() || has_materialized_value;
                    if (access_size == 0 || !has_value || operation.operands.size() != 2) {
                        error(result.diagnostics, "invalid integer store in @" + function.name);
                        failed = true;
                    } else {
                        const auto stack_opcode = operation.type == ir::Type(ir::TypeKind::f32) ? Opcode::store_stack_f32 : operation.type == ir::Type(ir::TypeKind::f64) ? Opcode::store_stack_f64 : access_size == 1 ? Opcode::store_stack_i8 : access_size == 2 ? Opcode::store_stack_i16 : access_size == 4 ? Opcode::store_stack_i32 : Opcode::store_stack_i64;
                        const auto pointer_opcode = operation.type == ir::Type(ir::TypeKind::f32) ? Opcode::store_ptr_f32 : operation.type == ir::Type(ir::TypeKind::f64) ? Opcode::store_ptr_f64 : access_size == 1 ? Opcode::store_ptr_i8 : access_size == 2 ? Opcode::store_ptr_i16 : access_size == 4 ? Opcode::store_ptr_i32 : Opcode::store_ptr_i64;
                        if (address != addresses.end()) {
                            const auto absolute = address->second.object_base + address->second.offset;
                            if (operation.alignment != 0 && absolute % operation.alignment != 0) { error(result.diagnostics, "stack store does not satisfy alignment in @" + function.name); failed = true; }
                            else if (address->second.offset + access_size > address->second.object_size) { error(result.diagnostics, "invalid stack store in @" + function.name); failed = true; }
                            else { instruction.opcode = stack_opcode; instruction.inputs.push_back(value != registers.end() ? value->second : materialized_value); instruction.immediate = -static_cast<std::int64_t>(address->second.object_base + address->second.object_size - address->second.offset); }
                        } else {
                            const auto pointer = registers.find(operation.operands[1]);
                            if (pointer == registers.end()) { error(result.diagnostics, "undefined store pointer in @" + function.name); failed = true; }
                            else { instruction.opcode = pointer_opcode; instruction.inputs = {value != registers.end() ? value->second : materialized_value, pointer->second}; }
                        }
                    }
                } else {
                    if (operation.result.empty()) {
                        error(result.diagnostics, "value-producing operation has no result in @" + function.name);
                        failed = true;
                        break;
                    }
                    const auto result_type = operation.opcode.starts_with("cmp.")
                        ? ir::Type(ir::TypeKind::i1)
                        : operation.type;
                    instruction.result = allocate_register(result_type);
                    const bool wide_value = operation.type == ir::Type(ir::TypeKind::i64);
                    const bool float_value = operation.type == ir::Type(ir::TypeKind::f32) || operation.type == ir::Type(ir::TypeKind::f64);
                    const bool wide_float = operation.type == ir::Type(ir::TypeKind::f64);
                    if (operation.opcode == "const") {
                        instruction.opcode = float_value ? (wide_float ? Opcode::load_immediate_f64 : Opcode::load_immediate_f32) : (wide_value ? Opcode::load_immediate_i64 : Opcode::load_immediate);
                        bool parsed_constant = false;
                        if (operation.operands.size() == 1) {
                            if (operation.type == ir::Type(ir::TypeKind::i1) &&
                                (operation.operands[0] == "true" || operation.operands[0] == "false")) {
                                instruction.immediate = operation.operands[0] == "true" ? 1 : 0;
                                parsed_constant = true;
                            } else {
                                parsed_constant = float_value ? parse_float_bits(operation.operands[0], wide_float, instruction.immediate)
                                    : (wide_value ? parse_i64(operation.operands[0], instruction.immediate)
                                                  : parse_i32(operation.operands[0], instruction.immediate));
                            }
                        }
                        if (!parsed_constant) {
                            error(result.diagnostics, "invalid scalar constant during lowering in @" + function.name);
                            failed = true;
                        }
                    } else if (operation.opcode == "sizeof.array" || operation.opcode == "alignof.array") {
                        const auto& table = operation.opcode == "sizeof.array" ? array_sizes : array_alignments;
                        const auto found = operation.operands.size() == 1 && operation.operands[0].starts_with("@") ? table.find(operation.operands[0].substr(1)) : table.end();
                        if (operation.type != ir::Type(ir::TypeKind::i64) || operation.result.empty() || found == table.end()) {
                            error(result.diagnostics, "invalid array layout constant in @" + function.name);
                            failed = true;
                        } else {
                            instruction.opcode = Opcode::load_immediate_i64;
                            instruction.immediate = found->second;
                        }
                    } else if (operation.opcode == "sizeof.struct" || operation.opcode == "alignof.struct") {
                        instruction.opcode = Opcode::load_immediate_i64;
                        const auto& table = operation.opcode == "sizeof.struct" ? struct_sizes : struct_alignments;
                        const auto name = operation.operands.empty() || !operation.operands[0].starts_with("@") ? std::string{} : operation.operands[0].substr(1);
                        const auto found = table.find(name);
                        if (operation.type != ir::Type(ir::TypeKind::i64) || found == table.end()) {
                            error(result.diagnostics, "invalid structure layout constant in @" + function.name);
                            failed = true;
                        } else instruction.immediate = found->second;
                    } else if (operation.opcode == "copy") {
                        instruction.opcode = float_value ? (wide_float ? Opcode::copy_f64 : Opcode::copy_f32) : Opcode::copy;
                    } else if (operation.opcode == "bitcast") {
                        const auto source_type_it = operation.operands.empty() ? value_types.end() : value_types.find(operation.operands[0]);
                        const bool pointer_integer_pair = source_type_it != value_types.end() &&
                            ((source_type_it->second == ir::Type(ir::TypeKind::ptr) && operation.type == ir::Type(ir::TypeKind::i64)) ||
                             (source_type_it->second == ir::Type(ir::TypeKind::i64) && operation.type == ir::Type(ir::TypeKind::ptr)) ||
                             (source_type_it->second == ir::Type(ir::TypeKind::ptr) && operation.type == ir::Type(ir::TypeKind::ptr)));
                        if (!pointer_integer_pair) {
                            error(result.diagnostics, "invalid bitcast in @" + function.name);
                            failed = true;
                        } else {
                            instruction.opcode = Opcode::copy;
                        }
                    } else if (operation.opcode == "zero_extend" || operation.opcode == "sign_extend" || operation.opcode == "truncate") {
                        const auto source_type_it = operation.operands.empty() ? value_types.end() : value_types.find(operation.operands[0]);
                        if (source_type_it == value_types.end() || !source_type_it->second.is_integer() || !operation.type.is_integer()) {
                            error(result.diagnostics, "invalid integer cast in @" + function.name);
                            failed = true;
                        } else {
                            instruction.opcode = operation.opcode == "zero_extend" ? Opcode::zero_extend :
                                                 operation.opcode == "sign_extend" ? Opcode::sign_extend : Opcode::truncate;
                            instruction.immediate = static_cast<std::int64_t>((integer_width(source_type_it->second) << 8U) | integer_width(operation.type));
                        }
                    } else if (operation.opcode == "int_to_float.signed" || operation.opcode == "int_to_float.unsigned" ||
                               operation.opcode == "float_to_int.signed" || operation.opcode == "float_to_int.unsigned" ||
                               operation.opcode == "float_extend" || operation.opcode == "float_truncate") {
                        const auto source_type_it = operation.operands.empty() ? value_types.end() : value_types.find(operation.operands[0]);
                        if (source_type_it == value_types.end()) {
                            error(result.diagnostics, "numeric cast source is unavailable in @" + function.name);
                            failed = true;
                        } else {
                            if (operation.opcode == "int_to_float.signed") instruction.opcode = Opcode::int_to_float_signed;
                            else if (operation.opcode == "int_to_float.unsigned") instruction.opcode = Opcode::int_to_float_unsigned;
                            else if (operation.opcode == "float_to_int.signed") instruction.opcode = Opcode::float_to_int_signed;
                            else if (operation.opcode == "float_to_int.unsigned") instruction.opcode = Opcode::float_to_int_unsigned;
                            else if (operation.opcode == "float_extend") instruction.opcode = Opcode::float_extend;
                            else instruction.opcode = Opcode::float_truncate;
                            const auto source_width = source_type_it->second.is_integer() ? integer_width(source_type_it->second) : (source_type_it->second == ir::Type(ir::TypeKind::f64) ? 64U : 32U);
                            const auto result_width = operation.type.is_integer() ? integer_width(operation.type) : (operation.type == ir::Type(ir::TypeKind::f64) ? 64U : 32U);
                            instruction.immediate = static_cast<std::int64_t>((source_width << 8U) | result_width);
                        }
                    }
                    else if (operation.opcode == "select") instruction.opcode = wide_value ? Opcode::select_i64 : Opcode::select_i32;
                    else if (operation.opcode == "add") instruction.opcode = float_value ? (wide_float ? Opcode::add_f64 : Opcode::add_f32) : (wide_value ? Opcode::add_i64 : Opcode::add_i32);
                    else if (operation.opcode == "sub") instruction.opcode = float_value ? (wide_float ? Opcode::sub_f64 : Opcode::sub_f32) : (wide_value ? Opcode::sub_i64 : Opcode::sub_i32);
                    else if (operation.opcode == "mul") instruction.opcode = float_value ? (wide_float ? Opcode::mul_f64 : Opcode::mul_f32) : (wide_value ? Opcode::mul_i64 : Opcode::mul_i32);
                    else if (operation.opcode.starts_with("cmp.") && float_value) {
                        if (operation.opcode == "cmp.eq") instruction.opcode = wide_float ? Opcode::cmp_eq_f64 : Opcode::cmp_eq_f32;
                        else if (operation.opcode == "cmp.ne") instruction.opcode = wide_float ? Opcode::cmp_ne_f64 : Opcode::cmp_ne_f32;
                        else if (operation.opcode == "cmp.lt") instruction.opcode = wide_float ? Opcode::cmp_lt_f64 : Opcode::cmp_lt_f32;
                        else if (operation.opcode == "cmp.le") instruction.opcode = wide_float ? Opcode::cmp_le_f64 : Opcode::cmp_le_f32;
                        else if (operation.opcode == "cmp.gt") instruction.opcode = wide_float ? Opcode::cmp_gt_f64 : Opcode::cmp_gt_f32;
                        else instruction.opcode = wide_float ? Opcode::cmp_ge_f64 : Opcode::cmp_ge_f32;
                    }
                    else if (operation.opcode.starts_with("cmp.")) instruction.opcode = comparison_opcode(operation.opcode, wide_value);
                    else if (operation.opcode == "div" && float_value) instruction.opcode = wide_float ? Opcode::div_f64 : Opcode::div_f32;
                    else if (operation.opcode == "div.signed") instruction.opcode = wide_value ? Opcode::div_s_i64 : Opcode::div_s_i32;
                    else if (operation.opcode == "div.unsigned") instruction.opcode = wide_value ? Opcode::div_u_i64 : Opcode::div_u_i32;
                    else if (operation.opcode == "rem.signed") instruction.opcode = wide_value ? Opcode::rem_s_i64 : Opcode::rem_s_i32;
                    else if (operation.opcode == "rem.unsigned") instruction.opcode = wide_value ? Opcode::rem_u_i64 : Opcode::rem_u_i32;
                    else if (operation.opcode == "and") instruction.opcode = wide_value ? Opcode::and_i64 : Opcode::and_i32;
                    else if (operation.opcode == "or") instruction.opcode = wide_value ? Opcode::or_i64 : Opcode::or_i32;
                    else if (operation.opcode == "xor") instruction.opcode = wide_value ? Opcode::xor_i64 : Opcode::xor_i32;
                    else if (operation.opcode == "shl") instruction.opcode = wide_value ? Opcode::shl_i64 : Opcode::shl_i32;
                    else if (operation.opcode == "shr.signed") instruction.opcode = wide_value ? Opcode::shr_s_i64 : Opcode::shr_s_i32;
                    else if (operation.opcode == "shr.unsigned") instruction.opcode = wide_value ? Opcode::shr_u_i64 : Opcode::shr_u_i32;
                    else if (operation.opcode == "neg") instruction.opcode = float_value ? (wide_float ? Opcode::neg_f64 : Opcode::neg_f32) : (wide_value ? Opcode::neg_i64 : Opcode::neg_i32);
                    else if (operation.opcode == "not") instruction.opcode = wide_value ? Opcode::not_i64 : Opcode::not_i32;
                    else {
                        error(result.diagnostics, "unsupported opcode '" + operation.opcode + "' during lowering in @" + function.name);
                        failed = true;
                    }
                    if (!failed && instruction.opcode != Opcode::load_immediate && instruction.opcode != Opcode::load_immediate_i64 && instruction.opcode != Opcode::load_immediate_f32 && instruction.opcode != Opcode::load_immediate_f64) {
                        if (!append_inputs(operation, registers, instruction, result.diagnostics, function.name)) failed = true;

                        // x86-64 performs i8/i16 scalar arithmetic in 32-bit registers.  The
                        // low bits already preserve wrapping add/sub/mul/bitwise semantics, but
                        // signed/unsigned division, right shift and comparisons must first
                        // normalize the logical narrow value.  Do that in machine IR so every
                        // backend consumer sees the same semantics rather than relying on the
                        // unspecified high bits of an ABI/register value.
                        if (!failed && operation.type.is_integer() && integer_width(operation.type) < 32U) {
                            const auto source_bits = integer_width(operation.type);
                            const auto normalize_input = [&](std::size_t index, bool signed_value) {
                                if (index >= instruction.inputs.size()) return;
                                const auto normalized = allocate_register(ir::Type(ir::TypeKind::i32));
                                Instruction cast;
                                cast.opcode = signed_value ? Opcode::sign_extend : Opcode::zero_extend;
                                cast.result = normalized;
                                cast.inputs.push_back(instruction.inputs[index]);
                                cast.immediate = static_cast<std::int64_t>((source_bits << 8U) | 32U);
                                machine_block.instructions.push_back(std::move(cast));
                                instruction.inputs[index] = normalized;
                            };

                            const bool signed_division = operation.opcode == "div.signed" || operation.opcode == "rem.signed";
                            const bool unsigned_division = operation.opcode == "div.unsigned" || operation.opcode == "rem.unsigned";
                            const bool signed_shift = operation.opcode == "shr.signed";
                            const bool unsigned_shift = operation.opcode == "shr.unsigned";
                            const bool equality = operation.opcode == "cmp.eq" || operation.opcode == "cmp.ne";
                            const bool signed_compare = operation.opcode == "cmp.lt" || operation.opcode == "cmp.le" ||
                                                        operation.opcode == "cmp.gt" || operation.opcode == "cmp.ge";
                            const bool unsigned_compare = operation.opcode == "cmp.ult" || operation.opcode == "cmp.ule" ||
                                                          operation.opcode == "cmp.ugt" || operation.opcode == "cmp.uge";

                            if (signed_division || unsigned_division) {
                                normalize_input(0U, signed_division);
                                normalize_input(1U, signed_division);
                            } else if (signed_shift || unsigned_shift) {
                                normalize_input(0U, signed_shift);
                                // Shift counts are quantities, not signed data values.
                                normalize_input(1U, false);
                            } else if (equality || signed_compare || unsigned_compare) {
                                const bool signed_values = signed_compare;
                                normalize_input(0U, signed_values);
                                normalize_input(1U, signed_values);
                            }
                        }

                        const auto expected = (instruction.opcode == Opcode::select_i32 || instruction.opcode == Opcode::select_i64) ? 3U : (instruction.opcode == Opcode::copy || instruction.opcode == Opcode::copy_f32 || instruction.opcode == Opcode::copy_f64 || instruction.opcode == Opcode::neg_f32 || instruction.opcode == Opcode::neg_f64 || instruction.opcode == Opcode::neg_i32 || instruction.opcode == Opcode::neg_i64 || instruction.opcode == Opcode::not_i32 || instruction.opcode == Opcode::not_i64 || instruction.opcode == Opcode::zero_extend || instruction.opcode == Opcode::sign_extend || instruction.opcode == Opcode::truncate ||
                            instruction.opcode == Opcode::int_to_float_signed || instruction.opcode == Opcode::int_to_float_unsigned ||
                            instruction.opcode == Opcode::float_to_int_signed || instruction.opcode == Opcode::float_to_int_unsigned ||
                            instruction.opcode == Opcode::float_extend || instruction.opcode == Opcode::float_truncate) ? 1U : 2U;
                        if (!failed && instruction.inputs.size() != expected) {
                            error(result.diagnostics, "invalid operand count during lowering in @" + function.name);
                            failed = true;
                        }
                    }
                    if (!failed) registers.emplace(operation.result, instruction.result);
                }
                if (failed) break;
                if (emit) machine_block.instructions.push_back(std::move(instruction));
            }
        }

        lowered.local_stack_size = (lowered.local_stack_size + 15U) & ~15U;
        if (!failed) output.functions.push_back(std::move(lowered));
    }

    if (result.diagnostics.empty()) {
        // AArch64 runs architecture-neutral canonical combines only. x86-64
        // keeps its richer immediate/memory/flags/SLP selection pipeline until
        // equivalent AArch64 legality and cost hooks exist. This preserves a
        // clean target boundary while still eliminating target-independent IR
        // noise before AArch64 allocation and encoding.
        if (target_architecture == TargetArchitecture::aarch64)
            (void)optimize_aarch64_canonical_module(output);
        else
            (void)optimize_module(output);
        auto machine_diagnostics = verify_module(output);
        if (machine_diagnostics.empty()) result.module = std::move(output);
        else result.diagnostics = std::move(machine_diagnostics);
    }
    return result;
}

} // namespace forge::machine
