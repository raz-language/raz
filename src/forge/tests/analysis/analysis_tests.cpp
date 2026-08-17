// Copyright 2026 Mario Vinciguerra
// SPDX-License-Identifier: Apache-2.0

#include "forge/analysis/function_analysis.hpp"
#include "forge/ir/parser.hpp"

#include <iostream>
#include <stdexcept>

static void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

int main() {
    try {
        constexpr auto source = R"(module @analysis {
func @diamond(%condition: i1) -> i32 {
entry:
  %one = const i32 1
  branch %condition, left(%one), right(%one)
left(%value_left: i32):
  jump merge(%value_left)
right(%value_right: i32):
  jump merge(%value_right)
merge(%result: i32):
  return %result
}
})";
        auto parsed = forge::ir::parse_module(source);
        require(parsed.ok(), "analysis fixture failed to parse");
        auto& function = parsed.module->functions().front();
        const auto cfg = forge::analysis::build_cfg(function);
        require(cfg.reachable.size() == 4, "CFG reachability is incorrect");
        require(cfg.predecessors.at("merge").size() == 2, "CFG predecessor count is incorrect");

        const auto uses = forge::analysis::build_use_def(function);
        require(uses.use_count.at("%one") == 2, "use count is incorrect");
        require(uses.definitions.contains("%result"), "block parameter definition missing");

        const auto dominators = forge::analysis::build_dominator_tree(function, cfg);
        require(dominators.dominates("entry", "merge"), "entry should dominate merge");
        require(!dominators.dominates("left", "merge"), "left must not dominate merge");

        // Analysis-manager invalidation is dependency-scoped. Operation-only
        // edits must refresh use-def/alias state while preserving valid CFG
        // metadata; control-flow edits invalidate the complete dependent chain.
        forge::analysis::FunctionAnalysisManager manager(function);
        require(manager.cfg().reachable.size() == 4, "manager CFG fixture is incorrect");
        require(manager.dominators().dominates("entry", "merge"), "manager dominator fixture is incorrect");
        const auto& initial_use_def = manager.use_def();
        const auto one_uses_before = initial_use_def.use_count.at("%one");
        const auto* untouched_right = &initial_use_def.block_contributions.at("right");

        forge::ir::Operation dead_copy;
        dead_copy.result = "%manager_added";
        dead_copy.opcode = "copy";
        dead_copy.type = forge::ir::i32_type();
        dead_copy.operands = {"%one"};
        function.blocks.front().operations.insert(function.blocks.front().operations.end() - 1,
                                                   std::move(dead_copy));
        manager.invalidate(forge::analysis::InvalidationScope::operations, {"entry"});
        require(manager.cfg().reachable.size() == 4, "operation invalidation damaged cached CFG");
        const auto& repaired_use_def = manager.use_def();
        require(repaired_use_def.definitions.contains("%manager_added"),
                "local operation invalidation failed to repair the touched block definition");
        require(repaired_use_def.use_count.at("%one") == one_uses_before + 1,
                "local operation invalidation failed to repair touched-block use counts");
        require(&repaired_use_def.block_contributions.at("right") == untouched_right,
                "local operation invalidation rebuilt an untouched block contribution");

        // A terminator-only rewrite with stable reachability takes the local
        // CFG path. Redirect right -> merge to right -> left: all four blocks
        // remain reachable, but merge now has a single predecessor and left
        // becomes a dominator of merge. Both facts must be repaired in place.
        auto& right_term = function.blocks[2].operations.back();
        right_term.successors[0] = "left";
        manager.invalidate(forge::analysis::InvalidationScope::control_flow, {"right"});
        require(manager.cfg().reachable.size() == 4,
                "local control-flow repair changed stable reachability");
        require(manager.cfg().predecessors.at("merge").size() == 1 &&
                manager.cfg().predecessors.at("merge").front() == "left",
                "local control-flow repair failed to update predecessor buckets");
        require(manager.cfg().predecessors.at("left").size() == 2,
                "local control-flow repair failed to add the redirected edge");
        require(manager.dominators().dominates("left", "merge"),
                "local dominator repair failed after a terminator rewrite");

        // Reachability-changing edits retain the conservative fallback.
        auto& entry_term = function.blocks.front().operations.back();
        entry_term.successors[1] = "left";
        manager.invalidate(forge::analysis::InvalidationScope::control_flow);
        require(!manager.cfg().reachable.contains("right"),
                "full control-flow invalidation failed to refresh reachability");

        constexpr auto memory_source = R"(module @aliasing {
func @memory(%input: ptr) -> i64 {
entry:
  %left = stack.alloc ptr 8 align 8
  %right = stack.alloc ptr 8 align 8
  %left_field = ptr.offset ptr %left 2
  %global = global.address ptr @counter
  %value = load i64 %left
  return %value
}
global @counter: i64 = 0
})";
        auto memory = forge::ir::parse_module(memory_source);
        require(memory.ok(), "alias fixture failed to parse");
        const auto aliases = forge::analysis::build_alias_analysis(memory.module->functions().front());
        const auto left = aliases.location("%left", 4);
        const auto right = aliases.location("%right", 4);
        const auto left_field = aliases.location("%left_field", 4);
        const auto global = aliases.location("%global", 8);
        const auto input = aliases.location("%input", 8);
        require(aliases.alias(left, right) == forge::analysis::AliasResult::no_alias, "distinct stack slots must not alias");
        require(aliases.alias(left, global) == forge::analysis::AliasResult::no_alias, "stack and global memory must not alias");
        require(aliases.alias(left, input) == forge::analysis::AliasResult::no_alias, "stack and argument memory must not alias");
        require(aliases.alias(left, left) == forge::analysis::AliasResult::must_alias, "identical stack locations must alias");
        require(aliases.alias(left, left_field) == forge::analysis::AliasResult::may_alias, "overlapping stack ranges should conservatively may-alias");

        constexpr auto incremental_alias_source = R"(module @incremental_alias {
func @memory() -> i64 {
entry:
  %base = stack.alloc ptr 8 align 8
  jump next
next:
  %copy = copy ptr %base
  %field = ptr.offset ptr %copy 4
  %value = load i64 %field
  return %value
}
global @counter: i64 = 0
})";
        auto incremental_alias = forge::ir::parse_module(incremental_alias_source);
        require(incremental_alias.ok(), "incremental alias fixture failed to parse");
        auto& alias_function = incremental_alias.module->functions().front();
        forge::analysis::FunctionAnalysisManager alias_manager(alias_function);
        const auto initial_field = alias_manager.aliases().location("%field", 8);
        require(initial_field.origin == forge::analysis::PointerOriginKind::stack && initial_field.offset == 4,
                "initial transitive alias location is incorrect");
        auto& base_definition = alias_function.blocks.front().operations.front();
        base_definition.opcode = "global.address";
        base_definition.operands = {"@counter"};
        alias_manager.invalidate(forge::analysis::InvalidationScope::operations, {"entry"});
        const auto repaired_field = alias_manager.aliases().location("%field", 8);
        require(repaired_field.origin == forge::analysis::PointerOriginKind::global &&
                repaired_field.base == "@counter" && repaired_field.offset == 4,
                "incremental alias repair missed a transitive cross-block dependent");

        std::cout << "Forge analysis tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "test failure: " << error.what() << '\n';
        return 1;
    }
}
