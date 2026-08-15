// Copyright 2026 Mario Vinciguerra
// SPDX-License-Identifier: Apache-2.0

#include "forge/analysis/function_analysis.hpp"

#include <algorithm>
#include <charconv>
#include <limits>
#include <queue>

namespace forge::analysis {
namespace {
void add_use(UseDefInfo& info, const std::string& value) {
    if (value.starts_with('%')) ++info.use_count[value];
}

std::optional<std::int64_t> parse_integer(std::string_view text) {
    std::int64_t value{};
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (error == std::errc{} && end == text.data() + text.size()) return value;
    return std::nullopt;
}

}

ControlFlowGraph build_cfg(const ir::Function& function) {
    ControlFlowGraph cfg;
    for (const auto& block : function.blocks) {
        auto& successors = cfg.successors[block.name];
        cfg.predecessors.try_emplace(block.name);
        if (!block.operations.empty()) {
            for (const auto& successor : block.operations.back().successors) {
                if (std::find(successors.begin(), successors.end(), successor) == successors.end())
                    successors.push_back(successor);
                cfg.predecessors[successor].push_back(block.name);
            }
        }
    }

    if (!function.blocks.empty()) {
        std::queue<std::string> pending;
        pending.push(function.blocks.front().name);
        cfg.reachable.insert(function.blocks.front().name);
        while (!pending.empty()) {
            auto block = std::move(pending.front());
            pending.pop();
            for (const auto& successor : cfg.successors[block]) {
                if (cfg.reachable.insert(successor).second) pending.push(successor);
            }
        }
    }
    return cfg;
}

UseDefInfo build_use_def(const ir::Function& function) {
    UseDefInfo info;
    for (const auto& parameter : function.parameters)
        info.definitions.emplace(parameter.name, UseDefInfo::Definition{"", 0, true});

    for (const auto& block : function.blocks) {
        for (const auto& parameter : block.parameters)
            info.definitions.emplace(parameter.name, UseDefInfo::Definition{block.name, 0, true});
        for (std::size_t index = 0; index < block.operations.size(); ++index) {
            const auto& operation = block.operations[index];
            if (!operation.result.empty())
                info.definitions.emplace(operation.result,
                                         UseDefInfo::Definition{block.name, index, false});
            for (const auto& operand : operation.operands) add_use(info, operand);
            for (const auto& arguments : operation.successor_arguments)
                for (const auto& argument : arguments) add_use(info, argument);
        }
    }
    return info;
}

DominatorTree build_dominator_tree(const ir::Function& function,
                                   const ControlFlowGraph& cfg) {
    DominatorTree tree;
    if (function.blocks.empty()) return tree;

    std::unordered_set<std::string> all_reachable = cfg.reachable;
    const auto& entry = function.blocks.front().name;
    for (const auto& block : function.blocks) {
        if (!cfg.reachable.contains(block.name)) continue;
        tree.dominators[block.name] = block.name == entry
            ? std::unordered_set<std::string>{entry}
            : all_reachable;
    }

    bool changed = true;
    while (changed) {
        changed = false;
        for (const auto& block : function.blocks) {
            if (block.name == entry || !cfg.reachable.contains(block.name)) continue;
            std::unordered_set<std::string> next;
            const auto pred_it = cfg.predecessors.find(block.name);
            bool first = true;
            if (pred_it != cfg.predecessors.end()) {
                for (const auto& predecessor : pred_it->second) {
                    if (!cfg.reachable.contains(predecessor)) continue;
                    if (first) {
                        next = tree.dominators[predecessor];
                        first = false;
                    } else {
                        std::unordered_set<std::string> intersection;
                        for (const auto& candidate : next)
                            if (tree.dominators[predecessor].contains(candidate))
                                intersection.insert(candidate);
                        next = std::move(intersection);
                    }
                }
            }
            next.insert(block.name);
            if (next != tree.dominators[block.name]) {
                tree.dominators[block.name] = std::move(next);
                changed = true;
            }
        }
    }
    return tree;
}

AliasAnalysis build_alias_analysis(const ir::Function& function) {
    AliasAnalysis analysis;
    for (const auto& parameter : function.parameters) {
        if (parameter.type == ir::ptr_type())
            analysis.pointers.emplace(parameter.name, MemoryLocation{PointerOriginKind::argument, parameter.name, 0, 0, true});
    }

    std::unordered_map<std::string, std::int64_t> constants;
    for (const auto& block : function.blocks) {
        for (const auto& parameter : block.parameters) {
            if (parameter.type == ir::ptr_type())
                analysis.pointers.emplace(parameter.name, MemoryLocation{PointerOriginKind::unknown, parameter.name, 0, 0, false});
        }
        for (const auto& operation : block.operations) {
            if (operation.opcode == "const" && !operation.result.empty() && operation.operands.size() == 1U) {
                if (const auto value = parse_integer(operation.operands.front())) constants[operation.result] = *value;
                continue;
            }
            if (operation.result.empty()) continue;
            if (operation.opcode == "stack.alloc" || operation.opcode == "stack.alloc.struct" ||
                operation.opcode == "stack.alloc.array") {
                analysis.pointers[operation.result] = {PointerOriginKind::stack, operation.result, 0, 0, true};
                continue;
            }
            if ((operation.opcode == "global.address" || operation.opcode == "tls.address") && operation.operands.size() == 1U) {
                analysis.pointers[operation.result] = {PointerOriginKind::global, operation.operands.front(), 0, 0, true};
                continue;
            }
            if ((operation.opcode == "copy" || operation.opcode == "bitcast") && operation.operands.size() == 1U) {
                const auto source = analysis.pointers.find(operation.operands.front());
                if (source != analysis.pointers.end()) analysis.pointers[operation.result] = source->second;
                continue;
            }
            if ((operation.opcode == "ptr.offset" || operation.opcode == "field.address") &&
                operation.operands.size() == 2U) {
                const auto base = analysis.pointers.find(operation.operands.front());
                if (base == analysis.pointers.end()) continue;
                std::optional<std::int64_t> offset = parse_integer(operation.operands[1]);
                if (!offset) {
                    const auto constant = constants.find(operation.operands[1]);
                    if (constant != constants.end()) offset = constant->second;
                }
                auto location = base->second;
                if (offset && location.precise) location.offset += *offset;
                else location.precise = false;
                analysis.pointers[operation.result] = std::move(location);
            }
        }
    }
    return analysis;
}

MemoryLocation AliasAnalysis::location(const std::string& value, std::uint32_t access_size) const {
    const auto found = pointers.find(value);
    if (found == pointers.end()) return {PointerOriginKind::unknown, value, 0, access_size, false};
    auto result = found->second;
    result.size = access_size;
    return result;
}

AliasResult AliasAnalysis::alias(const MemoryLocation& left, const MemoryLocation& right) const noexcept {
    if (left.origin == PointerOriginKind::unknown || right.origin == PointerOriginKind::unknown)
        return AliasResult::may_alias;
    if (left.origin == PointerOriginKind::stack || right.origin == PointerOriginKind::stack) {
        if (left.origin != right.origin || left.base != right.base) return AliasResult::no_alias;
    } else if (left.origin == PointerOriginKind::global && right.origin == PointerOriginKind::global) {
        if (left.base != right.base) return AliasResult::no_alias;
    } else if (left.origin != right.origin) {
        return AliasResult::may_alias;
    } else if (left.origin == PointerOriginKind::argument && left.base != right.base) {
        return AliasResult::may_alias;
    }

    if (!left.precise || !right.precise) return AliasResult::may_alias;
    if (left.offset == right.offset && left.size == right.size) return AliasResult::must_alias;
    if (left.size != 0U && right.size != 0U) {
        const auto left_end = left.offset + static_cast<std::int64_t>(left.size);
        const auto right_end = right.offset + static_cast<std::int64_t>(right.size);
        if (left_end <= right.offset || right_end <= left.offset) return AliasResult::no_alias;
    }
    return AliasResult::may_alias;
}

LoopInfo build_loop_info(const ir::Function& function, const ControlFlowGraph& cfg,
                         const DominatorTree& dominators) {
    LoopInfo info;
    for (const auto& block : function.blocks) {
        const auto successors = cfg.successors.find(block.name);
        if (successors == cfg.successors.end()) continue;
        for (const auto& header : successors->second) {
            if (!dominators.dominates(header, block.name)) continue;
            NaturalLoop loop;
            loop.header = header;
            loop.latch = block.name;
            loop.blocks.insert(header);
            loop.blocks.insert(block.name);
            std::vector<std::string> pending;
            if (block.name != header) pending.push_back(block.name);
            while (!pending.empty()) {
                const auto current = std::move(pending.back());
                pending.pop_back();
                const auto predecessors = cfg.predecessors.find(current);
                if (predecessors == cfg.predecessors.end()) continue;
                for (const auto& predecessor : predecessors->second) {
                    if (loop.blocks.insert(predecessor).second && predecessor != header)
                        pending.push_back(predecessor);
                }
            }
            const auto header_predecessors = cfg.predecessors.find(header);
            if (header_predecessors != cfg.predecessors.end()) {
                std::vector<std::string> outside;
                for (const auto& predecessor : header_predecessors->second)
                    if (!loop.blocks.contains(predecessor)) outside.push_back(predecessor);
                if (outside.size() == 1U) loop.preheader = outside.front();
            }
            info.loops.push_back(std::move(loop));
        }
    }
    return info;
}

bool DominatorTree::dominates(const std::string& candidate,
                              const std::string& block) const {
    const auto iterator = dominators.find(block);
    return iterator != dominators.end() && iterator->second.contains(candidate);
}

const ControlFlowGraph& FunctionAnalysisManager::cfg() {
    if (!has_cfg_) {
        cfg_ = build_cfg(*function_);
        has_cfg_ = true;
    }
    return cfg_;
}

const UseDefInfo& FunctionAnalysisManager::use_def() {
    if (!has_use_def_) {
        use_def_ = build_use_def(*function_);
        has_use_def_ = true;
    }
    return use_def_;
}

const DominatorTree& FunctionAnalysisManager::dominators() {
    if (!has_dominators_) {
        dominators_ = build_dominator_tree(*function_, cfg());
        has_dominators_ = true;
    }
    return dominators_;
}

const AliasAnalysis& FunctionAnalysisManager::aliases() {
    if (!has_aliases_) {
        aliases_ = build_alias_analysis(*function_);
        has_aliases_ = true;
    }
    return aliases_;
}

const LoopInfo& FunctionAnalysisManager::loops() {
    if (!has_loops_) {
        loops_ = build_loop_info(*function_, cfg(), dominators());
        has_loops_ = true;
    }
    return loops_;
}

void FunctionAnalysisManager::invalidate_all() {
    has_cfg_ = false;
    has_use_def_ = false;
    has_dominators_ = false;
    has_aliases_ = false;
    has_loops_ = false;
    cfg_ = {};
    use_def_ = {};
    dominators_ = {};
    aliases_ = {};
    loops_ = {};
}

} // namespace forge::analysis
