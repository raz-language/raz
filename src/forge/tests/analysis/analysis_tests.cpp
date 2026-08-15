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
        const auto& function = parsed.module->functions().front();
        const auto cfg = forge::analysis::build_cfg(function);
        require(cfg.reachable.size() == 4, "CFG reachability is incorrect");
        require(cfg.predecessors.at("merge").size() == 2, "CFG predecessor count is incorrect");

        const auto uses = forge::analysis::build_use_def(function);
        require(uses.use_count.at("%one") == 2, "use count is incorrect");
        require(uses.definitions.contains("%result"), "block parameter definition missing");

        const auto dominators = forge::analysis::build_dominator_tree(function, cfg);
        require(dominators.dominates("entry", "merge"), "entry should dominate merge");
        require(!dominators.dominates("left", "merge"), "left must not dominate merge");

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
        std::cout << "Forge analysis tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "test failure: " << error.what() << '\n';
        return 1;
    }
}
