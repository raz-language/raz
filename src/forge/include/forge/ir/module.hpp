// Copyright 2026 Mario Vinciguerra
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include <cstdint>
#include <string>
#include <utility>
#include <vector>
#include "forge/ir/type.hpp"

namespace forge::ir {

using ValueId = std::uint32_t;

enum class AggregateRefKind : std::uint8_t { scalar, structure, array };
enum class BorrowMode : std::uint8_t { none, immutable, mutable_ };
enum class CallingConvention : std::uint8_t { platform, c, system_v, windows_x64, fast };
enum class SymbolLinkage : std::uint8_t { external, internal, weak };
enum class SymbolVisibility : std::uint8_t { default_, hidden };

struct ValueDecl {
    std::string name;
    Type type;
    AggregateRefKind aggregate_kind{AggregateRefKind::scalar};
    std::string aggregate_name;
    bool owned{};
    BorrowMode borrow_mode{BorrowMode::none};
    std::string function_signature_name;

    ValueDecl() = default;
    ValueDecl(std::string value_name, Type value_type)
        : name(std::move(value_name)), type(value_type) {}

    [[nodiscard]] bool is_aggregate() const noexcept { return aggregate_kind != AggregateRefKind::scalar; }
};

struct StructField {
    std::string name;
    Type type;
    AggregateRefKind aggregate_kind{AggregateRefKind::scalar};
    std::string aggregate_name;

    StructField() = default;
    StructField(std::string field_name, Type field_type,
                AggregateRefKind kind = AggregateRefKind::scalar, std::string referenced_name = {})
        : name(std::move(field_name)), type(field_type), aggregate_kind(kind), aggregate_name(std::move(referenced_name)) {}

    [[nodiscard]] bool is_aggregate() const noexcept { return aggregate_kind != AggregateRefKind::scalar; }
};
struct StructDecl { std::string name; std::vector<StructField> fields; bool move_only{}; };
struct ArrayDecl {
    std::string name;
    Type element_type;
    AggregateRefKind element_aggregate_kind{AggregateRefKind::scalar};
    std::string element_aggregate_name;
    std::uint32_t element_count{};
    bool move_only{};

    [[nodiscard]] bool has_aggregate_elements() const noexcept { return element_aggregate_kind != AggregateRefKind::scalar; }
};

struct Global {
    std::string name;
    Type type;
    AggregateRefKind aggregate_kind{AggregateRefKind::scalar};
    std::string aggregate_name;
    std::string function_signature_name;
    bool is_constant{};
    bool is_external{};
    bool is_thread_local{};
    SymbolLinkage linkage{SymbolLinkage::external};
    SymbolVisibility visibility{SymbolVisibility::default_};
    std::string initializer;
    std::uint32_t element_count{1};
    std::uint32_t alignment{};
    bool zero_initialized{};
    std::vector<std::uint8_t> bytes;

    [[nodiscard]] bool is_byte_array() const noexcept { return aggregate_kind == AggregateRefKind::scalar && element_count != 1; }
    [[nodiscard]] bool is_named_aggregate() const noexcept { return aggregate_kind != AggregateRefKind::scalar; }
};

struct Attribute {
    std::string name;
    std::string value;
};

struct Operation {
    std::string result;
    std::string opcode;
    Type type;
    std::vector<std::string> operands;
    std::vector<std::string> successors;
    std::vector<std::vector<std::string>> successor_arguments;
    std::uint32_t alignment{};
    std::string source_file;
    std::uint32_t source_line{};
    std::uint32_t source_column{};
    std::uint32_t source_end_line{};
    std::uint32_t source_end_column{};
    std::vector<Attribute> attributes;

    [[nodiscard]] bool is_terminator() const noexcept {
        return opcode == "return" || opcode == "jump" || opcode == "branch" || opcode == "unreachable";
    }
    [[nodiscard]] bool has_side_effects() const noexcept {
        return is_terminator() || opcode == "store" || opcode == "memory.copy" || opcode == "memory.set" || opcode == "aggregate.copy.struct" || opcode == "aggregate.copy.array" || opcode == "aggregate.move.struct" || opcode == "aggregate.move.array" || opcode == "aggregate.borrow.struct" || opcode == "aggregate.borrow.array" || opcode == "aggregate.borrow.mut.struct" || opcode == "aggregate.borrow.mut.array" || opcode == "aggregate.borrow.end.struct" || opcode == "aggregate.borrow.end.array" || opcode == "aggregate.end.struct" || opcode == "aggregate.end.array" || opcode == "aggregate.zero.struct" || opcode == "aggregate.zero.array" || opcode == "call" || opcode == "call.indirect";
    }
};

struct Block {
    std::string name;
    std::vector<ValueDecl> parameters;
    std::vector<Operation> operations;
};

struct Function {
    std::string name;
    bool is_external{};
    bool is_signature{};
    bool variadic{};
    // Optional per-function ISA contract emitted by Raz (@target_feature).
    // Empty means the baseline target. Known values are sse2, avx2, and neon.
    std::string target_feature;
    CallingConvention calling_convention{CallingConvention::platform};
    SymbolLinkage linkage{SymbolLinkage::external};
    SymbolVisibility visibility{SymbolVisibility::default_};
    Type return_type;
    AggregateRefKind return_aggregate_kind{AggregateRefKind::scalar};
    std::string return_aggregate_name;
    bool return_owned{};
    BorrowMode return_borrow_mode{BorrowMode::none};
    std::int32_t return_borrow_parameter{-1};
    [[nodiscard]] bool returns_aggregate() const noexcept { return return_aggregate_kind != AggregateRefKind::scalar; }
    std::vector<ValueDecl> parameters;
    std::vector<Block> blocks;
};

class Module {
public:
    explicit Module(std::string name = "anonymous") : name_(std::move(name)) {}
    [[nodiscard]] const std::string& name() const noexcept { return name_; }
    [[nodiscard]] std::vector<StructDecl>& structs() noexcept { return structs_; }
    [[nodiscard]] const std::vector<StructDecl>& structs() const noexcept { return structs_; }
    [[nodiscard]] std::vector<ArrayDecl>& arrays() noexcept { return arrays_; }
    [[nodiscard]] const std::vector<ArrayDecl>& arrays() const noexcept { return arrays_; }
    [[nodiscard]] std::vector<Global>& globals() noexcept { return globals_; }
    [[nodiscard]] const std::vector<Global>& globals() const noexcept { return globals_; }
    [[nodiscard]] std::vector<Function>& functions() noexcept { return functions_; }
    [[nodiscard]] std::vector<Attribute>& metadata() noexcept { return metadata_; }
    [[nodiscard]] const std::vector<Attribute>& metadata() const noexcept { return metadata_; }
    [[nodiscard]] const std::vector<Function>& functions() const noexcept { return functions_; }
private:
    std::string name_;
    std::vector<StructDecl> structs_;
    std::vector<ArrayDecl> arrays_;
    std::vector<Global> globals_;
    std::vector<Function> functions_;
    std::vector<Attribute> metadata_;
};

} // namespace forge::ir
