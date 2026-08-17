// Copyright 2026 Mario Vinciguerra
// SPDX-License-Identifier: Apache-2.0

#include "forge/analysis/function_analysis.hpp"

#include <algorithm>
#include <charconv>
#include <limits>
#include <queue>

namespace forge::analysis {
namespace {
std::optional<std::int64_t> parse_integer(std::string_view text) {
    std::int64_t value{};
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (error == std::errc{} && end == text.data() + text.size()) return value;
    return std::nullopt;
}

}

namespace {
struct CFGRepairResult {
    bool valid{};
    bool changed{};
    bool reachability_changed{};
    std::vector<std::string> affected_successors;
};

void recompute_reachable(const ir::Function& function, ControlFlowGraph& cfg) {
    cfg.reachable.clear();
    if (function.blocks.empty()) return;
    std::queue<std::string> pending;
    pending.push(function.blocks.front().name);
    cfg.reachable.insert(function.blocks.front().name);
    while (!pending.empty()) {
        auto block = std::move(pending.front());
        pending.pop();
        const auto successors = cfg.successors.find(block);
        if (successors == cfg.successors.end()) continue;
        for (const auto& successor : successors->second)
            if (cfg.reachable.insert(successor).second) pending.push(successor);
    }
}

CFGRepairResult repair_cfg_blocks(const ir::Function& function, ControlFlowGraph& cfg,
                                  const std::vector<std::string>& touched_blocks) {
    CFGRepairResult result;
    if (touched_blocks.empty() || cfg.successors.size() != function.blocks.size()) return result;

    std::unordered_map<std::string, const ir::Block*> blocks;
    blocks.reserve(function.blocks.size());
    for (const auto& block : function.blocks) {
        blocks.emplace(block.name, &block);
        if (!cfg.successors.contains(block.name) || !cfg.predecessors.contains(block.name)) return result;
    }

    const auto old_reachable = cfg.reachable;
    std::unordered_set<std::string> affected;
    for (const auto& block_name : touched_blocks) {
        const auto block_it = blocks.find(block_name);
        if (block_it == blocks.end()) return {};
        auto succ_it = cfg.successors.find(block_name);
        if (succ_it == cfg.successors.end()) return {};

        std::vector<std::string> next;
        const auto* block = block_it->second;
        if (!block->operations.empty()) {
            for (const auto& successor : block->operations.back().successors) {
                if (!blocks.contains(successor)) return {};
                if (std::find(next.begin(), next.end(), successor) == next.end()) next.push_back(successor);
            }
        }
        const auto previous = succ_it->second;
        if (previous == next) continue;
        result.changed = true;
        for (const auto& successor : previous) {
            affected.insert(successor);
            auto pred = cfg.predecessors.find(successor);
            if (pred == cfg.predecessors.end()) continue;
            auto& values = pred->second;
            values.erase(std::remove(values.begin(), values.end(), block_name), values.end());
        }
        succ_it->second = next;
        for (const auto& successor : next) {
            affected.insert(successor);
            auto& predecessors = cfg.predecessors[successor];
            if (std::find(predecessors.begin(), predecessors.end(), block_name) == predecessors.end())
                predecessors.push_back(block_name);
        }
    }

    recompute_reachable(function, cfg);
    result.reachability_changed = cfg.reachable != old_reachable;
    result.affected_successors.assign(affected.begin(), affected.end());
    result.valid = true;
    return result;
}

void repair_dominator_tree(const ir::Function& function, const ControlFlowGraph& cfg,
                           DominatorTree& tree, const std::vector<std::string>& seeds) {
    if (function.blocks.empty() || seeds.empty()) return;
    const auto& entry = function.blocks.front().name;
    std::queue<std::string> pending;
    std::unordered_set<std::string> queued;
    for (const auto& seed : seeds) {
        if (seed != entry && cfg.reachable.contains(seed) && queued.insert(seed).second) pending.push(seed);
    }
    while (!pending.empty()) {
        auto block = std::move(pending.front());
        pending.pop();
        queued.erase(block);
        std::unordered_set<std::string> next;
        bool first = true;
        const auto pred_it = cfg.predecessors.find(block);
        if (pred_it != cfg.predecessors.end()) {
            for (const auto& predecessor : pred_it->second) {
                if (!cfg.reachable.contains(predecessor)) continue;
                const auto dom_it = tree.dominators.find(predecessor);
                if (dom_it == tree.dominators.end()) continue;
                if (first) {
                    next = dom_it->second;
                    first = false;
                } else {
                    std::unordered_set<std::string> intersection;
                    for (const auto& candidate : next)
                        if (dom_it->second.contains(candidate)) intersection.insert(candidate);
                    next = std::move(intersection);
                }
            }
        }
        next.insert(block);
        auto& current = tree.dominators[block];
        if (current == next) continue;
        current = std::move(next);
        const auto succ_it = cfg.successors.find(block);
        if (succ_it == cfg.successors.end()) continue;
        for (const auto& successor : succ_it->second) {
            if (successor != entry && cfg.reachable.contains(successor) && queued.insert(successor).second)
                pending.push(successor);
        }
    }
}

void populate_cfg(const ir::Function& function, ControlFlowGraph& cfg) {
    // Keep bucket/vector storage alive across invalidation cycles. Scalar
    // fixpoints frequently dirty the CFG only to rebuild it a pass later; the
    // old assign-from-temporary path repeatedly discarded all hash storage.
    cfg.successors.clear();
    cfg.predecessors.clear();
    cfg.reachable.clear();
    cfg.successors.reserve(std::max(cfg.successors.bucket_count(), function.blocks.size()));
    cfg.predecessors.reserve(std::max(cfg.predecessors.bucket_count(), function.blocks.size()));
    cfg.reachable.reserve(std::max(cfg.reachable.bucket_count(), function.blocks.size()));

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
}

void add_use(UseDefInfo::BlockContribution& contribution, const std::string& value) {
    if (value.starts_with('%')) ++contribution.use_count[value];
}

UseDefInfo::BlockContribution scan_block_use_def(const ir::Block& block) {
    UseDefInfo::BlockContribution contribution;
    contribution.definitions.reserve(block.parameters.size() + block.operations.size());
    for (const auto& parameter : block.parameters) contribution.definitions.push_back(parameter.name);
    for (const auto& operation : block.operations) {
        if (!operation.result.empty()) contribution.definitions.push_back(operation.result);
        for (const auto& operand : operation.operands) add_use(contribution, operand);
        for (const auto& arguments : operation.successor_arguments)
            for (const auto& argument : arguments) add_use(contribution, argument);
    }
    return contribution;
}

void apply_block_use_def(const ir::Block& block, const UseDefInfo::BlockContribution& contribution,
                         UseDefInfo& info) {
    for (const auto& parameter : block.parameters)
        info.definitions[parameter.name] = UseDefInfo::Definition{block.name, 0, true};
    for (std::size_t index = 0; index < block.operations.size(); ++index) {
        const auto& operation = block.operations[index];
        if (!operation.result.empty())
            info.definitions[operation.result] = UseDefInfo::Definition{block.name, index, false};
    }
    for (const auto& [value, count] : contribution.use_count) info.use_count[value] += count;
}

void subtract_block_use_def(const std::string& block_name,
                            const UseDefInfo::BlockContribution& contribution, UseDefInfo& info) {
    for (const auto& value : contribution.definitions) {
        const auto definition = info.definitions.find(value);
        if (definition != info.definitions.end() && definition->second.block == block_name)
            info.definitions.erase(definition);
    }
    for (const auto& [value, count] : contribution.use_count) {
        const auto use = info.use_count.find(value);
        if (use == info.use_count.end()) continue;
        if (use->second <= count) info.use_count.erase(use);
        else use->second -= count;
    }
}

const ir::Block* find_block(const ir::Function& function, const std::string& name) {
    const auto found = std::find_if(function.blocks.begin(), function.blocks.end(),
                                    [&](const ir::Block& block) { return block.name == name; });
    return found == function.blocks.end() ? nullptr : &*found;
}

void populate_use_def(const ir::Function& function, UseDefInfo& info) {
    info.definitions.clear();
    info.use_count.clear();
    info.block_contributions.clear();
    std::size_t values = function.parameters.size();
    for (const auto& block : function.blocks)
        values += block.parameters.size() + block.operations.size();
    info.definitions.reserve(std::max(info.definitions.bucket_count(), values));
    info.use_count.reserve(std::max(info.use_count.bucket_count(), values));
    info.block_contributions.reserve(std::max(info.block_contributions.bucket_count(), function.blocks.size()));

    for (const auto& parameter : function.parameters)
        info.definitions.emplace(parameter.name, UseDefInfo::Definition{"", 0, true});

    for (const auto& block : function.blocks) {
        auto contribution = scan_block_use_def(block);
        apply_block_use_def(block, contribution, info);
        info.block_contributions.emplace(block.name, std::move(contribution));
    }
}

void repair_use_def_blocks(const ir::Function& function, UseDefInfo& info,
                           const std::vector<std::string>& touched_blocks) {
    for (const auto& block_name : touched_blocks) {
        if (const auto old = info.block_contributions.find(block_name);
            old != info.block_contributions.end()) {
            subtract_block_use_def(block_name, old->second, info);
            info.block_contributions.erase(old);
        }

        const auto* block = find_block(function, block_name);
        if (block == nullptr) continue;
        auto contribution = scan_block_use_def(*block);
        apply_block_use_def(*block, contribution, info);
        info.block_contributions.emplace(block_name, std::move(contribution));
    }
}
}

ControlFlowGraph build_cfg(const ir::Function& function) {
    ControlFlowGraph cfg;
    populate_cfg(function, cfg);
    return cfg;
}

UseDefInfo build_use_def(const ir::Function& function) {
    UseDefInfo info;
    populate_use_def(function, info);
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

namespace {
void add_alias_dependency(AliasAnalysis& analysis, const std::string& source,
                          const std::string& result) {
    if (!source.starts_with('%') || result.empty()) return;
    auto& values = analysis.dependents[source];
    if (std::find(values.begin(), values.end(), result) == values.end()) values.push_back(result);
}

void rebuild_alias_metadata(const ir::Function& function, AliasAnalysis& analysis) {
    analysis.dependents.clear();
    analysis.defining_blocks.clear();
    std::size_t definitions = function.parameters.size();
    for (const auto& block : function.blocks)
        definitions += block.parameters.size() + block.operations.size();
    analysis.defining_blocks.reserve(std::max(analysis.defining_blocks.bucket_count(), definitions));
    analysis.dependents.reserve(std::max(analysis.dependents.bucket_count(), definitions));

    for (const auto& parameter : function.parameters)
        analysis.defining_blocks[parameter.name] = "";
    for (const auto& block : function.blocks) {
        for (const auto& parameter : block.parameters)
            analysis.defining_blocks[parameter.name] = block.name;
        for (const auto& operation : block.operations) {
            if (operation.result.empty()) continue;
            analysis.defining_blocks[operation.result] = block.name;
            if ((operation.opcode == "copy" || operation.opcode == "bitcast") &&
                operation.operands.size() == 1U) {
                add_alias_dependency(analysis, operation.operands.front(), operation.result);
            } else if ((operation.opcode == "ptr.offset" || operation.opcode == "field.address") &&
                       operation.operands.size() == 2U) {
                add_alias_dependency(analysis, operation.operands[0], operation.result);
                add_alias_dependency(analysis, operation.operands[1], operation.result);
            }
        }
    }
}

const ir::Operation* find_definition_operation(const ir::Function& function,
                                               const UseDefInfo& use_def,
                                               const std::string& value) {
    const auto definition = use_def.definitions.find(value);
    if (definition == use_def.definitions.end() || definition->second.is_parameter) return nullptr;
    const auto* block = find_block(function, definition->second.block);
    if (block == nullptr || definition->second.operation_index >= block->operations.size()) return nullptr;
    return &block->operations[definition->second.operation_index];
}

std::optional<std::int64_t> resolve_alias_constant(const ir::Function& function,
                                                   const UseDefInfo& use_def,
                                                   const std::string& value) {
    if (const auto immediate = parse_integer(value)) return immediate;
    const auto* operation = find_definition_operation(function, use_def, value);
    if (operation == nullptr || operation->opcode != "const" || operation->operands.size() != 1U)
        return std::nullopt;
    return parse_integer(operation->operands.front());
}

std::optional<MemoryLocation> resolve_alias_pointer(const ir::Function& function,
                                                    const UseDefInfo& use_def,
                                                    AliasAnalysis& analysis,
                                                    const std::string& value,
                                                    std::unordered_set<std::string>& visiting) {
    if (const auto existing = analysis.pointers.find(value); existing != analysis.pointers.end())
        return existing->second;
    if (!visiting.insert(value).second) return std::nullopt;

    for (const auto& parameter : function.parameters) {
        if (parameter.name == value) {
            visiting.erase(value);
            if (parameter.type != ir::ptr_type()) return std::nullopt;
            MemoryLocation location{PointerOriginKind::argument, parameter.name, 0, 0, true};
            analysis.pointers[value] = location;
            return location;
        }
    }

    const auto definition = use_def.definitions.find(value);
    if (definition == use_def.definitions.end()) {
        visiting.erase(value);
        return std::nullopt;
    }
    if (definition->second.is_parameter) {
        const auto* block = find_block(function, definition->second.block);
        if (block != nullptr) {
            const auto parameter = std::find_if(block->parameters.begin(), block->parameters.end(),
                                                [&](const auto& item) { return item.name == value; });
            if (parameter != block->parameters.end() && parameter->type == ir::ptr_type()) {
                MemoryLocation location{PointerOriginKind::unknown, value, 0, 0, false};
                analysis.pointers[value] = location;
                visiting.erase(value);
                return location;
            }
        }
        visiting.erase(value);
        return std::nullopt;
    }

    const auto* operation = find_definition_operation(function, use_def, value);
    if (operation == nullptr) {
        visiting.erase(value);
        return std::nullopt;
    }
    std::optional<MemoryLocation> location;
    if (operation->opcode == "stack.alloc" || operation->opcode == "stack.alloc.struct" ||
        operation->opcode == "stack.alloc.array") {
        location = MemoryLocation{PointerOriginKind::stack, value, 0, 0, true};
    } else if ((operation->opcode == "global.address" || operation->opcode == "tls.address") &&
               operation->operands.size() == 1U) {
        location = MemoryLocation{PointerOriginKind::global, operation->operands.front(), 0, 0, true};
    } else if ((operation->opcode == "copy" || operation->opcode == "bitcast") &&
               operation->operands.size() == 1U) {
        location = resolve_alias_pointer(function, use_def, analysis, operation->operands.front(), visiting);
    } else if ((operation->opcode == "ptr.offset" || operation->opcode == "field.address") &&
               operation->operands.size() == 2U) {
        location = resolve_alias_pointer(function, use_def, analysis, operation->operands.front(), visiting);
        if (location) {
            const auto offset = resolve_alias_constant(function, use_def, operation->operands[1]);
            if (offset && location->precise) location->offset += *offset;
            else location->precise = false;
        }
    }
    visiting.erase(value);
    if (location) analysis.pointers[value] = *location;
    return location;
}

void populate_alias_analysis(const ir::Function& function, const UseDefInfo& use_def,
                             AliasAnalysis& analysis) {
    analysis.pointers.clear();
    analysis.pointers.reserve(std::max(analysis.pointers.bucket_count(), use_def.definitions.size()));
    rebuild_alias_metadata(function, analysis);
    std::unordered_set<std::string> visiting;
    for (const auto& [value, _] : use_def.definitions)
        (void)resolve_alias_pointer(function, use_def, analysis, value, visiting);
}

void repair_alias_analysis(const ir::Function& function, const UseDefInfo& use_def,
                           AliasAnalysis& analysis,
                           const std::vector<std::string>& touched_blocks) {
    std::unordered_set<std::string> touched(touched_blocks.begin(), touched_blocks.end());
    std::unordered_set<std::string> affected;
    std::vector<std::string> pending;

    // Old definitions catch values deleted by the rewrite. Current definitions
    // catch newly-created values in touched blocks.
    for (const auto& [value, block] : analysis.defining_blocks) {
        if (touched.contains(block) && affected.insert(value).second) pending.push_back(value);
    }
    for (const auto& block_name : touched_blocks) {
        const auto* block = find_block(function, block_name);
        if (block == nullptr) continue;
        for (const auto& parameter : block->parameters)
            if (affected.insert(parameter.name).second) pending.push_back(parameter.name);
        for (const auto& operation : block->operations)
            if (!operation.result.empty() && affected.insert(operation.result).second)
                pending.push_back(operation.result);
    }

    // Walk the old reverse graph before refreshing metadata so a changed base
    // invalidates derived pointers in otherwise untouched blocks.
    for (std::size_t cursor = 0; cursor < pending.size(); ++cursor) {
        const auto dependents = analysis.dependents.find(pending[cursor]);
        if (dependents == analysis.dependents.end()) continue;
        for (const auto& dependent : dependents->second)
            if (affected.insert(dependent).second) pending.push_back(dependent);
    }
    for (const auto& value : affected) analysis.pointers.erase(value);

    rebuild_alias_metadata(function, analysis);
    std::unordered_set<std::string> visiting;
    for (const auto& value : affected)
        (void)resolve_alias_pointer(function, use_def, analysis, value, visiting);
}
} // namespace

AliasAnalysis build_alias_analysis(const ir::Function& function) {
    AliasAnalysis analysis;
    const auto use_def = build_use_def(function);
    populate_alias_analysis(function, use_def, analysis);
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
        populate_cfg(*function_, cfg_);
        has_cfg_ = true;
    }
    return cfg_;
}

const UseDefInfo& FunctionAnalysisManager::use_def() {
    if (!has_use_def_) {
        populate_use_def(*function_, use_def_);
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
        populate_alias_analysis(*function_, use_def(), aliases_);
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

void FunctionAnalysisManager::invalidate(InvalidationScope scope) {
    invalidate(scope, {});
}

void FunctionAnalysisManager::invalidate(InvalidationScope scope,
                                         const std::vector<std::string>& touched_blocks) {
    switch (scope) {
    case InvalidationScope::none: return;
    case InvalidationScope::operations: invalidate_operations(touched_blocks); return;
    case InvalidationScope::control_flow: invalidate_control_flow(touched_blocks); return;
    case InvalidationScope::all: invalidate_all(); return;
    }
}

void FunctionAnalysisManager::repair_use_def(const std::vector<std::string>& touched_blocks) {
    // Local repair is intended for genuinely local rewrites. Once a pass has
    // touched a substantial portion of the CFG, one linear rebuild is cheaper
    // than repeated block lookup/subtraction and produces the same cache.
    const bool local = !touched_blocks.empty() &&
        (function_->blocks.size() < 8 || touched_blocks.size() * 4U <= function_->blocks.size());
    if (!has_use_def_ || !local) {
        populate_use_def(*function_, use_def_);
        has_use_def_ = true;
        return;
    }
    repair_use_def_blocks(*function_, use_def_, touched_blocks);
}

void FunctionAnalysisManager::invalidate_operations() {
    invalidate_operations({});
}

void FunctionAnalysisManager::invalidate_operations(const std::vector<std::string>& touched_blocks) {
    // If use-def is already hot and the pass names its local edits, repair it
    // in place. Otherwise retain the old lazy full-rebuild behavior. Alias
    // information is still conservatively invalidated function-wide.
    const bool local = !touched_blocks.empty() &&
        (function_->blocks.size() < 8 || touched_blocks.size() * 4U <= function_->blocks.size());
    if (has_use_def_ && local)
        repair_use_def_blocks(*function_, use_def_, touched_blocks);
    else
        has_use_def_ = false;

    if (has_aliases_ && local) {
        if (!has_use_def_) {
            populate_use_def(*function_, use_def_);
            has_use_def_ = true;
        }
        repair_alias_analysis(*function_, use_def_, aliases_, touched_blocks);
    } else {
        has_aliases_ = false;
    }
}

void FunctionAnalysisManager::invalidate_control_flow() {
    invalidate_control_flow({});
}

void FunctionAnalysisManager::invalidate_control_flow(const std::vector<std::string>& touched_blocks) {
    const bool local = !touched_blocks.empty() &&
        (function_->blocks.size() < 8 || touched_blocks.size() * 4U <= function_->blocks.size());
    if (has_cfg_ && local) {
        const auto repair = repair_cfg_blocks(*function_, cfg_, touched_blocks);
        if (repair.valid) {
            if (repair.changed) {
                if (has_dominators_ && !repair.reachability_changed)
                    repair_dominator_tree(*function_, cfg_, dominators_, repair.affected_successors);
                else
                    has_dominators_ = false;
                has_loops_ = false;
            }
            invalidate_operations(touched_blocks);
            return;
        }
    }
    has_cfg_ = false;
    has_dominators_ = false;
    has_loops_ = false;
    invalidate_operations();
}

void FunctionAnalysisManager::invalidate_all() {
    has_cfg_ = false;
    has_use_def_ = false;
    has_dominators_ = false;
    has_aliases_ = false;
    has_loops_ = false;
}

} // namespace forge::analysis
