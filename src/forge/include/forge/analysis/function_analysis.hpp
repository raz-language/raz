// Copyright 2026 Mario Vinciguerra
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "forge/ir/module.hpp"

namespace forge::analysis {

struct ControlFlowGraph {
    std::unordered_map<std::string, std::vector<std::string>> successors;
    std::unordered_map<std::string, std::vector<std::string>> predecessors;
    std::unordered_set<std::string> reachable;
};

struct UseDefInfo {
    struct Definition {
        std::string block;
        std::size_t operation_index{};
        bool is_parameter{};
    };
    std::unordered_map<std::string, Definition> definitions;
    std::unordered_map<std::string, std::size_t> use_count;
};


enum class PointerOriginKind : std::uint8_t { unknown, stack, global, argument };
enum class AliasResult : std::uint8_t { no_alias, may_alias, must_alias };

struct MemoryLocation {
    PointerOriginKind origin{PointerOriginKind::unknown};
    std::string base;
    std::int64_t offset{};
    std::uint32_t size{};
    bool precise{};
};

struct AliasAnalysis {
    std::unordered_map<std::string, MemoryLocation> pointers;

    [[nodiscard]] MemoryLocation location(const std::string& value,
                                          std::uint32_t access_size = 0) const;
    [[nodiscard]] AliasResult alias(const MemoryLocation& left,
                                    const MemoryLocation& right) const noexcept;
};

struct NaturalLoop {
    std::string header;
    std::string latch;
    std::string preheader;
    std::unordered_set<std::string> blocks;
};

struct LoopInfo {
    std::vector<NaturalLoop> loops;
};

struct DominatorTree {
    std::unordered_map<std::string, std::unordered_set<std::string>> dominators;

    [[nodiscard]] bool dominates(const std::string& candidate,
                                 const std::string& block) const;
};

[[nodiscard]] ControlFlowGraph build_cfg(const ir::Function& function);
[[nodiscard]] UseDefInfo build_use_def(const ir::Function& function);
[[nodiscard]] DominatorTree build_dominator_tree(const ir::Function& function,
                                                 const ControlFlowGraph& cfg);
[[nodiscard]] AliasAnalysis build_alias_analysis(const ir::Function& function);
[[nodiscard]] LoopInfo build_loop_info(const ir::Function& function,
                                       const ControlFlowGraph& cfg,
                                       const DominatorTree& dominators);

class FunctionAnalysisManager {
public:
    explicit FunctionAnalysisManager(const ir::Function& function)
        : function_(&function) {}

    [[nodiscard]] const ControlFlowGraph& cfg();
    [[nodiscard]] const UseDefInfo& use_def();
    [[nodiscard]] const DominatorTree& dominators();
    [[nodiscard]] const AliasAnalysis& aliases();
    [[nodiscard]] const LoopInfo& loops();

    void invalidate_all();

private:
    const ir::Function* function_;
    bool has_cfg_{};
    bool has_use_def_{};
    bool has_dominators_{};
    bool has_aliases_{};
    bool has_loops_{};
    ControlFlowGraph cfg_;
    UseDefInfo use_def_;
    DominatorTree dominators_;
    AliasAnalysis aliases_;
    LoopInfo loops_;
};

} // namespace forge::analysis
