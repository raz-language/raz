// Copyright 2026 Mario Vinciguerra
// SPDX-License-Identifier: Apache-2.0

#include "forge/ir/builder.hpp"
#include "forge/ir/verifier.hpp"
#include <algorithm>
#include <stdexcept>
#include <utility>

namespace forge::ir {

Function& IRBuilder::create_function(std::string name, Type return_type,
                                     std::vector<ValueDecl> parameters, bool external) {
    if (name.empty()) throw std::invalid_argument("function name must not be empty");
    if (std::ranges::any_of(module_->functions(), [&](const Function& function) { return function.name == name; }))
        throw std::invalid_argument("duplicate function @" + name);
    Function function;
    function.name = std::move(name);
    function.return_type = return_type;
    function.parameters = std::move(parameters);
    function.is_external = external;
    module_->functions().push_back(std::move(function));
    return module_->functions().back();
}

Block& IRBuilder::create_block(Function& function, std::string name,
                               std::vector<ValueDecl> parameters) {
    if (name.empty()) throw std::invalid_argument("block name must not be empty");
    if (std::ranges::any_of(function.blocks, [&](const Block& block) { return block.name == name; }))
        throw std::invalid_argument("duplicate block ^" + name + " in @" + function.name);
    Block block;
    block.name = std::move(name);
    block.parameters = std::move(parameters);
    function.blocks.push_back(std::move(block));
    return function.blocks.back();
}

FunctionHandle IRBuilder::create_function_handle(std::string name, Type return_type,
                                                  std::vector<ValueDecl> parameters, bool external) {
    (void)create_function(std::move(name), return_type, std::move(parameters), external);
    return FunctionHandle{module_->functions().size() - 1};
}

BlockHandle IRBuilder::create_block_handle(FunctionHandle function, std::string name,
                                            std::vector<ValueDecl> parameters) {
    auto& resolved = resolve(function);
    (void)create_block(resolved, std::move(name), std::move(parameters));
    return BlockHandle{function.index, resolved.blocks.size() - 1};
}

Function& IRBuilder::resolve(FunctionHandle handle) {
    if (handle.index >= module_->functions().size()) throw std::out_of_range("function handle out of range");
    return module_->functions()[handle.index];
}

const Function& IRBuilder::resolve(FunctionHandle handle) const {
    if (handle.index >= module_->functions().size()) throw std::out_of_range("function handle out of range");
    return module_->functions()[handle.index];
}

Block& IRBuilder::resolve(BlockHandle handle) {
    auto& function = resolve(FunctionHandle{handle.function_index});
    if (handle.block_index >= function.blocks.size()) throw std::out_of_range("block handle out of range");
    return function.blocks[handle.block_index];
}

const Block& IRBuilder::resolve(BlockHandle handle) const {
    const auto& function = resolve(FunctionHandle{handle.function_index});
    if (handle.block_index >= function.blocks.size()) throw std::out_of_range("block handle out of range");
    return function.blocks[handle.block_index];
}

FunctionHandle IRBuilder::find_function(std::string_view name) const {
    for (std::size_t i = 0; i < module_->functions().size(); ++i)
        if (module_->functions()[i].name == name) return FunctionHandle{i};
    throw std::out_of_range("unknown function @" + std::string(name));
}

BlockHandle IRBuilder::find_block(FunctionHandle function, std::string_view name) const {
    const auto& resolved = resolve(function);
    for (std::size_t i = 0; i < resolved.blocks.size(); ++i)
        if (resolved.blocks[i].name == name) return BlockHandle{function.index, i};
    throw std::out_of_range("unknown block ^" + std::string(name));
}

void IRBuilder::position_at_end(Block& block) {
    for (std::size_t function_index = 0; function_index < module_->functions().size(); ++function_index) {
        auto& blocks = module_->functions()[function_index].blocks;
        for (std::size_t block_index = 0; block_index < blocks.size(); ++block_index) {
            if (&blocks[block_index] == &block) {
                block_ = BlockHandle{function_index, block_index};
                return;
            }
        }
    }

    throw std::invalid_argument("block does not belong to this IRBuilder module");
}

std::string IRBuilder::next_value_name() { return "%v" + std::to_string(next_value_++); }

void IRBuilder::set_module_metadata(std::string name, std::string value) {
    if (name.empty()) throw std::invalid_argument("metadata name must not be empty");
    auto& metadata = module_->metadata();
    const auto found = std::ranges::find_if(metadata, [&](const Attribute& attribute) { return attribute.name == name; });
    if (found != metadata.end()) found->value = std::move(value);
    else metadata.push_back({std::move(name), std::move(value)});
}

std::string_view IRBuilder::module_metadata(std::string_view name) const {
    const auto& metadata = module_->metadata();
    const auto found = std::ranges::find_if(metadata, [&](const Attribute& attribute) { return attribute.name == name; });
    return found == metadata.end() ? std::string_view{} : std::string_view(found->value);
}

void IRBuilder::set_next_attribute(std::string name, std::string value) {
    if (name.empty()) throw std::invalid_argument("attribute name must not be empty");
    const auto found = std::ranges::find_if(next_attributes_, [&](const Attribute& attribute) { return attribute.name == name; });
    if (found != next_attributes_.end()) found->value = std::move(value);
    else next_attributes_.push_back({std::move(name), std::move(value)});
}

bool IRBuilder::insertion_block_terminated() const noexcept {
    if (!block_) return false;
    try {
        const auto& block = resolve(*block_);
        return !block.operations.empty() && block.operations.back().is_terminator();
    } catch (...) {
        return false;
    }
}

Diagnostics IRBuilder::verify() const { return verify_module(*module_); }

Operation& IRBuilder::append(Operation operation) {
    if (!block_) {
        throw std::logic_error("IRBuilder has no insertion block");
    }

    if (insertion_block_terminated()) {
        const auto location = location_.file.empty() ? std::string{} :
            location_.file + ":" + std::to_string(location_.line) + ":" + std::to_string(location_.column) + ": ";
        throw std::logic_error(location + "cannot append " + operation.opcode + " after terminator in ^" + resolve(*block_).name);
    }
    operation.source_file = location_.file;
    operation.source_line = location_.line;
    operation.source_column = location_.column;
    operation.source_end_line = location_.end_line;
    operation.source_end_column = location_.end_column;
    operation.attributes = std::move(next_attributes_);
    next_attributes_.clear();
    auto& block = resolve(*block_);
    block.operations.push_back(std::move(operation));
    return block.operations.back();
}

std::string IRBuilder::create_constant(Type type, std::string literal) {
    Operation operation;
    operation.result = next_value_name();
    operation.opcode = std::string(opcode_name(Opcode::constant));
    operation.type = type;
    operation.operands.push_back(std::move(literal));
    const auto result = operation.result;
    append(std::move(operation));
    return result;
}

std::string IRBuilder::create_binary(Opcode opcode, Type type, std::string lhs, std::string rhs) {
    Operation operation;
    operation.result = next_value_name();
    operation.opcode = std::string(opcode_name(opcode));
    operation.type = type;
    operation.operands = {std::move(lhs), std::move(rhs)};
    const auto result = operation.result;
    append(std::move(operation));
    return result;
}

std::string IRBuilder::create_compare(Opcode opcode, Type operand_type,
                                      std::string lhs, std::string rhs) {
    Operation operation;
    operation.result = next_value_name();
    operation.opcode = std::string(opcode_name(opcode));
    operation.type = operand_type;
    operation.operands = {std::move(lhs), std::move(rhs)};
    const auto result = operation.result;
    append(std::move(operation));
    return result;
}

std::string IRBuilder::create_copy(Type type, std::string value) {
    Operation operation;
    operation.result = next_value_name();
    operation.opcode = std::string(opcode_name(Opcode::copy));
    operation.type = type;
    operation.operands.push_back(std::move(value));
    const auto result = operation.result;
    append(std::move(operation));
    return result;
}

std::string IRBuilder::create_cast(Opcode opcode, Type type, std::string value) {
    Operation operation;
    operation.opcode = std::string(opcode_name(opcode));
    operation.result = next_value_name();
    operation.type = type;
    operation.operands = {std::move(value)};
    auto& added = append(std::move(operation));
    return added.result;
}

std::string IRBuilder::create_stack_allocation(std::uint64_t size, std::uint32_t alignment) {
    if (size == 0) throw std::invalid_argument("stack allocation size must be nonzero");
    Operation operation;
    operation.result = next_value_name();
    operation.opcode = std::string(opcode_name(Opcode::stack_allocate));
    operation.type = ptr_type();
    operation.operands.push_back(std::to_string(size));
    operation.alignment = alignment;
    const auto result = operation.result;
    append(std::move(operation));
    return result;
}

std::string IRBuilder::create_load(Type type, std::string address, std::uint32_t alignment) {
    Operation operation;
    operation.result = next_value_name();
    operation.opcode = std::string(opcode_name(Opcode::load));
    operation.type = type;
    operation.operands.push_back(std::move(address));
    operation.alignment = alignment;
    const auto result = operation.result;
    append(std::move(operation));
    return result;
}

void IRBuilder::create_store(Type type, std::string value, std::string address, std::uint32_t alignment) {
    Operation operation;
    operation.opcode = std::string(opcode_name(Opcode::store));
    operation.type = type;
    operation.operands = {std::move(value), std::move(address)};
    operation.alignment = alignment;
    append(std::move(operation));
}

std::string IRBuilder::create_pointer_offset(std::string base, std::string byte_offset) {
    Operation operation;
    operation.result = next_value_name();
    operation.opcode = std::string(opcode_name(Opcode::pointer_offset));
    operation.type = ptr_type();
    operation.operands = {std::move(base), std::move(byte_offset)};
    const auto result = operation.result;
    append(std::move(operation));
    return result;
}

std::string IRBuilder::create_function_address(std::string function) {
    Operation operation;
    operation.result = next_value_name();
    operation.opcode = "func.address";
    operation.type = ptr_type();
    if (!function.starts_with('@')) function.insert(function.begin(), '@');
    operation.operands.push_back(std::move(function));
    const auto result = operation.result;
    append(std::move(operation));
    return result;
}

std::string IRBuilder::create_call(Type return_type, std::string callee,
                                   std::vector<std::string> arguments) {
    Operation operation;
    if (return_type.kind() != TypeKind::void_) operation.result = next_value_name();
    operation.opcode = std::string(opcode_name(Opcode::call));
    operation.type = return_type;
    if (!callee.starts_with('@')) callee.insert(callee.begin(), '@');
    operation.operands.push_back(std::move(callee));
    for (auto& argument : arguments) operation.operands.push_back(std::move(argument));
    const auto result = operation.result;
    append(std::move(operation));
    return result;
}

std::string IRBuilder::create_indirect_call(Type return_type, std::string pointer,
                                            std::string signature,
                                            std::vector<std::string> arguments) {
    Operation operation;
    if (return_type.kind() != TypeKind::void_) operation.result = next_value_name();
    operation.opcode = std::string(opcode_name(Opcode::call_indirect));
    operation.type = return_type;
    if (!signature.starts_with('@')) signature.insert(signature.begin(), '@');
    operation.operands.push_back(std::move(pointer));
    operation.operands.push_back(std::move(signature));
    for (auto& argument : arguments) operation.operands.push_back(std::move(argument));
    const auto result = operation.result;
    append(std::move(operation));
    return result;
}

void IRBuilder::create_return(std::string value) {
    Operation operation;
    operation.opcode = std::string(opcode_name(Opcode::return_));
    if (!value.empty()) operation.operands.push_back(std::move(value));
    append(std::move(operation));
}

void IRBuilder::create_jump(std::string successor, std::vector<std::string> arguments) {
    Operation operation;
    operation.opcode = std::string(opcode_name(Opcode::jump));
    operation.successors.push_back(std::move(successor));
    operation.successor_arguments.push_back(std::move(arguments));
    append(std::move(operation));
}

void IRBuilder::create_branch(std::string condition, std::string true_successor,
                              std::string false_successor,
                              std::vector<std::string> true_arguments,
                              std::vector<std::string> false_arguments) {
    Operation operation;
    operation.opcode = std::string(opcode_name(Opcode::branch));
    operation.type = Type{TypeKind::i1};
    operation.operands.push_back(std::move(condition));
    operation.successors = {std::move(true_successor), std::move(false_successor)};
    operation.successor_arguments = {std::move(true_arguments), std::move(false_arguments)};
    append(std::move(operation));
}

void IRBuilder::create_unreachable() {
    Operation operation;
    operation.opcode = std::string(opcode_name(Opcode::unreachable));
    append(std::move(operation));
}

} // namespace forge::ir
