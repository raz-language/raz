// Copyright 2026 Mario Vinciguerra
// SPDX-License-Identifier: Apache-2.0

#include "forge/transforms/scalar.hpp"

#include <algorithm>
#include <charconv>
#include <optional>
#include <unordered_map>
#include <unordered_set>

namespace forge::transforms {
namespace {
std::optional<long long> number(const std::string& text) {
    long long value{};
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (error == std::errc{} && end == text.data() + text.size()) return value;
    return {};
}

}

pass::PassResult ConstantFoldPass::run(ir::Function& function,
                                       analysis::FunctionAnalysisManager& analyses) {
    struct ConstantDefinition {
        long long value{};
        std::string block;
        std::size_t operation_index{};
    };
    pass::PassResult result;
    result.invalidation = analysis::InvalidationScope::operations;
    std::unordered_map<std::string, ConstantDefinition> constants;
    const auto& dominators = analyses.dominators();
    for (auto& block : function.blocks) {
        for (std::size_t index = 0; index < block.operations.size(); ++index) {
            auto& operation = block.operations[index];
            if (operation.opcode == "const" && !operation.result.empty() && operation.operands.size() == 1) {
                if (auto value = number(operation.operands[0]))
                    constants[operation.result] = {*value, block.name, index};
                continue;
            }
            if ((operation.opcode == "add" || operation.opcode == "sub" ||
                 operation.opcode == "mul" || operation.opcode == "div") &&
                !operation.result.empty() && operation.operands.size() == 2) {
                const auto available = [&](const std::string& name) -> std::optional<long long> {
                    const auto found = constants.find(name);
                    if (found == constants.end()) return {};
                    const auto& definition = found->second;
                    const bool dominates = definition.block == block.name
                        ? definition.operation_index < index
                        : dominators.dominates(definition.block, block.name);
                    if (!dominates) return {};
                    return definition.value;
                };
                const auto left = available(operation.operands[0]);
                const auto right = available(operation.operands[1]);
                if (!left || !right) continue;
                if (operation.opcode == "div" && *right == 0) continue;
                long long value{};
                if (operation.opcode == "add") value = *left + *right;
                else if (operation.opcode == "sub") value = *left - *right;
                else if (operation.opcode == "mul") value = *left * *right;
                else value = *left / *right;
                operation.opcode = "const";
                operation.operands = {std::to_string(value)};
                constants[operation.result] = {value, block.name, index};
                result.changed = true;
                ++result.operations_rewritten;
                result.touch_block(block.name);
            }
        }
    }
    return result;
}

pass::PassResult CopyPropagationPass::run(ir::Function& function,
                                          analysis::FunctionAnalysisManager& analyses) {
    struct CopyDefinition {
        std::string source;
        std::string block;
        std::size_t operation_index{};
    };

    pass::PassResult result;
    result.invalidation = analysis::InvalidationScope::operations;
    std::unordered_map<std::string, CopyDefinition> copies;
    for (const auto& block : function.blocks) {
        for (std::size_t index = 0; index < block.operations.size(); ++index) {
            const auto& operation = block.operations[index];
            if (operation.opcode == "copy" && !operation.result.empty() &&
                operation.operands.size() == 1) {
                copies.emplace(operation.result,
                               CopyDefinition{operation.operands.front(), block.name, index});
            }
        }
    }

    if (copies.empty()) return result;

    const auto& dominators = analyses.dominators();
    for (auto& block : function.blocks) {
        bool block_changed = false;
        for (std::size_t index = 0; index < block.operations.size(); ++index) {
            auto& operation = block.operations[index];
            const auto rewrite = [&](std::string& value) {
                const auto iterator = copies.find(value);
                if (iterator == copies.end()) return;
                const auto& copy = iterator->second;
                const bool available = copy.block == block.name
                    ? copy.operation_index < index
                    : dominators.dominates(copy.block, block.name);
                if (!available || value == copy.source) return;
                value = copy.source;
                block_changed = true;
            };
            for (auto& operand : operation.operands) rewrite(operand);
            for (auto& arguments : operation.successor_arguments)
                for (auto& argument : arguments) rewrite(argument);
        }
        if (block_changed) {
            result.changed = true;
            result.touch_block(block.name);
        }
    }

    // This pass consumes use-def after mutating operands. Repair the already
    // materialized cache locally before using it to decide which copies died.
    if (!result.touched_blocks.empty()) analyses.repair_use_def(result.touched_blocks);
    const auto& uses = analyses.use_def();
    for (auto& block : function.blocks) {
        const auto before = block.operations.size();
        block.operations.erase(std::remove_if(block.operations.begin(), block.operations.end(),
            [&](const ir::Operation& operation) {
                const auto iterator = copies.find(operation.result);
                if (operation.opcode != "copy" || iterator == copies.end()) return false;
                const auto use_iterator = uses.use_count.find(operation.result);
                return use_iterator == uses.use_count.end() || use_iterator->second == 0;
            }), block.operations.end());
        const auto removed = before - block.operations.size();
        if (removed != 0) {
            result.operations_removed += removed;
            result.changed = true;
            result.touch_block(block.name);
        }
    }
    return result;
}

pass::PassResult BranchFoldPass::run(ir::Function& function,
                                     analysis::FunctionAnalysisManager& analyses) {
    struct ConstantDefinition {
        long long value{};
        std::string block;
        std::size_t operation_index{};
    };

    pass::PassResult result;
    result.invalidation = analysis::InvalidationScope::control_flow;
    std::unordered_map<std::string, ConstantDefinition> constants;
    for (const auto& block : function.blocks) {
        for (std::size_t index = 0; index < block.operations.size(); ++index) {
            const auto& operation = block.operations[index];
            if (operation.opcode == "const" && operation.operands.size() == 1 &&
                !operation.result.empty()) {
                if (auto value = number(operation.operands.front()))
                    constants[operation.result] = {*value, block.name, index};
            }
        }
    }

    const auto& dominators = analyses.dominators();
    for (auto& block : function.blocks) {
        if (block.operations.empty()) continue;
        auto& terminator = block.operations.back();
        if (terminator.opcode != "branch" || terminator.operands.size() != 1 ||
            terminator.successors.size() != 2 || terminator.successor_arguments.size() != 2)
            continue;
        const auto condition = constants.find(terminator.operands.front());
        if (condition == constants.end()) continue;
        const auto terminator_index = block.operations.size() - 1;
        const bool available = condition->second.block == block.name
            ? condition->second.operation_index < terminator_index
            : dominators.dominates(condition->second.block, block.name);
        if (!available) continue;
        const std::size_t selected = condition->second.value != 0 ? 0 : 1;
        terminator.opcode = "jump";
        terminator.operands.clear();
        terminator.successors = {terminator.successors[selected]};
        terminator.successor_arguments = {terminator.successor_arguments[selected]};
        result.changed = true;
        result.touch_block(block.name);
        ++result.operations_rewritten;
    }
    return result;
}

pass::PassResult ScalarStackPromotionPass::run(
    ir::Function& function, analysis::FunctionAnalysisManager& analyses) {
    pass::PassResult result;
    result.invalidation = analysis::InvalidationScope::operations;

    // Promote non-escaping scalar entry slots across arbitrary reducible
    // control flow.  Slot liveness determines where block parameters are
    // required; iterative renaming then wires both forward edges and backedges.
    const auto& cfg = analyses.cfg();
    std::unordered_map<std::string, std::size_t> block_index;
    for (std::size_t index = 0; index < function.blocks.size(); ++index)
        block_index.emplace(function.blocks[index].name, index);

    if (!function.blocks.empty()) {
        struct CrossCandidate {
            std::size_t allocation_index{};
            ir::Type stored_type;
            bool has_type{};
            bool valid{true};
            std::unordered_set<std::string> access_blocks;
            std::string function_signature_name;
        };
        std::unordered_map<std::string, CrossCandidate> cross_candidates;
        const auto& entry = function.blocks.front();
        for (std::size_t index = 0; index < entry.operations.size(); ++index) {
            const auto& operation = entry.operations[index];
            if (operation.opcode == "stack.alloc" && !operation.result.empty()) {
                std::string function_signature_name;
                const auto callback_signature = std::find_if(operation.attributes.begin(), operation.attributes.end(), [](const ir::Attribute& attribute) {
                    return attribute.name == "callback.signature";
                });
                if (callback_signature != operation.attributes.end()) function_signature_name = callback_signature->value;
                cross_candidates.emplace(operation.result,
                    CrossCandidate{index, ir::Type(ir::TypeKind::void_), false, true, {}, std::move(function_signature_name)});
            }
        }

        for (const auto& block : function.blocks) {
            for (const auto& operation : block.operations) {
                for (std::size_t operand_index = 0; operand_index < operation.operands.size(); ++operand_index) {
                    const auto found = cross_candidates.find(operation.operands[operand_index]);
                    if (found == cross_candidates.end()) continue;
                    auto& candidate = found->second;
                    const bool scalar_load = operation.opcode == "load" && operand_index == 0U && operation.operands.size() == 1U;
                    const bool scalar_store = operation.opcode == "store" && operand_index == 1U && operation.operands.size() == 2U;
                    if (!scalar_load && !scalar_store) {
                        candidate.valid = false;
                        continue;
                    }
                    candidate.access_blocks.insert(block.name);
                    if (!candidate.has_type) {
                        candidate.stored_type = operation.type;
                        candidate.has_type = true;
                    } else if (candidate.stored_type != operation.type) {
                        candidate.valid = false;
                    }
                }
                for (const auto& arguments : operation.successor_arguments)
                    for (const auto& argument : arguments)
                        if (const auto found = cross_candidates.find(argument); found != cross_candidates.end())
                            found->second.valid = false;
            }
        }

        for (const auto& [slot, candidate] : cross_candidates) {
            if (!candidate.valid || !candidate.has_type || candidate.access_blocks.size() < 2U) continue;

            std::unordered_map<std::string, bool> use_before_definition;
            std::unordered_map<std::string, bool> defines;
            for (const auto& block : function.blocks) {
                if (!cfg.reachable.contains(block.name)) continue;
                bool defined = false;
                bool uses_incoming = false;
                for (const auto& operation : block.operations) {
                    if (operation.opcode == "store" && operation.operands.size() == 2U && operation.operands[1] == slot) {
                        defined = true;
                    } else if (operation.opcode == "load" && operation.operands.size() == 1U && operation.operands[0] == slot && !defined) {
                        uses_incoming = true;
                    }
                }
                use_before_definition[block.name] = uses_incoming;
                defines[block.name] = defined;
            }

            std::unordered_map<std::string, bool> live_in;
            std::unordered_map<std::string, bool> live_out;
            bool liveness_changed = true;
            while (liveness_changed) {
                liveness_changed = false;
                for (auto block_it = function.blocks.rbegin(); block_it != function.blocks.rend(); ++block_it) {
                    const auto& block = *block_it;
                    if (!cfg.reachable.contains(block.name)) continue;
                    bool out = false;
                    if (const auto successors = cfg.successors.find(block.name); successors != cfg.successors.end())
                        for (const auto& successor : successors->second)
                            if (cfg.reachable.contains(successor) && live_in[successor]) out = true;
                    const bool in = use_before_definition[block.name] || (out && !defines[block.name]);
                    if (live_out[block.name] != out || live_in[block.name] != in) {
                        live_out[block.name] = out;
                        live_in[block.name] = in;
                        liveness_changed = true;
                    }
                }
            }

            std::unordered_map<std::string, std::string> parameters;
            std::string stem = slot;
            if (!stem.empty() && stem.front() == '%') stem.erase(stem.begin());
            for (const auto& block : function.blocks) {
                if (block.name == function.blocks.front().name || !live_in[block.name]) continue;
                std::size_t predecessor_count = 0;
                if (const auto predecessors = cfg.predecessors.find(block.name); predecessors != cfg.predecessors.end())
                    for (const auto& predecessor : predecessors->second)
                        if (cfg.reachable.contains(predecessor)) ++predecessor_count;
                if (predecessor_count >= 2U)
                    parameters.emplace(block.name, "%mem2reg." + stem + "." + block.name);
            }

            std::unordered_map<std::string, std::string> outgoing;
            const std::size_t iteration_limit = std::max<std::size_t>(4U, function.blocks.size() * 4U);
            for (std::size_t iteration = 0; iteration < iteration_limit; ++iteration) {
                bool changed = false;
                for (const auto& block : function.blocks) {
                    if (!cfg.reachable.contains(block.name)) continue;
                    std::string current;
                    if (const auto parameter = parameters.find(block.name); parameter != parameters.end()) {
                        current = parameter->second;
                    } else if (block.name != function.blocks.front().name) {
                        std::string common;
                        bool all_known = true;
                        bool first = true;
                        if (const auto predecessors = cfg.predecessors.find(block.name); predecessors != cfg.predecessors.end()) {
                            for (const auto& predecessor : predecessors->second) {
                                if (!cfg.reachable.contains(predecessor)) continue;
                                const auto incoming = outgoing.find(predecessor);
                                if (incoming == outgoing.end() || incoming->second.empty()) {
                                    all_known = false;
                                    break;
                                }
                                if (first) {
                                    common = incoming->second;
                                    first = false;
                                } else if (common != incoming->second) {
                                    all_known = false;
                                    break;
                                }
                            }
                        }
                        if (all_known && !first) current = common;
                    }

                    for (const auto& operation : block.operations) {
                        if (operation.opcode == "store" && operation.operands.size() == 2U && operation.operands[1] == slot)
                            current = operation.operands[0];
                    }
                    if (outgoing[block.name] != current) {
                        outgoing[block.name] = current;
                        changed = true;
                    }
                }
                if (!changed) break;
            }

            std::unordered_map<std::string, std::string> load_replacements;
            bool safe = true;
            for (const auto& block : function.blocks) {
                if (!cfg.reachable.contains(block.name)) continue;
                std::string current;
                if (const auto parameter = parameters.find(block.name); parameter != parameters.end()) {
                    current = parameter->second;
                } else if (block.name != function.blocks.front().name) {
                    bool first = true;
                    if (const auto predecessors = cfg.predecessors.find(block.name); predecessors != cfg.predecessors.end()) {
                        for (const auto& predecessor : predecessors->second) {
                            if (!cfg.reachable.contains(predecessor)) continue;
                            const auto incoming = outgoing.find(predecessor);
                            if (incoming == outgoing.end() || incoming->second.empty()) { safe = false; break; }
                            if (first) { current = incoming->second; first = false; }
                            else if (current != incoming->second) { safe = false; break; }
                        }
                    }
                    if (!safe) break;
                }
                for (const auto& operation : block.operations) {
                    if (operation.opcode == "store" && operation.operands.size() == 2U && operation.operands[1] == slot) {
                        current = operation.operands[0];
                    } else if (operation.opcode == "load" && operation.operands.size() == 1U && operation.operands[0] == slot) {
                        if (current.empty()) { safe = false; break; }
                        load_replacements.emplace(operation.result, current);
                    }
                }
                if (!safe) break;
            }
            if (!safe) continue;

            for (const auto& [block_name, parameter] : parameters) {
                auto& block = function.blocks[block_index.at(block_name)];
                ir::ValueDecl promoted_parameter(parameter, candidate.stored_type);
                promoted_parameter.function_signature_name = candidate.function_signature_name;
                block.parameters.push_back(std::move(promoted_parameter));
                const auto predecessors = cfg.predecessors.find(block_name);
                if (predecessors == cfg.predecessors.end()) { safe = false; break; }
                for (const auto& predecessor : predecessors->second) {
                    if (!cfg.reachable.contains(predecessor)) continue;
                    const auto incoming = outgoing.find(predecessor);
                    if (incoming == outgoing.end() || incoming->second.empty()) { safe = false; break; }
                    auto& terminator = function.blocks[block_index.at(predecessor)].operations.back();
                    for (std::size_t edge = 0; edge < terminator.successors.size(); ++edge) {
                        if (terminator.successors[edge] != block_name) continue;
                        if (terminator.successor_arguments.size() < terminator.successors.size())
                            terminator.successor_arguments.resize(terminator.successors.size());
                        terminator.successor_arguments[edge].push_back(incoming->second);
                    }
                }
                if (!safe) break;
            }
            if (!safe) continue;

            for (auto& block : function.blocks) {
                for (auto& operation : block.operations) {
                    if (operation.opcode == "store" && operation.operands.size() == 2U && operation.operands[1] == slot) {
                        operation.opcode.clear();
                        operation.result.clear();
                        operation.operands.clear();
                        operation.successors.clear();
                        operation.successor_arguments.clear();
                        operation.type = ir::Type(ir::TypeKind::void_);
                        ++result.operations_removed;
                        result.changed = true;
                    } else if (operation.opcode == "load" && operation.operands.size() == 1U && operation.operands[0] == slot) {
                        const auto replacement = load_replacements.find(operation.result);
                        if (replacement == load_replacements.end()) continue;
                        operation.opcode = "copy";
                        operation.operands = {replacement->second};
                        operation.alignment = 0;
                        ++result.operations_rewritten;
                        result.changed = true;
                    }
                }
            }
            auto& allocation = function.blocks.front().operations[candidate.allocation_index];
            allocation.opcode.clear();
            allocation.result.clear();
            allocation.operands.clear();
            allocation.successors.clear();
            allocation.successor_arguments.clear();
            allocation.type = ir::Type(ir::TypeKind::void_);
            ++result.operations_removed;
        }
        for (auto& block : function.blocks)
            block.operations.erase(std::remove_if(block.operations.begin(), block.operations.end(),
                [](const ir::Operation& operation) { return operation.opcode.empty(); }), block.operations.end());
    }

    struct Candidate {
        std::size_t block_index{};
        std::size_t allocation_index{};
        ir::Type stored_type;
        bool has_type{};
        bool valid{true};
    };

    std::unordered_map<std::string, Candidate> candidates;
    for (std::size_t local_block_index = 0; local_block_index < function.blocks.size(); ++local_block_index) {
        const auto& block = function.blocks[local_block_index];
        for (std::size_t operation_index = 0; operation_index < block.operations.size(); ++operation_index) {
            const auto& operation = block.operations[operation_index];
            if (operation.opcode == "stack.alloc" && !operation.result.empty())
                candidates.emplace(operation.result, Candidate{local_block_index, operation_index, ir::Type(ir::TypeKind::void_), false, true});
        }
    }

    if (candidates.empty()) return result;

    for (std::size_t local_block_index = 0; local_block_index < function.blocks.size(); ++local_block_index) {
        const auto& block = function.blocks[local_block_index];
        for (const auto& operation : block.operations) {
            for (std::size_t operand_index = 0; operand_index < operation.operands.size(); ++operand_index) {
                const auto candidate_it = candidates.find(operation.operands[operand_index]);
                if (candidate_it == candidates.end()) continue;
                auto& candidate = candidate_it->second;
                const bool scalar_load = operation.opcode == "load" && operand_index == 0U && operation.operands.size() == 1U;
                const bool scalar_store = operation.opcode == "store" && operand_index == 1U && operation.operands.size() == 2U;
                if (local_block_index != candidate.block_index || (!scalar_load && !scalar_store)) {
                    candidate.valid = false;
                    continue;
                }
                if (!candidate.has_type) {
                    candidate.stored_type = operation.type;
                    candidate.has_type = true;
                } else if (candidate.stored_type != operation.type) {
                    candidate.valid = false;
                }
            }
            for (const auto& argument_list : operation.successor_arguments) {
                for (const auto& argument : argument_list) {
                    const auto candidate_it = candidates.find(argument);
                    if (candidate_it != candidates.end()) candidate_it->second.valid = false;
                }
            }
        }
    }

    for (const auto& [name, candidate] : candidates) {
        if (!candidate.valid || !candidate.has_type) continue;
        auto& operations = function.blocks[candidate.block_index].operations;
        std::string current_value;
        bool initialized = false;
        bool safe = true;
        for (std::size_t index = candidate.allocation_index + 1U; index < operations.size(); ++index) {
            const auto& operation = operations[index];
            if (operation.opcode == "store" && operation.operands.size() == 2U && operation.operands[1] == name) {
                current_value = operation.operands[0];
                initialized = true;
            } else if (operation.opcode == "load" && operation.operands.size() == 1U && operation.operands[0] == name) {
                if (!initialized) { safe = false; break; }
            }
        }
        if (!safe) continue;

        current_value.clear();
        for (auto& operation : operations) {
            if (operation.opcode == "store" && operation.operands.size() == 2U && operation.operands[1] == name) {
                current_value = operation.operands[0];
                operation.opcode = "copy";
                operation.result.clear();
                operation.operands.clear();
                operation.type = ir::Type(ir::TypeKind::void_);
                ++result.operations_removed;
                result.changed = true;
            } else if (operation.opcode == "load" && operation.operands.size() == 1U && operation.operands[0] == name) {
                operation.opcode = "copy";
                operation.operands = {current_value};
                operation.alignment = 0;
                ++result.operations_rewritten;
                result.changed = true;
            }
        }
        auto& allocation = operations[candidate.allocation_index];
        allocation.opcode = "copy";
        allocation.result.clear();
        allocation.operands.clear();
        allocation.type = ir::Type(ir::TypeKind::void_);
        ++result.operations_removed;
    }

    for (auto& block : function.blocks) {
        block.operations.erase(std::remove_if(block.operations.begin(), block.operations.end(),
            [](const ir::Operation& operation) {
                return operation.opcode == "copy" && operation.result.empty() && operation.operands.empty();
            }), block.operations.end());
    }
    return result;
}

pass::PassResult MemoryForwardingPass::run(ir::Function& function,
                                            analysis::FunctionAnalysisManager& analyses) {
    struct AvailableMemoryValue {
        analysis::MemoryLocation location;
        ir::Type type;
        std::string value;
    };
    using MemoryState = std::vector<AvailableMemoryValue>;

    pass::PassResult result;
    result.invalidation = analysis::InvalidationScope::operations;
    const auto& aliases = analyses.aliases();
    const auto& cfg = analyses.cfg();
    const auto& dominators = analyses.dominators();
    const auto size_of = [](ir::Type type) -> std::uint32_t {
        switch (type.kind()) {
        case ir::TypeKind::i1: case ir::TypeKind::i8: return 1U;
        case ir::TypeKind::i16: return 2U;
        case ir::TypeKind::i32: case ir::TypeKind::f32: return 4U;
        case ir::TypeKind::i64: case ir::TypeKind::f64: case ir::TypeKind::ptr: return 8U;
        case ir::TypeKind::void_: return 0U;
        }
        return 0U;
    };
    const auto is_memory_barrier = [](const ir::Operation& operation) {
        return operation.opcode == "call" || operation.opcode == "call.indirect" ||
               operation.opcode == "memory.copy" || operation.opcode == "memory.set" ||
               operation.opcode.starts_with("aggregate.");
    };
    const auto erase_may_aliases = [&](MemoryState& state,
                                       const analysis::MemoryLocation& location) {
        state.erase(std::remove_if(state.begin(), state.end(),
            [&](const AvailableMemoryValue& item) {
                return aliases.alias(item.location, location) != analysis::AliasResult::no_alias;
            }), state.end());
    };
    const auto find_available = [&](const MemoryState& state,
                                    const analysis::MemoryLocation& location,
                                    ir::Type type) -> const AvailableMemoryValue* {
        const auto found = std::find_if(state.rbegin(), state.rend(),
            [&](const AvailableMemoryValue& item) {
                return item.type == type &&
                       aliases.alias(item.location, location) == analysis::AliasResult::must_alias;
            });
        return found == state.rend() ? nullptr : &*found;
    };
    const auto add_available = [&](MemoryState& state, AvailableMemoryValue value) {
        erase_may_aliases(state, value.location);
        state.push_back(std::move(value));
    };
    // Dataflow convergence compares state descriptors, not alias certainty.
    // Two identical imprecise locations are the same abstract state even though
    // AliasAnalysis correctly reports may_alias for optimization queries. Using
    // must_alias here made any imprecise stack-derived address keep the fixpoint
    // loop alive forever. Predecessor intersection below remains conservative.
    const auto same_location = [](const analysis::MemoryLocation& left,
                                  const analysis::MemoryLocation& right) {
        return left.origin == right.origin && left.base == right.base &&
               left.offset == right.offset && left.size == right.size &&
               left.precise == right.precise;
    };
    const auto same_state = [&](const MemoryState& left, const MemoryState& right) {
        if (left.size() != right.size()) return false;
        return std::all_of(left.begin(), left.end(), [&](const AvailableMemoryValue& item) {
            return std::any_of(right.begin(), right.end(), [&](const AvailableMemoryValue& other) {
                return item.type == other.type && item.value == other.value &&
                       same_location(item.location, other.location);
            });
        });
    };
    const auto intersect_predecessors = [&](const std::string& block_name,
                                            const std::unordered_map<std::string, MemoryState>& out_states) {
        MemoryState intersection;
        const auto predecessors_it = cfg.predecessors.find(block_name);
        if (predecessors_it == cfg.predecessors.end() || predecessors_it->second.empty()) return intersection;
        bool first = true;
        for (const auto& predecessor : predecessors_it->second) {
            const auto state_it = out_states.find(predecessor);
            const MemoryState empty;
            const auto& state = state_it == out_states.end() ? empty : state_it->second;
            if (first) {
                intersection = state;
                first = false;
                continue;
            }
            intersection.erase(std::remove_if(intersection.begin(), intersection.end(),
                [&](const AvailableMemoryValue& item) {
                    return !std::any_of(state.begin(), state.end(), [&](const AvailableMemoryValue& other) {
                        return item.type == other.type && item.value == other.value &&
                               aliases.alias(item.location, other.location) == analysis::AliasResult::must_alias;
                    });
                }), intersection.end());
        }
        return intersection;
    };
    const auto transfer = [&](const ir::Block& block, MemoryState state) {
        for (const auto& operation : block.operations) {
            if (operation.opcode == "load" && operation.operands.size() == 1U && !operation.result.empty()) {
                const auto location = aliases.location(operation.operands.front(), size_of(operation.type));
                if (find_available(state, location, operation.type) == nullptr)
                    state.push_back({location, operation.type, operation.result});
                continue;
            }
            if (operation.opcode == "store" && operation.operands.size() == 2U) {
                const auto location = aliases.location(operation.operands[1], size_of(operation.type));
                add_available(state, {location, operation.type, operation.operands.front()});
                continue;
            }
            if (is_memory_barrier(operation)) state.clear();
        }
        return state;
    };

    std::unordered_map<std::string, MemoryState> in_states;
    std::unordered_map<std::string, MemoryState> out_states;
    for (const auto& block : function.blocks) {
        in_states.try_emplace(block.name);
        out_states.try_emplace(block.name);
    }

    bool changed = true;
    while (changed) {
        changed = false;
        for (const auto& block : function.blocks) {
            const auto next_in = intersect_predecessors(block.name, out_states);
            const auto next_out = transfer(block, next_in);
            if (!same_state(in_states[block.name], next_in)) {
                in_states[block.name] = next_in;
                changed = true;
            }
            if (!same_state(out_states[block.name], next_out)) {
                out_states[block.name] = next_out;
                changed = true;
            }
        }
    }

    std::unordered_map<std::string, std::string> definition_blocks;
    for (const auto& parameter : function.parameters)
        definition_blocks.emplace(parameter.name, function.blocks.empty() ? std::string{} : function.blocks.front().name);
    for (const auto& block : function.blocks) {
        for (const auto& parameter : block.parameters) definition_blocks.emplace(parameter.name, block.name);
        for (const auto& operation : block.operations)
            if (!operation.result.empty()) definition_blocks.emplace(operation.result, block.name);
    }

    for (auto& block : function.blocks) {
        auto available = in_states[block.name];
        for (auto& operation : block.operations) {
            if (operation.opcode == "load" && operation.operands.size() == 1U && !operation.result.empty()) {
                const auto location = aliases.location(operation.operands.front(), size_of(operation.type));
                const auto* match = find_available(available, location, operation.type);
                if (match != nullptr) {
                    const auto definition = definition_blocks.find(match->value);
                    const bool dominates = definition == definition_blocks.end() ||
                                           definition->second == block.name ||
                                           dominators.dominates(definition->second, block.name);
                    if (dominates && match->value != operation.result) {
                        operation.opcode = "copy";
                        operation.operands = {match->value};
                        operation.alignment = 0;
                        ++result.operations_rewritten;
                        result.changed = true;
                        continue;
                    }
                }
                available.push_back({location, operation.type, operation.result});
                continue;
            }
            if (operation.opcode == "store" && operation.operands.size() == 2U) {
                const auto location = aliases.location(operation.operands[1], size_of(operation.type));
                add_available(available, {location, operation.type, operation.operands.front()});
                continue;
            }
            if (is_memory_barrier(operation)) available.clear();
        }
    }
    return result;
}

pass::PassResult DeadStoreEliminationPass::run(
    ir::Function& function, analysis::FunctionAnalysisManager& analyses) {
    using LocationSet = std::vector<analysis::MemoryLocation>;
    pass::PassResult result;
    result.invalidation = analysis::InvalidationScope::operations;
    const auto& aliases = analyses.aliases();
    const auto& cfg = analyses.cfg();
    const auto size_of = [](ir::Type type) -> std::uint32_t {
        switch (type.kind()) {
        case ir::TypeKind::i1: case ir::TypeKind::i8: return 1U;
        case ir::TypeKind::i16: return 2U;
        case ir::TypeKind::i32: case ir::TypeKind::f32: return 4U;
        case ir::TypeKind::i64: case ir::TypeKind::f64: case ir::TypeKind::ptr: return 8U;
        case ir::TypeKind::void_: return 0U;
        }
        return 0U;
    };
    const auto is_memory_barrier = [](const ir::Operation& operation) {
        return operation.opcode == "call" || operation.opcode == "call.indirect" ||
               operation.opcode == "memory.copy" || operation.opcode == "memory.set" ||
               operation.opcode.starts_with("aggregate.");
    };
    const auto contains_must_alias = [&](const LocationSet& locations,
                                         const analysis::MemoryLocation& candidate) {
        return std::any_of(locations.begin(), locations.end(),
            [&](const analysis::MemoryLocation& location) {
                return aliases.alias(location, candidate) == analysis::AliasResult::must_alias;
            });
    };
    const auto remove_may_aliases = [&](LocationSet& locations,
                                        const analysis::MemoryLocation& candidate) {
        locations.erase(std::remove_if(locations.begin(), locations.end(),
            [&](const analysis::MemoryLocation& location) {
                return aliases.alias(location, candidate) != analysis::AliasResult::no_alias;
            }), locations.end());
    };
    const auto add_location = [&](LocationSet& locations,
                                  const analysis::MemoryLocation& candidate) {
        if (!contains_must_alias(locations, candidate)) locations.push_back(candidate);
    };
    // As in memory forwarding, convergence compares the abstract location
    // descriptors themselves. An imprecise location is still identical to the
    // same imprecise location on the next dataflow iteration even though it is
    // conservatively may-alias for optimization decisions.
    const auto same_location = [](const analysis::MemoryLocation& left,
                                  const analysis::MemoryLocation& right) {
        return left.origin == right.origin && left.base == right.base &&
               left.offset == right.offset && left.size == right.size &&
               left.precise == right.precise;
    };
    const auto same_set = [&](const LocationSet& left, const LocationSet& right) {
        if (left.size() != right.size()) return false;
        return std::all_of(left.begin(), left.end(), [&](const analysis::MemoryLocation& location) {
            return std::any_of(right.begin(), right.end(), [&](const analysis::MemoryLocation& other) {
                return same_location(location, other);
            });
        });
    };
    const auto intersect_successors = [&](const std::string& block_name,
                                          const std::unordered_map<std::string, LocationSet>& in_sets) {
        LocationSet intersection;
        const auto successors_it = cfg.successors.find(block_name);
        if (successors_it == cfg.successors.end() || successors_it->second.empty()) return intersection;
        bool first = true;
        for (const auto& successor : successors_it->second) {
            const auto state_it = in_sets.find(successor);
            const LocationSet empty;
            const auto& state = state_it == in_sets.end() ? empty : state_it->second;
            if (first) {
                intersection = state;
                first = false;
                continue;
            }
            intersection.erase(std::remove_if(intersection.begin(), intersection.end(),
                [&](const analysis::MemoryLocation& location) {
                    return !contains_must_alias(state, location);
                }), intersection.end());
        }
        return intersection;
    };
    const auto transfer = [&](const ir::Block& block, LocationSet state) {
        for (std::size_t reverse = block.operations.size(); reverse > 0U; --reverse) {
            const auto& operation = block.operations[reverse - 1U];
            if (operation.opcode == "load" && operation.operands.size() == 1U) {
                remove_may_aliases(state,
                    aliases.location(operation.operands.front(), size_of(operation.type)));
                continue;
            }
            if (operation.opcode == "store" && operation.operands.size() == 2U) {
                const auto location = aliases.location(operation.operands[1], size_of(operation.type));
                remove_may_aliases(state, location);
                add_location(state, location);
                continue;
            }
            if (is_memory_barrier(operation)) state.clear();
        }
        return state;
    };

    std::unordered_map<std::string, LocationSet> in_sets;
    std::unordered_map<std::string, LocationSet> out_sets;
    for (const auto& block : function.blocks) {
        in_sets.try_emplace(block.name);
        out_sets.try_emplace(block.name);
    }

    bool dataflow_changed = true;
    while (dataflow_changed) {
        dataflow_changed = false;
        for (auto block_it = function.blocks.rbegin(); block_it != function.blocks.rend(); ++block_it) {
            const auto next_out = intersect_successors(block_it->name, in_sets);
            const auto next_in = transfer(*block_it, next_out);
            if (!same_set(out_sets[block_it->name], next_out)) {
                out_sets[block_it->name] = next_out;
                dataflow_changed = true;
            }
            if (!same_set(in_sets[block_it->name], next_in)) {
                in_sets[block_it->name] = next_in;
                dataflow_changed = true;
            }
        }
    }

    for (auto& block : function.blocks) {
        auto overwritten = out_sets[block.name];
        std::vector<bool> remove(block.operations.size(), false);
        bool block_changed = false;
        for (std::size_t reverse = block.operations.size(); reverse > 0U; --reverse) {
            const auto index = reverse - 1U;
            const auto& operation = block.operations[index];
            if (operation.opcode == "load" && operation.operands.size() == 1U) {
                remove_may_aliases(overwritten,
                    aliases.location(operation.operands.front(), size_of(operation.type)));
                continue;
            }
            if (operation.opcode == "store" && operation.operands.size() == 2U) {
                const auto location = aliases.location(operation.operands[1], size_of(operation.type));
                if (contains_must_alias(overwritten, location)) {
                    remove[index] = true;
                    block_changed = true;
                    ++result.operations_removed;
                    result.changed = true;
                    continue;
                }
                remove_may_aliases(overwritten, location);
                add_location(overwritten, location);
                continue;
            }
            if (is_memory_barrier(operation)) overwritten.clear();
        }
        if (!block_changed) continue;
        std::size_t index = 0U;
        block.operations.erase(std::remove_if(block.operations.begin(), block.operations.end(),
            [&](const ir::Operation&) { return remove[index++]; }), block.operations.end());
    }
    return result;
}

pass::PassResult LoopInvariantCodeMotionPass::run(ir::Function& function,
                                                   analysis::FunctionAnalysisManager& analyses) {
    pass::PassResult result;
    result.invalidation = analysis::InvalidationScope::operations;
    const auto loop_info = analyses.loops();
    if (loop_info.loops.empty()) return result;

    const auto is_hoistable = [](const ir::Operation& operation) {
        if (operation.is_terminator()) return false;
        static const std::unordered_set<std::string> pure_nontrapping{
            "const", "copy", "add", "sub", "mul", "and", "or", "xor", "shl",
            "shr.signed", "shr.unsigned", "neg", "not", "cmp.eq", "cmp.ne", "cmp.lt",
            "cmp.le", "cmp.gt", "cmp.ge", "cmp.ult", "cmp.ule", "cmp.ugt", "cmp.uge",
            "truncate", "zero_extend", "sign_extend", "bitcast", "int_to_float.signed", "int_to_float.unsigned", "float_to_int.signed", "float_to_int.unsigned", "float_extend", "float_truncate", "func.address", "global.address",
            "callback.address", "ptr.offset", "field.address"};
        return !operation.result.empty() && pure_nontrapping.contains(operation.opcode);
    };

    for (const auto& loop : loop_info.loops) {
        if (loop.preheader.empty()) continue;
        const auto header_it = std::find_if(function.blocks.begin(), function.blocks.end(),
            [&](const ir::Block& block) { return block.name == loop.header; });
        const auto preheader_it = std::find_if(function.blocks.begin(), function.blocks.end(),
            [&](const ir::Block& block) { return block.name == loop.preheader; });
        if (header_it == function.blocks.end() || preheader_it == function.blocks.end() || preheader_it->operations.empty()) continue;
        auto& header = *header_it;
        auto& preheader = *preheader_it;
        if (preheader.operations.back().opcode != "jump" || preheader.operations.back().successors.size() != 1U ||
            preheader.operations.back().successors.front() != header.name) continue;

        std::unordered_set<std::string> loop_definitions;
        for (const auto& block : function.blocks) {
            if (!loop.blocks.contains(block.name)) continue;
            for (const auto& parameter : block.parameters) loop_definitions.insert(parameter.name);
            for (const auto& operation : block.operations)
                if (!operation.result.empty()) loop_definitions.insert(operation.result);
        }
        std::unordered_set<std::string> invariant;
        std::vector<std::size_t> hoisted_indices;
        for (std::size_t operation_index = 0; operation_index < header.operations.size(); ++operation_index) {
            const auto& operation = header.operations[operation_index];
            bool operands_invariant = true;
            for (const auto& operand : operation.operands) {
                if (!operand.starts_with('%')) continue;
                if (loop_definitions.contains(operand) && !invariant.contains(operand)) {
                    operands_invariant = false;
                    break;
                }
            }
            if (is_hoistable(operation) && operands_invariant) {
                invariant.insert(operation.result);
                hoisted_indices.push_back(operation_index);
            }
        }
        if (hoisted_indices.empty()) continue;

        // Copy selected operations before mutating either block. Keeping the
        // terminator in place avoids destructive move-based reconstruction of
        // the header and makes repeated/nested LICM runs structurally safe.
        std::vector<ir::Operation> hoisted;
        hoisted.reserve(hoisted_indices.size());
        for (const auto operation_index : hoisted_indices)
            hoisted.push_back(header.operations[operation_index]);
        for (auto index = hoisted_indices.rbegin(); index != hoisted_indices.rend(); ++index)
            header.operations.erase(header.operations.begin() + static_cast<std::ptrdiff_t>(*index));

        const auto insertion = preheader.operations.end() - 1;
        preheader.operations.insert(insertion, hoisted.begin(), hoisted.end());
        result.changed = true;
        result.operations_rewritten += hoisted.size();
    }
    return result;
}

pass::PassResult DeadCodeEliminationPass::run(ir::Function& function,
                                              analysis::FunctionAnalysisManager&) {
    pass::PassResult result;
    result.invalidation = analysis::InvalidationScope::operations;
    struct Location { std::size_t block{}; std::size_t operation{}; };

    // SSA dead-code elimination is a reachability problem, not a fixed-point
    // rescan problem.  The old implementation rebuilt use-def information and
    // rescanned the entire function once per dead-definition layer.  Compiler
    // generated IR contains long SSA chains, making that approach quadratic.
    // Build the definition table once, seed side-effecting/terminator roots,
    // then walk operand dependencies backwards exactly once.
    std::unordered_map<std::string, Location> definitions;
    std::vector<std::vector<bool>> live(function.blocks.size());
    std::vector<Location> worklist;

    for (std::size_t bi = 0; bi < function.blocks.size(); ++bi) {
        const auto& block = function.blocks[bi];
        live[bi].assign(block.operations.size(), false);
        for (std::size_t oi = 0; oi < block.operations.size(); ++oi) {
            const auto& operation = block.operations[oi];
            if (!operation.result.empty()) definitions.emplace(operation.result, Location{bi, oi});
        }
    }

    const auto mark = [&](Location location) {
        if (location.block >= live.size() || location.operation >= live[location.block].size() ||
            live[location.block][location.operation]) return;
        live[location.block][location.operation] = true;
        worklist.push_back(location);
    };

    for (std::size_t bi = 0; bi < function.blocks.size(); ++bi) {
        const auto& block = function.blocks[bi];
        for (std::size_t oi = 0; oi < block.operations.size(); ++oi) {
            const auto& operation = block.operations[oi];
            if (operation.result.empty() || operation.has_side_effects()) mark(Location{bi, oi});
        }
    }

    while (!worklist.empty()) {
        const auto location = worklist.back();
        worklist.pop_back();
        const auto& operation = function.blocks[location.block].operations[location.operation];
        const auto mark_value = [&](const std::string& value) {
            const auto found = definitions.find(value);
            if (found != definitions.end()) mark(found->second);
        };
        for (const auto& operand : operation.operands) mark_value(operand);
        for (const auto& arguments : operation.successor_arguments)
            for (const auto& argument : arguments) mark_value(argument);
    }

    for (std::size_t bi = 0; bi < function.blocks.size(); ++bi) {
        auto& operations = function.blocks[bi].operations;
        std::vector<ir::Operation> retained;
        retained.reserve(operations.size());
        const auto removed_before = result.operations_removed;
        for (std::size_t oi = 0; oi < operations.size(); ++oi) {
            if (live[bi][oi]) retained.push_back(std::move(operations[oi]));
            else ++result.operations_removed;
        }
        if (result.operations_removed != removed_before) result.touch_block(function.blocks[bi].name);
        operations = std::move(retained);
    }
    result.changed = result.operations_removed != 0;
    return result;
}

pass::PassResult IfConversionPass::run(ir::Function& function,
                                        analysis::FunctionAnalysisManager&) {
    pass::PassResult result;
    result.invalidation = analysis::InvalidationScope::control_flow;
    if (function.blocks.size() < 4U) return result;
    std::unordered_map<std::string, std::size_t> indices;
    std::unordered_map<std::string, std::size_t> predecessors;
    for (std::size_t i = 0; i < function.blocks.size(); ++i) indices.emplace(function.blocks[i].name, i);
    for (const auto& block : function.blocks) {
        if (block.operations.empty()) continue;
        const auto& term = block.operations.back();
        for (const auto& successor : term.successors) ++predecessors[successor];
    }
    const auto safe_opcode = [](const std::string& opcode) {
        return opcode == "const" || opcode == "copy" || opcode == "add" || opcode == "sub" ||
               opcode == "mul" || opcode == "and" || opcode == "or" || opcode == "xor" ||
               opcode == "shl" || opcode == "shr.signed" || opcode == "shr.unsigned" ||
               opcode == "neg" || opcode == "not" || opcode == "zero_extend" ||
               opcode == "sign_extend" || opcode == "truncate" || opcode.starts_with("cmp.");
    };
    std::unordered_set<std::string> remove;
    for (auto& branch_block : function.blocks) {
        if (branch_block.operations.empty()) continue;
        auto& branch = branch_block.operations.back();
        if (branch.opcode != "branch" || branch.operands.size() != 1U || branch.successors.size() != 2U ||
            branch.successor_arguments.size() != 2U) continue;
        const auto ti = indices.find(branch.successors[0]);
        const auto fi = indices.find(branch.successors[1]);
        if (ti == indices.end() || fi == indices.end() || ti->second == fi->second) continue;
        auto& true_block = function.blocks[ti->second];
        auto& false_block = function.blocks[fi->second];
        if (predecessors[true_block.name] != 1U || predecessors[false_block.name] != 1U ||
            true_block.operations.empty() || false_block.operations.empty()) continue;
        const auto& tjump = true_block.operations.back();
        const auto& fjump = false_block.operations.back();
        if (tjump.opcode != "jump" || fjump.opcode != "jump" || tjump.successors.size() != 1U ||
            fjump.successors.size() != 1U || tjump.successors[0] != fjump.successors[0] ||
            tjump.successor_arguments.size() != 1U || fjump.successor_arguments.size() != 1U) continue;
        const auto mi = indices.find(tjump.successors[0]);
        if (mi == indices.end()) continue;
        auto& merge = function.blocks[mi->second];
        if (tjump.successor_arguments[0].size() != merge.parameters.size() ||
            fjump.successor_arguments[0].size() != merge.parameters.size() ||
            branch.successor_arguments[0].size() != true_block.parameters.size() ||
            branch.successor_arguments[1].size() != false_block.parameters.size()) continue;
        bool safe = true;
        for (std::size_t i=0;i+1<true_block.operations.size();++i) safe = safe && safe_opcode(true_block.operations[i].opcode) && !true_block.operations[i].result.empty();
        for (std::size_t i=0;i+1<false_block.operations.size();++i) safe = safe && safe_opcode(false_block.operations[i].opcode) && !false_block.operations[i].result.empty();
        if (!safe || true_block.operations.size() > 5U || false_block.operations.size() > 5U) continue;
        std::unordered_map<std::string,std::string> tmap, fmap;
        for (std::size_t i=0;i<true_block.parameters.size();++i) tmap[true_block.parameters[i].name]=branch.successor_arguments[0][i];
        for (std::size_t i=0;i<false_block.parameters.size();++i) fmap[false_block.parameters[i].name]=branch.successor_arguments[1][i];
        std::vector<ir::Operation> converted;
        const auto clone_arm = [&](const ir::Block& arm, std::unordered_map<std::string,std::string>& map, const char* tag) {
            for (std::size_t oi=0; oi+1<arm.operations.size(); ++oi) {
                auto op = arm.operations[oi];
                for (auto& operand : op.operands) if (const auto it=map.find(operand); it!=map.end()) operand=it->second;
                const auto old=op.result;
                op.result = "%ifc." + branch_block.name + "." + tag + "." + std::to_string(oi);
                map[old]=op.result;
                converted.push_back(std::move(op));
            }
        };
        clone_arm(true_block,tmap,"t"); clone_arm(false_block,fmap,"f");
        ir::Operation jump; jump.opcode="jump"; jump.successors={merge.name}; jump.successor_arguments.resize(1);
        for (std::size_t ai=0; ai<merge.parameters.size(); ++ai) {
            auto tv=tjump.successor_arguments[0][ai]; auto fv=fjump.successor_arguments[0][ai];
            if (const auto it=tmap.find(tv);it!=tmap.end()) tv=it->second;
            if (const auto it=fmap.find(fv);it!=fmap.end()) fv=it->second;
            if (tv==fv) jump.successor_arguments[0].push_back(tv);
            else {
                ir::Operation sel; sel.result="%ifc." + branch_block.name + ".s." + std::to_string(ai);
                sel.opcode="select"; sel.type=merge.parameters[ai].type; sel.operands={branch.operands[0],tv,fv};
                jump.successor_arguments[0].push_back(sel.result); converted.push_back(std::move(sel));
            }
        }
        branch_block.operations.pop_back();
        branch_block.operations.insert(branch_block.operations.end(), converted.begin(), converted.end());
        branch_block.operations.push_back(std::move(jump));
        remove.insert(true_block.name); remove.insert(false_block.name);
        result.changed=true; result.operations_rewritten += converted.size()+1U;
    }

    if (result.changed) {
        function.blocks.erase(std::remove_if(function.blocks.begin(), function.blocks.end(), [&](const ir::Block& b){return remove.contains(b.name);}), function.blocks.end());
    }
    return result;
}

pass::PassResult MergeParameterSimplificationPass::run(
    ir::Function& function, analysis::FunctionAnalysisManager& analyses) {
    pass::PassResult result;
    result.invalidation = analysis::InvalidationScope::operations;
    if (function.blocks.empty()) return result;

    const auto& dominators = analyses.dominators();
    for (auto& block : function.blocks) {
        if (block.parameters.empty()) continue;

        struct IncomingEdge {
            ir::Operation* terminator{};
            std::size_t successor_index{};
        };
        std::vector<IncomingEdge> incoming;
        for (auto& predecessor : function.blocks) {
            if (predecessor.operations.empty()) continue;
            auto& terminator = predecessor.operations.back();
            for (std::size_t edge = 0; edge < terminator.successors.size(); ++edge) {
                if (terminator.successors[edge] == block.name &&
                    edge < terminator.successor_arguments.size()) {
                    incoming.push_back({&terminator, edge});
                }
            }
        }
        if (incoming.size() < 2U) continue;

        for (std::size_t parameter_index = block.parameters.size(); parameter_index-- > 0;) {
            const auto& parameter = block.parameters[parameter_index];
            std::optional<std::string> common;
            bool identical = true;
            for (const auto& edge : incoming) {
                auto& arguments = edge.terminator->successor_arguments[edge.successor_index];
                if (parameter_index >= arguments.size()) {
                    identical = false;
                    break;
                }
                if (!common) common = arguments[parameter_index];
                else if (*common != arguments[parameter_index]) {
                    identical = false;
                    break;
                }
            }
            if (!identical || !common || *common == parameter.name) continue;

            for (auto& dominated : function.blocks) {
                if (dominated.name != block.name &&
                    !dominators.dominates(block.name, dominated.name)) continue;
                for (auto& operation : dominated.operations) {
                    for (auto& operand : operation.operands)
                        if (operand == parameter.name) operand = *common;
                    for (auto& arguments : operation.successor_arguments)
                        for (auto& argument : arguments)
                            if (argument == parameter.name) argument = *common;
                }
            }
            for (const auto& edge : incoming) {
                auto& arguments = edge.terminator->successor_arguments[edge.successor_index];
                arguments.erase(arguments.begin() + static_cast<std::ptrdiff_t>(parameter_index));
            }
            block.parameters.erase(block.parameters.begin() + static_cast<std::ptrdiff_t>(parameter_index));
            result.changed = true;
            ++result.operations_rewritten;
        }
    }
    return result;
}

pass::PassResult SimplifyCFGPass::run(ir::Function& function,
                                      analysis::FunctionAnalysisManager& analyses) {
    pass::PassResult result;
    result.invalidation = analysis::InvalidationScope::control_flow;
    if (function.blocks.empty()) return result;
    const auto reachable = analyses.cfg().reachable;
    const auto before = function.blocks.size();
    function.blocks.erase(std::remove_if(function.blocks.begin(), function.blocks.end(),
        [&](const ir::Block& block) { return !reachable.contains(block.name); }),
        function.blocks.end());
    result.blocks_removed = before - function.blocks.size();
    result.changed = result.blocks_removed != 0;
    return result;
}

pass::PassResult AlgebraicSimplificationPass::run(ir::Function& function,
                                                   analysis::FunctionAnalysisManager& analyses) {
    struct ConstantDefinition {
        long long value{};
        std::string block;
        std::size_t operation_index{};
    };
    struct ValueDefinition {
        const ir::Operation* operation{};
        std::string block;
        std::size_t operation_index{};
    };
    pass::PassResult result;
    result.invalidation = analysis::InvalidationScope::operations;
    std::unordered_map<std::string, ConstantDefinition> constants;
    std::unordered_map<std::string, ValueDefinition> definitions;
    for (const auto& block : function.blocks) {
        for (std::size_t index = 0; index < block.operations.size(); ++index) {
            const auto& operation = block.operations[index];
            if (!operation.result.empty()) definitions[operation.result] = {&operation, block.name, index};
        }
    }
    const auto& dominators = analyses.dominators();
    for (auto& block : function.blocks) {
        const auto rewritten_before = result.operations_rewritten;
        for (std::size_t index = 0; index < block.operations.size(); ++index) {
            auto& operation = block.operations[index];
            if (operation.opcode == "const" && operation.operands.size() == 1 && !operation.result.empty()) {
                if (auto value = number(operation.operands.front()))
                    constants[operation.result] = {*value, block.name, index};
                continue;
            }
            if (operation.result.empty()) continue;
            const auto constant = [&](std::size_t operand_index) -> std::optional<long long> {
                if (operand_index >= operation.operands.size()) return {};
                const auto found = constants.find(operation.operands[operand_index]);
                if (found == constants.end()) return {};
                const auto& definition = found->second;
                const bool available = definition.block == block.name
                    ? definition.operation_index < index
                    : dominators.dominates(definition.block, block.name);
                return available ? std::optional<long long>{definition.value} : std::optional<long long>{};
            };
            const auto make_copy = [&](const std::string& source) {
                operation.opcode = "copy";
                operation.operands = {source};
                result.changed = true;
                ++result.operations_rewritten;
            };
            const auto make_zero = [&] {
                operation.opcode = "const";
                operation.operands = {"0"};
                constants[operation.result] = {0, block.name, index};
                result.changed = true;
                ++result.operations_rewritten;
            };
            if ((operation.opcode == "neg" || operation.opcode == "not") &&
                operation.operands.size() == 1U) {
                const auto definition = definitions.find(operation.operands.front());
                if (definition != definitions.end()) {
                    const auto& source = definition->second;
                    const bool available = source.block == block.name
                        ? source.operation_index < index
                        : dominators.dominates(source.block, block.name);
                    if (available && source.operation != nullptr &&
                        source.operation->opcode == operation.opcode &&
                        source.operation->operands.size() == 1U) {
                        make_copy(source.operation->operands.front());
                    }
                }
                continue;
            }
            if (operation.opcode == "select" && operation.operands.size() == 3) {
                const auto condition = constant(0);
                const auto true_value = constant(1);
                const auto false_value = constant(2);
                if (operation.operands[1] == operation.operands[2])
                    make_copy(operation.operands[1]);
                else if (condition)
                    make_copy(operation.operands[*condition != 0 ? 1U : 2U]);
                else if (operation.type.is_integer() && operation.type.kind() == ir::TypeKind::i1 &&
                         true_value == 1 && false_value == 0)
                    make_copy(operation.operands[0]);
                else if (operation.type.is_integer() && operation.type.kind() == ir::TypeKind::i1 &&
                         true_value == 0 && false_value == 1) {
                    operation.opcode = "not";
                    operation.operands = {operation.operands[0]};
                    result.changed = true;
                    ++result.operations_rewritten;
                }
                continue;
            }
            if (operation.operands.size() == 2) {
                const auto left = constant(0);
                const auto right = constant(1);
                const bool same_value = operation.operands[0] == operation.operands[1];
                if ((operation.opcode == "add" || operation.opcode == "or" || operation.opcode == "xor") && right == 0)
                    make_copy(operation.operands[0]);
                else if ((operation.opcode == "add" || operation.opcode == "or" || operation.opcode == "xor") && left == 0)
                    make_copy(operation.operands[1]);
                else if (operation.opcode == "sub" && right == 0)
                    make_copy(operation.operands[0]);
                else if ((operation.opcode == "sub" || operation.opcode == "xor") && same_value)
                    make_zero();
                else if ((operation.opcode == "and" || operation.opcode == "or") && same_value)
                    make_copy(operation.operands[0]);
                else if (operation.opcode == "and" && right == -1)
                    make_copy(operation.operands[0]);
                else if (operation.opcode == "and" && left == -1)
                    make_copy(operation.operands[1]);
                else if (operation.opcode == "or" && right == -1)
                    make_copy(operation.operands[1]);
                else if (operation.opcode == "or" && left == -1)
                    make_copy(operation.operands[0]);
                else if (operation.opcode == "xor" && right == -1 && operation.type.is_integer()) {
                    operation.opcode = "not";
                    operation.operands = {operation.operands[0]};
                    result.changed = true;
                    ++result.operations_rewritten;
                } else if (operation.opcode == "xor" && left == -1 && operation.type.is_integer()) {
                    operation.opcode = "not";
                    operation.operands = {operation.operands[1]};
                    result.changed = true;
                    ++result.operations_rewritten;
                }
                else if ((operation.opcode == "cmp.eq" || operation.opcode == "cmp.le" ||
                          operation.opcode == "cmp.ge" || operation.opcode == "cmp.ule" ||
                          operation.opcode == "cmp.uge") && same_value && operation.type.is_integer()) {
                    operation.opcode = "const";
                    operation.operands = {"1"};
                    constants[operation.result] = {1, block.name, index};
                    result.changed = true;
                    ++result.operations_rewritten;
                } else if ((operation.opcode == "cmp.ne" || operation.opcode == "cmp.lt" ||
                            operation.opcode == "cmp.gt" || operation.opcode == "cmp.ult" ||
                            operation.opcode == "cmp.ugt") && same_value && operation.type.is_integer()) {
                    make_zero();
                } else if (operation.opcode == "mul" && right == 1)
                    make_copy(operation.operands[0]);
                else if (operation.opcode == "mul" && left == 1)
                    make_copy(operation.operands[1]);
                else if (operation.opcode == "mul" && right == -1 && operation.type.is_integer()) {
                    operation.opcode = "neg";
                    operation.operands = {operation.operands[0]};
                    result.changed = true;
                    ++result.operations_rewritten;
                } else if (operation.opcode == "mul" && left == -1 && operation.type.is_integer()) {
                    operation.opcode = "neg";
                    operation.operands = {operation.operands[1]};
                    result.changed = true;
                    ++result.operations_rewritten;
                } else if ((operation.opcode == "div.signed" || operation.opcode == "div.unsigned") && right == 1)
                    make_copy(operation.operands[0]);
                else if ((operation.opcode == "rem.signed" || operation.opcode == "rem.unsigned") && right == 1)
                    make_zero();
                else if ((operation.opcode == "mul" || operation.opcode == "and") && (left == 0 || right == 0))
                    make_zero();
                else if ((operation.opcode == "shl" || operation.opcode == "shr.signed" || operation.opcode == "shr.unsigned") && right == 0)
                    make_copy(operation.operands[0]);
            }
        }
        if (result.operations_rewritten != rewritten_before) result.touch_block(block.name);
    }
    return result;
}

pass::PassResult CommonSubexpressionEliminationPass::run(ir::Function& function,
                                                          analysis::FunctionAnalysisManager& analyses) {
    struct Expression {
        std::string opcode;
        ir::Type type;
        std::vector<std::string> operands;
        std::string value;
        std::string block;
        std::size_t operation_index{};
    };
    pass::PassResult result;
    result.invalidation = analysis::InvalidationScope::operations;
    std::vector<Expression> available;
    const auto& dominators = analyses.dominators();
    const auto is_candidate = [](const ir::Operation& operation) {
        static const std::unordered_set<std::string> pure{
            "const", "add", "sub", "mul", "div", "div.signed", "div.unsigned",
            "rem.signed", "rem.unsigned", "and", "or", "xor", "shl", "shr.signed",
            "shr.unsigned", "neg", "not", "cmp.eq", "cmp.ne", "cmp.lt", "cmp.le",
            "cmp.gt", "cmp.ge", "cmp.ult", "cmp.ule", "cmp.ugt", "cmp.uge",
            "truncate", "zero_extend", "sign_extend", "bitcast", "int_to_float.signed", "int_to_float.unsigned", "float_to_int.signed", "float_to_int.unsigned", "float_extend", "float_truncate", "func.address", "callback.address",
            "global.address", "tls.address", "ptr.offset", "field.address", "select"};
        return !operation.result.empty() && pure.contains(operation.opcode);
    };
    for (auto& block : function.blocks) {
        const auto rewritten_before = result.operations_rewritten;
        for (std::size_t index = 0; index < block.operations.size(); ++index) {
            auto& operation = block.operations[index];
            if (!is_candidate(operation)) continue;
            auto canonical_opcode = operation.opcode;
            auto canonical_operands = operation.operands;
            const bool commutative_integer = operation.type.is_integer() && canonical_operands.size() == 2U &&
                (canonical_opcode == "add" || canonical_opcode == "mul" || canonical_opcode == "and" ||
                 canonical_opcode == "or" || canonical_opcode == "xor" || canonical_opcode == "cmp.eq" ||
                 canonical_opcode == "cmp.ne");
            if (commutative_integer && canonical_operands[1] < canonical_operands[0])
                std::swap(canonical_operands[0], canonical_operands[1]);
            if (canonical_operands.size() == 2U && canonical_operands[1] < canonical_operands[0]) {
                static const std::unordered_map<std::string, std::string> swapped_comparison{
                    {"cmp.lt", "cmp.gt"}, {"cmp.gt", "cmp.lt"},
                    {"cmp.le", "cmp.ge"}, {"cmp.ge", "cmp.le"},
                    {"cmp.ult", "cmp.ugt"}, {"cmp.ugt", "cmp.ult"},
                    {"cmp.ule", "cmp.uge"}, {"cmp.uge", "cmp.ule"}};
                if (const auto swapped = swapped_comparison.find(canonical_opcode);
                    swapped != swapped_comparison.end()) {
                    std::swap(canonical_operands[0], canonical_operands[1]);
                    canonical_opcode = swapped->second;
                }
            }
            const auto match = std::find_if(available.rbegin(), available.rend(), [&](const Expression& expression) {
                if (expression.opcode != canonical_opcode || expression.type != operation.type || expression.operands != canonical_operands)
                    return false;
                return expression.block == block.name ? expression.operation_index < index
                                                      : dominators.dominates(expression.block, block.name);
            });
            if (match != available.rend()) {
                const bool comparison = operation.opcode.starts_with("cmp.");
                operation.opcode = "copy";
                if (comparison) operation.type = ir::i1_type();
                operation.operands = {match->value};
                result.changed = true;
                ++result.operations_rewritten;
            } else {
                available.push_back({std::move(canonical_opcode), operation.type, std::move(canonical_operands),
                                     operation.result, block.name, index});
            }
        }
        if (result.operations_rewritten != rewritten_before) result.touch_block(block.name);
    }
    return result;
}

pass::PassResult SparseConditionalConstantPropagationPass::run(
    ir::Function& function, analysis::FunctionAnalysisManager& analyses) {
    pass::PassResult total;
    bool changed = true;
    unsigned iterations = 0;
    while (changed && iterations++ < 16) {
        changed = false;
        ConstantFoldPass fold;
        auto folded = fold.run(function, analyses);
        total += folded;
        changed = changed || folded.changed;
        if (folded.changed) analyses.invalidate(
            analysis::InvalidationScope::operations, folded.touched_blocks);
        BranchFoldPass branch;
        auto branched = branch.run(function, analyses);
        total += branched;
        changed = changed || branched.changed;
        if (branched.changed) analyses.invalidate_control_flow(branched.touched_blocks);
        SimplifyCFGPass cfg;
        auto simplified = cfg.run(function, analyses);
        total += simplified;
        changed = changed || simplified.changed;
        if (simplified.changed) analyses.invalidate_control_flow();
        CopyPropagationPass copies;
        auto propagated = copies.run(function, analyses);
        total += propagated;
        changed = changed || propagated.changed;
        if (propagated.changed) analyses.invalidate(
            analysis::InvalidationScope::operations, propagated.touched_blocks);
    }
    total.changed = total.changed || changed;
    return total;
}

pass::PassResult ScalarCleanupFixpointPass::run(
    ir::Function& function, analysis::FunctionAnalysisManager& analyses) {
    pass::PassResult total;
    if (max_iterations_ == 0) return total;

    SparseConditionalConstantPropagationPass sccp;
    AlgebraicSimplificationPass algebraic;
    CommonSubexpressionEliminationPass cse;
    CopyPropagationPass copies;
    DeadCodeEliminationPass dce;
    SimplifyCFGPass cfg;

    for (std::size_t iteration = 0; iteration < max_iterations_; ++iteration) {
        pass::PassResult round;
        auto run_pass = [&](pass::FunctionPass& pass) {
            auto result = pass.run(function, analyses);
            round += result;
            if (!result.changed) return;
            // Honor the pass's mutation contract. SCCP may change control flow,
            // while the surrounding scalar passes generally mutate operations
            // only; treating SCCP as operation-only retained stale CFG state.
            const auto scope = result.invalidation == analysis::InvalidationScope::none
                ? analysis::InvalidationScope::all : result.invalidation;
            analyses.invalidate(scope, result.touched_blocks);
        };
        run_pass(sccp);
        run_pass(algebraic);
        run_pass(cse);
        run_pass(copies);
        run_pass(dce);
        run_pass(cfg);
        total += round;
        if (!round.changed) break;
    }
    return total;
}

} // namespace forge::transforms

namespace forge::transforms {
namespace {
const ir::Block* find_block(const ir::Function& function, const std::string& name) {
    const auto it = std::find_if(function.blocks.begin(), function.blocks.end(),
        [&](const ir::Block& block) { return block.name == name; });
    return it == function.blocks.end() ? nullptr : &*it;
}

std::optional<long long> constant_value(const ir::Block& block, const std::string& result) {
    const auto it = std::find_if(block.operations.begin(), block.operations.end(), [&](const ir::Operation& op) {
        return op.opcode == "const" && op.result == result && op.operands.size() == 1U;
    });
    return it == block.operations.end() ? std::optional<long long>{} : number(it->operands.front());
}

struct AffineReduction {
    const ir::Block* entry{};
    const ir::Block* header{};
    const ir::Block* body{};
    const ir::Block* exit{};
    std::string limit;
    long long start{};
    long long initial{};
    long long step{};
    long long scale{1};
    long long bias{};
    bool ascending{};
};

std::optional<AffineReduction> match_affine_reduction(const ir::Function& function) {
    if (function.return_type.kind() != ir::TypeKind::i64 || function.parameters.size() != 1U ||
        function.parameters.front().type.kind() != ir::TypeKind::i64 || function.blocks.size() != 4U)
        return {};

    const auto* entry = find_block(function, "entry");
    const auto* header = find_block(function, "loop");
    const auto* body = find_block(function, "body");
    const auto* exit = find_block(function, "exit");
    if (!entry || !header || !body || !exit || header->parameters.size() != 2U ||
        body->parameters.size() != 2U || exit->parameters.size() != 1U || entry->operations.empty() ||
        header->operations.size() != 2U || body->operations.size() < 3U || exit->operations.size() != 1U)
        return {};

    const auto& entry_jump = entry->operations.back();
    if (entry_jump.opcode != "jump" || entry_jump.successors != std::vector<std::string>{header->name} ||
        entry_jump.successor_arguments.size() != 1U || entry_jump.successor_arguments.front().size() != 2U)
        return {};
    const auto start = constant_value(*entry, entry_jump.successor_arguments.front()[0]);
    const auto initial = constant_value(*entry, entry_jump.successor_arguments.front()[1]);
    if (!start || !initial) return {};

    const auto& index = header->parameters[0].name;
    const auto& total = header->parameters[1].name;
    const auto& compare = header->operations[0];
    const auto& branch = header->operations[1];
    const bool ascending = compare.opcode == "cmp.ge";
    const bool descending = compare.opcode == "cmp.le";
    if ((!ascending && !descending) || compare.operands != std::vector<std::string>{index, function.parameters.front().name} ||
        branch.opcode != "branch" || branch.operands != std::vector<std::string>{compare.result} ||
        branch.successors != std::vector<std::string>({exit->name, body->name}) ||
        branch.successor_arguments.size() != 2U ||
        branch.successor_arguments[0] != std::vector<std::string>{total} ||
        branch.successor_arguments[1] != std::vector<std::string>({index, total}))
        return {};

    const auto& current = body->parameters[0].name;
    const auto& running = body->parameters[1].name;
    const auto& backedge = body->operations.back();
    if (backedge.opcode != "jump" || backedge.successors != std::vector<std::string>{header->name} ||
        backedge.successor_arguments.size() != 1U || backedge.successor_arguments.front().size() != 2U ||
        exit->operations.front().opcode != "return" ||
        exit->operations.front().operands != std::vector<std::string>{exit->parameters.front().name})
        return {};

    const auto next_index_name = backedge.successor_arguments.front()[0];
    const auto next_total_name = backedge.successor_arguments.front()[1];
    const ir::Operation* index_update = nullptr;
    const ir::Operation* total_update = nullptr;
    for (const auto& operation : body->operations) {
        if (operation.result == next_index_name) index_update = &operation;
        if (operation.result == next_total_name) total_update = &operation;
    }
    if (!index_update || !total_update || index_update->operands.size() != 2U ||
        total_update->opcode != "add" || total_update->operands.size() != 2U ||
        total_update->operands.front() != running)
        return {};

    long long step{};
    if (index_update->operands.front() != current) return {};
    const auto raw_step = constant_value(*entry, index_update->operands[1]);
    if (!raw_step) return {};
    if (index_update->opcode == "add") step = *raw_step;
    else if (index_update->opcode == "sub") step = -*raw_step;
    else return {};
    if ((ascending && step <= 0) || (descending && step >= 0)) return {};

    // Recognize total += index, total += index + C, and total += index * C (+ C).
    std::string term = total_update->operands[1];
    long long scale = 1;
    long long bias = 0;
    if (term != current) {
        const ir::Operation* term_op = nullptr;
        for (const auto& operation : body->operations)
            if (operation.result == term) term_op = &operation;
        if (!term_op || term_op->operands.size() != 2U) return {};
        const auto lhs_constant = constant_value(*entry, term_op->operands[0]);
        const auto rhs_constant = constant_value(*entry, term_op->operands[1]);
        if (term_op->opcode == "add") {
            if (term_op->operands[0] == current && rhs_constant) bias = *rhs_constant;
            else if (term_op->operands[1] == current && lhs_constant) bias = *lhs_constant;
            else return {};
        } else if (term_op->opcode == "mul") {
            if (term_op->operands[0] == current && rhs_constant) scale = *rhs_constant;
            else if (term_op->operands[1] == current && lhs_constant) scale = *lhs_constant;
            else return {};
        } else return {};
    }

    return AffineReduction{entry, header, body, exit, function.parameters.front().name,
                           *start, *initial, step, scale, bias, ascending};
}

ir::Operation make_operation(std::string result_name, std::string opcode, ir::Type type,
                             std::vector<std::string> operands = {},
                             std::vector<std::string> successors = {},
                             std::vector<std::vector<std::string>> successor_arguments = {}) {
    ir::Operation operation;
    operation.result = std::move(result_name);
    operation.opcode = std::move(opcode);
    operation.type = type;
    operation.operands = std::move(operands);
    operation.successors = std::move(successors);
    operation.successor_arguments = std::move(successor_arguments);
    return operation;
}
}

pass::PassResult ConstantTripLoopUnrollPass::run(
    ir::Function& function, analysis::FunctionAnalysisManager&) {
    pass::PassResult result;
    result.invalidation = analysis::InvalidationScope::control_flow;
    if (function.blocks.size() < 4U) return result;

    std::unordered_map<std::string, std::size_t> block_index;
    std::unordered_map<std::string, long long> constants;
    for (std::size_t i = 0; i < function.blocks.size(); ++i) {
        block_index.emplace(function.blocks[i].name, i);
        for (const auto& operation : function.blocks[i].operations) {
            if (operation.opcode == "const" && operation.operands.size() == 1U &&
                !operation.result.empty()) {
                if (const auto value = number(operation.operands[0])) constants[operation.result] = *value;
            }
        }
    }

    std::unordered_map<std::string, std::size_t> predecessor_count;
    for (const auto& block : function.blocks) {
        if (block.operations.empty()) continue;
        for (const auto& successor : block.operations.back().successors) ++predecessor_count[successor];
    }

    for (auto& entry : function.blocks) {
        if (entry.operations.empty()) continue;
        auto& entry_jump = entry.operations.back();
        if (entry_jump.opcode != "jump" || entry_jump.successors.size() != 1U ||
            entry_jump.successor_arguments.size() != 1U) continue;
        const auto header_it = block_index.find(entry_jump.successors[0]);
        if (header_it == block_index.end()) continue;
        auto& header = function.blocks[header_it->second];
        if (header.parameters.empty() || header.operations.size() < 2U ||
            entry_jump.successor_arguments[0].size() != header.parameters.size()) continue;
        const auto& compare = header.operations[header.operations.size() - 2U];
        const auto& branch = header.operations.back();
        if (compare.opcode != "cmp.ge" || compare.operands.size() != 2U || compare.result.empty() ||
            branch.opcode != "branch" || branch.operands.size() != 1U ||
            branch.operands[0] != compare.result || branch.successors.size() != 2U ||
            branch.successor_arguments.size() != 2U) continue;
        if (compare.operands[0] != header.parameters[0].name) continue;
        const auto limit_it = constants.find(compare.operands[1]);
        const auto initial_it = constants.find(entry_jump.successor_arguments[0][0]);
        if (limit_it == constants.end() || initial_it == constants.end()) continue;

        const auto body_it = block_index.find(branch.successors[1]);
        const auto exit_it = block_index.find(branch.successors[0]);
        if (body_it == block_index.end() || exit_it == block_index.end()) continue;
        auto& body = function.blocks[body_it->second];
        auto& exit = function.blocks[exit_it->second];
        if (body.operations.empty() || body.parameters.size() != header.parameters.size() ||
            branch.successor_arguments[1].size() != body.parameters.size() ||
            branch.successor_arguments[0].size() != exit.parameters.size() ||
            predecessor_count[header.name] != 2U || predecessor_count[body.name] != 1U) continue;
        const auto& backedge = body.operations.back();
        if (backedge.opcode != "jump" || backedge.successors.size() != 1U ||
            backedge.successors[0] != header.name || backedge.successor_arguments.size() != 1U ||
            backedge.successor_arguments[0].size() != header.parameters.size()) continue;
        bool safe = true;
        for (std::size_t i = 0; i + 1U < body.operations.size(); ++i) {
            const auto& operation = body.operations[i];
            if (operation.has_side_effects() || operation.opcode == "div" || operation.opcode == "rem" ||
                operation.opcode == "load" || operation.opcode == "load.f32" || operation.opcode == "load.f64") {
                safe = false;
                break;
            }
        }
        if (!safe || body.operations.size() > 9U) continue;

        // Find a positive constant induction step in the first backedge argument.
        const auto next_name = backedge.successor_arguments[0][0];
        const ir::Operation* update = nullptr;
        for (const auto& operation : body.operations) if (operation.result == next_name) update = &operation;
        if (update == nullptr || update->opcode != "add" || update->operands.size() != 2U) continue;
        std::string step_name;
        if (update->operands[0] == body.parameters[0].name) step_name = update->operands[1];
        else if (update->operands[1] == body.parameters[0].name) step_name = update->operands[0];
        else continue;
        const auto step_it = constants.find(step_name);
        if (step_it == constants.end() || step_it->second <= 0) continue;
        const auto initial = initial_it->second;
        const auto limit = limit_it->second;
        const auto step = step_it->second;
        const auto trips = initial >= limit ? 0LL : (limit - initial + step - 1LL) / step;
        if (trips < 0 || trips > 8) continue;

        std::vector<std::string> current = entry_jump.successor_arguments[0];
        std::vector<ir::Operation> expanded;
        for (long long iteration = 0; iteration < trips; ++iteration) {
            std::unordered_map<std::string, std::string> values;
            for (std::size_t i = 0; i < body.parameters.size(); ++i) {
                auto incoming = branch.successor_arguments[1][i];
                for (std::size_t p = 0; p < header.parameters.size(); ++p)
                    if (incoming == header.parameters[p].name) incoming = current[p];
                values[body.parameters[i].name] = incoming;
            }
            for (std::size_t oi = 0; oi + 1U < body.operations.size(); ++oi) {
                auto operation = body.operations[oi];
                for (auto& operand : operation.operands)
                    if (const auto it = values.find(operand); it != values.end()) operand = it->second;
                const auto old_result = operation.result;
                if (!old_result.empty()) {
                    operation.result = "%unroll." + header.name + "." + std::to_string(iteration) + "." + std::to_string(oi);
                    values[old_result] = operation.result;
                }
                expanded.push_back(std::move(operation));
            }
            std::vector<std::string> next;
            next.reserve(current.size());
            for (auto value : backedge.successor_arguments[0]) {
                if (const auto it = values.find(value); it != values.end()) value = it->second;
                next.push_back(std::move(value));
            }
            current = std::move(next);
        }

        ir::Operation final_jump;
        final_jump.opcode = "jump";
        final_jump.successors = {exit.name};
        final_jump.successor_arguments.resize(1);
        for (auto value : branch.successor_arguments[0]) {
            for (std::size_t p = 0; p < header.parameters.size(); ++p)
                if (value == header.parameters[p].name) value = current[p];
            final_jump.successor_arguments[0].push_back(std::move(value));
        }
        entry.operations.pop_back();
        entry.operations.insert(entry.operations.end(), expanded.begin(), expanded.end());
        entry.operations.push_back(std::move(final_jump));
        const auto header_name = header.name;
        const auto body_name = body.name;
        function.blocks.erase(std::remove_if(function.blocks.begin(), function.blocks.end(),
            [&](const ir::Block& block) { return block.name == header_name || block.name == body_name; }),
            function.blocks.end());
        result.changed = true;
        result.operations_rewritten += expanded.size();
        result.blocks_removed += 2U;
        return result;
    }
    return result;
}

pass::PassResult LoopReductionPass::run(ir::Function& function,
                                        analysis::FunctionAnalysisManager&) {
    pass::PassResult result;
    result.invalidation = analysis::InvalidationScope::control_flow;
    const auto match = match_affine_reduction(function);
    if (!match) return result;

    const auto i64 = ir::i64_type();
    const auto c = [&](std::string name, long long value) {
        return make_operation(std::move(name), "const", i64, {std::to_string(value)});
    };
    const auto magnitude = match->ascending ? match->step : -match->step;
    const auto is_power_of_two = [](long long value) {
        return value > 0 && (static_cast<unsigned long long>(value) &
                             (static_cast<unsigned long long>(value) - 1ULL)) == 0ULL;
    };
    const auto log2_exact = [](long long value) {
        long long shift = 0;
        while (value > 1) { value >>= 1; ++shift; }
        return shift;
    };

    // Materialize only constants that survive specialization. This keeps the
    // common unit-stride reduction on a small, dedicated path instead of
    // routing it through the fully generalized formula.
    std::vector<ir::Operation> entry_operations;
    entry_operations.push_back(c("%zero", 0));
    if (match->start != 0) entry_operations.push_back(c("%start", match->start));
    if (match->initial != 0) entry_operations.push_back(c("%initial", match->initial));
    if (magnitude != 1) entry_operations.push_back(c("%magnitude", magnitude));
    if (match->step != 1) entry_operations.push_back(c("%step", match->step));
    if (match->scale != 1 && match->scale != 0) entry_operations.push_back(c("%scale", match->scale));
    if (match->bias != 0) entry_operations.push_back(c("%bias", match->bias));

    const std::string start_value = match->start == 0 ? "%zero" : "%start";
    const std::string initial_value = match->initial == 0 ? "%zero" : "%initial";
    entry_operations.push_back(make_operation(
        "%distance", "sub", i64,
        match->ascending ? std::vector<std::string>{match->limit, start_value}
                         : std::vector<std::string>{start_value, match->limit}));
    entry_operations.push_back(make_operation("%empty", "cmp.le", i64, {"%distance", "%zero"}));
    entry_operations.push_back(make_operation("", "branch", ir::void_type(), {"%empty"},
                                               {"exit", "count"}, {{initial_value}, {}}));
    ir::Block new_entry{"entry", {}, std::move(entry_operations)};

    std::vector<ir::Operation> count_operations;
    std::string trip_count = "%distance";
    if (magnitude != 1) {
        count_operations.push_back(c("%magnitude_adjust_one", 1));
        count_operations.push_back(make_operation("%magnitude_minus_one", "sub", i64,
                                                   {"%magnitude", "%magnitude_adjust_one"}));
        count_operations.push_back(make_operation("%rounded_distance", "add", i64,
                                                   {"%distance", "%magnitude_minus_one"}));
        if (is_power_of_two(magnitude)) {
            count_operations.push_back(c("%magnitude_shift", log2_exact(magnitude)));
            count_operations.push_back(make_operation("%trip_count", "shr.unsigned", i64,
                                                       {"%rounded_distance", "%magnitude_shift"}));
        } else {
            count_operations.push_back(make_operation("%trip_count", "div.signed", i64,
                                                       {"%rounded_distance", "%magnitude"}));
        }
        trip_count = "%trip_count";
    }

    count_operations.push_back(c("%count_decrement", 1));
    count_operations.push_back(make_operation("%count_minus_one", "sub", i64,
                                               {trip_count, "%count_decrement"}));

    std::string last_index;
    if (match->start == 0 && match->step == 1) {
        last_index = "%count_minus_one";
    } else {
        if (match->step == 1) {
            count_operations.push_back(make_operation("%last_index", "add", i64,
                                                       {start_value, "%count_minus_one"}));
        } else if (match->step == -1) {
            count_operations.push_back(make_operation("%last_index", "sub", i64,
                                                       {start_value, "%count_minus_one"}));
        } else {
            count_operations.push_back(make_operation("%last_delta", "mul", i64,
                                                       {"%count_minus_one", "%step"}));
            count_operations.push_back(make_operation("%last_index", "add", i64,
                                                       {start_value, "%last_delta"}));
        }
        last_index = "%last_index";
    }

    std::string endpoints = last_index;
    if (match->start != 0) {
        count_operations.push_back(make_operation("%endpoints", "add", i64,
                                                   {start_value, last_index}));
        endpoints = "%endpoints";
    }
    const bool specialize_unit_parity = magnitude == 1 && match->scale == 1 &&
                                        match->bias == 0 && match->initial == 0;
    std::optional<ir::Block> series_even;
    std::optional<ir::Block> series_odd;
    if (specialize_unit_parity) {
        // The common unit-stride leaf path uses one parity-selected product.
        // This keeps pressure within caller-saved registers and avoids both a
        // callee-saved prologue and the unused second multiplication.
        count_operations.push_back(c("%count_lowbit_mask", 1));
        count_operations.push_back(make_operation("%count_lowbit", "and", i64,
                                                   {trip_count, "%count_lowbit_mask"}));
        count_operations.push_back(make_operation("%count_is_odd", "cmp.ne", i64,
                                                   {"%count_lowbit", "%zero"}));
        count_operations.push_back(make_operation("", "branch", ir::void_type(),
                                                   {"%count_is_odd"},
                                                   {"series_odd", "series_even"}, {{}, {}}));

        std::vector<ir::Operation> even_operations;
        even_operations.push_back(c("%half_count_shift", 1));
        even_operations.push_back(make_operation("%half_count", "shr.unsigned", i64,
                                                  {trip_count, "%half_count_shift"}));
        even_operations.push_back(make_operation("%even_index_sum", "mul", i64,
                                                  {"%half_count", endpoints}));
        even_operations.push_back(make_operation("", "jump", ir::void_type(), {}, {"finish"},
                                                  {{"%even_index_sum", trip_count}}));
        series_even.emplace("series_even", std::vector<ir::ValueDecl>{},
                            std::move(even_operations));

        std::vector<ir::Operation> odd_operations;
        odd_operations.push_back(c("%half_endpoints_shift", 1));
        odd_operations.push_back(make_operation("%half_endpoints", "shr.signed", i64,
                                                 {endpoints, "%half_endpoints_shift"}));
        odd_operations.push_back(make_operation("%odd_index_sum", "mul", i64,
                                                 {trip_count, "%half_endpoints"}));
        odd_operations.push_back(make_operation("", "jump", ir::void_type(), {}, {"finish"},
                                                 {{"%odd_index_sum", trip_count}}));
        series_odd.emplace("series_odd", std::vector<ir::ValueDecl>{},
                           std::move(odd_operations));
    } else if ((match->step & 1LL) == 0) {
        // For an even induction stride, first + last is always even:
        //   last = first + (count - 1) * even_stride
        // Therefore the endpoint sum can always be halved before the single
        // multiplication. This removes all parity selection and its live
        // ranges from the important stride-2/4 closed forms.
        count_operations.push_back(c("%half_endpoints_shift", 1));
        count_operations.push_back(make_operation("%half_endpoints", "shr.signed", i64,
                                                   {endpoints, "%half_endpoints_shift"}));
        count_operations.push_back(make_operation("%computed_index_sum", "mul", i64,
                                                   {trip_count, "%half_endpoints"}));
        count_operations.push_back(make_operation("", "jump", ir::void_type(), {}, {"finish"},
                                                   {{"%computed_index_sum", trip_count}}));
    } else {
        // Odd non-unit strides retain the branchless identity.
        count_operations.push_back(c("%half_count_shift", 1));
        count_operations.push_back(make_operation("%half_count", "shr.unsigned", i64,
                                                   {trip_count, "%half_count_shift"}));
        count_operations.push_back(c("%count_lowbit_mask", 1));
        count_operations.push_back(make_operation("%count_lowbit", "and", i64,
                                                   {trip_count, "%count_lowbit_mask"}));
        count_operations.push_back(c("%half_endpoints_shift", 1));
        count_operations.push_back(make_operation("%half_endpoints", "shr.signed", i64,
                                                   {endpoints, "%half_endpoints_shift"}));
        count_operations.push_back(make_operation("%even_contribution", "mul", i64,
                                                   {"%half_count", endpoints}));
        // count_lowbit is exactly 0 or 1. Convert it to an all-zero or all-one
        // mask so the odd contribution is selected with cheap integer logic
        // instead of consuming a second multiplier and extending both inputs'
        // live ranges.
        count_operations.push_back(make_operation("%odd_mask", "neg", i64,
                                                   {"%count_lowbit"}));
        count_operations.push_back(make_operation("%odd_contribution", "and", i64,
                                                   {"%odd_mask", "%half_endpoints"}));
        count_operations.push_back(make_operation("%computed_index_sum", "add", i64,
                                                   {"%even_contribution", "%odd_contribution"}));
        count_operations.push_back(make_operation("", "jump", ir::void_type(), {}, {"finish"},
                                                   {{"%computed_index_sum", trip_count}}));
    }
    ir::Block count{"count", {}, std::move(count_operations)};

    std::vector<ir::Operation> finish_operations;
    std::string affine_value = "%index_sum";
    if (match->scale == 0) {
        affine_value = "%zero";
    } else if (match->scale != 1) {
        finish_operations.push_back(make_operation("%scaled_sum", "mul", i64,
                                                   {"%index_sum", "%scale"}));
        affine_value = "%scaled_sum";
    }

    if (match->bias != 0) {
        finish_operations.push_back(make_operation("%bias_sum", "mul", i64,
                                                   {"%count_value", "%bias"}));
        if (match->scale == 0) {
            affine_value = "%bias_sum";
        } else {
            finish_operations.push_back(make_operation("%affine_sum", "add", i64,
                                                       {affine_value, "%bias_sum"}));
            affine_value = "%affine_sum";
        }
    }

    std::string result_value = affine_value;
    if (match->initial != 0) {
        finish_operations.push_back(make_operation("%result", "add", i64,
                                                   {initial_value, affine_value}));
        result_value = "%result";
    }
    finish_operations.push_back(make_operation("", "jump", ir::void_type(), {}, {"exit"},
                                                {{result_value}}));
    ir::Block finish{"finish", {{"%index_sum", i64}, {"%count_value", i64}},
                     std::move(finish_operations)};
    ir::Block new_exit{"exit", {{"%result_value", i64}}, {
        make_operation("", "return", i64, {"%result_value"})
    }};

    function.blocks.clear();
    function.blocks.push_back(std::move(new_entry));
    function.blocks.push_back(std::move(count));
    if (series_odd) function.blocks.push_back(std::move(*series_odd));
    if (series_even) function.blocks.push_back(std::move(*series_even));
    function.blocks.push_back(std::move(finish));
    function.blocks.push_back(std::move(new_exit));
    result.changed = true;
    result.operations_rewritten = 1;
    return result;
}



} // namespace forge::transforms
