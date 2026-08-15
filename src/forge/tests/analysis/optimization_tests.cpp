// Copyright 2026 Mario Vinciguerra
// SPDX-License-Identifier: Apache-2.0

#include "forge/interpreter/interpreter.hpp"
#include "forge/ir/parser.hpp"
#include "forge/ir/printer.hpp"
#include "forge/ir/verifier.hpp"
#include "forge/pass/pass.hpp"
#include "forge/transforms/scalar.hpp"

#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <vector>

static void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

int main() {
    try {
        constexpr auto source = R"(module @optimization {
func @compute(%value: i32) -> i32 {
entry:
  %zero = const i32 0
  %one = const i32 1
  %always = const i1 1
  %left = add i32 %value %zero
  %right = mul i32 %left %one
  %first = add i32 %right %one
  %duplicate = add i32 %right %one
  branch %always, live(%first, %duplicate), dead(%zero)
live(%a: i32, %b: i32):
  %answer = add i32 %a %b
  return %answer
dead(%unused: i32):
  %noise = mul i32 %unused %unused
  return %noise
}
})";
        auto parsed = forge::ir::parse_module(source);
        require(parsed.ok(), "optimization fixture failed to parse");
        const std::vector<forge::interpreter::Value> arguments{
            forge::interpreter::Value::integer(forge::ir::Type(forge::ir::TypeKind::i32), 20)};
        const auto before = forge::interpreter::execute(*parsed.module, "compute", arguments);
        require(before.value.has_value() && before.diagnostics.empty() && before.value->signed_value() == 42, "pre-optimization result mismatch");

        forge::pass::PassManager pipeline;
        pipeline.add<forge::transforms::SparseConditionalConstantPropagationPass>()
                .add<forge::transforms::AlgebraicSimplificationPass>()
                .add<forge::transforms::CopyPropagationPass>()
                .add<forge::transforms::CommonSubexpressionEliminationPass>()
                .add<forge::transforms::CopyPropagationPass>()
                .add<forge::transforms::DeadCodeEliminationPass>()
                .add<forge::transforms::SimplifyCFGPass>();
        const auto stats = pipeline.run(*parsed.module);
        require(stats.changed, "optimizer reported no changes");
        require(stats.blocks_removed >= 1, "SCCP did not remove dead branch");
        require(stats.operations_rewritten >= 3, "expected algebraic/CSE rewrites");

        const auto diagnostics = forge::ir::verify_module(*parsed.module);
        for (const auto& diagnostic : diagnostics)
            require(diagnostic.severity != forge::DiagnosticSeverity::error, "optimized module failed verification");
        const auto after = forge::interpreter::execute(*parsed.module, "compute", arguments);
        require(after.value.has_value() && after.diagnostics.empty() && after.value->signed_value() == 42, "post-optimization result mismatch");

        const auto text = forge::ir::print_module(*parsed.module);
        require(text.find("dead(") == std::string::npos, "dead block remains after SCCP");

        // Regression: copy propagation may rewrite a copy's source and then
        // introduce a use of an earlier copy when rewriting a later consumer.
        // Removal decisions must use the post-rewrite use-def graph rather
        // than stale replacement counts, or %first can be deleted while the
        // rewritten ptr.offset operations still reference it.
        constexpr auto transitive_copy_source = R"(module @transitive_copy {
func @copy_chain(%base: ptr) -> ptr {
entry:
  %first = copy ptr %base
  %second = copy ptr %first
  %zero = const i64 0
  %left = ptr.offset ptr %second %zero
  %third = copy ptr %first
  %right = ptr.offset ptr %third %zero
  return %right
}
})";
        auto transitive_copy = forge::ir::parse_module(transitive_copy_source);
        require(transitive_copy.ok(), "transitive-copy fixture failed to parse");
        forge::pass::PassManager transitive_copy_pipeline;
        transitive_copy_pipeline.add<forge::transforms::CopyPropagationPass>();
        (void)transitive_copy_pipeline.run(*transitive_copy.module);
        const auto transitive_copy_diagnostics = forge::ir::verify_module(*transitive_copy.module);
        for (const auto& diagnostic : transitive_copy_diagnostics)
            require(diagnostic.severity != forge::DiagnosticSeverity::error,
                    "transitive copy propagation left an undefined value");

        constexpr auto memory_source = R"(module @memory_forward {
func @forward() -> i64 {
entry:
  %left = stack.alloc ptr 8 align 8
  %right = stack.alloc ptr 8 align 8
  %ten = const i64 10
  %twenty = const i64 20
  store i64 %ten %left align 8
  %first = load i64 %left align 8
  store i64 %twenty %right align 8
  %second = load i64 %left align 8
  %sum = add i64 %first %second
  return %sum
}
})";
        auto memory = forge::ir::parse_module(memory_source);
        require(memory.ok(), "memory-forwarding fixture failed to parse");
        forge::pass::PassManager memory_pipeline;
        memory_pipeline.add<forge::transforms::MemoryForwardingPass>()
                       .add<forge::transforms::CopyPropagationPass>()
                       .add<forge::transforms::DeadCodeEliminationPass>();
        const auto memory_stats = memory_pipeline.run(*memory.module);
        require(memory_stats.operations_rewritten >= 2, "store/load forwarding did not rewrite both loads");
        const auto memory_text = forge::ir::print_module(*memory.module);
        require(memory_text.find("load i64 %left") == std::string::npos, "forwarded loads remain in IR");
        const auto memory_result = forge::interpreter::execute(*memory.module, "forward", {});
        require(memory_result.value.has_value() && memory_result.value->signed_value() == 20, "memory forwarding changed semantics");

        // Regression: dataflow state equality must converge even when a pointer
        // location is imprecise. Alias queries correctly classify such locations
        // as may-alias, but identical abstract states are still equal for the
        // fixed-point iteration.
        constexpr auto imprecise_memory_source = R"(module @imprecise_memory_forward {
func @forward_imprecise() -> i64 {
entry:
  %base = stack.alloc ptr 16 align 8
  %zero = const i64 0
  %scale = const i64 8
  %dynamic_offset = mul i64 %zero %scale
  %slot = ptr.offset ptr %base %dynamic_offset
  %value = const i64 42
  store i64 %value %slot align 8
  %loaded = load i64 %slot align 8
  return %loaded
}
})";
        auto imprecise_memory = forge::ir::parse_module(imprecise_memory_source);
        require(imprecise_memory.ok(), "imprecise memory-forwarding fixture failed to parse");
        forge::pass::PassManager imprecise_memory_pipeline;
        imprecise_memory_pipeline.add<forge::transforms::MemoryForwardingPass>();
        (void)imprecise_memory_pipeline.run(*imprecise_memory.module);
        const auto imprecise_memory_result = forge::interpreter::execute(*imprecise_memory.module, "forward_imprecise", {});
        require(imprecise_memory_result.value.has_value() &&
                    imprecise_memory_result.value->signed_value() == 42,
                "imprecise memory forwarding changed semantics");

        auto imprecise_dead_store = forge::ir::parse_module(imprecise_memory_source);
        require(imprecise_dead_store.ok(), "imprecise dead-store fixture failed to parse");
        forge::pass::PassManager imprecise_dead_store_pipeline;
        imprecise_dead_store_pipeline.add<forge::transforms::DeadStoreEliminationPass>();
        (void)imprecise_dead_store_pipeline.run(*imprecise_dead_store.module);
        const auto imprecise_dead_store_result = forge::interpreter::execute(*imprecise_dead_store.module, "forward_imprecise", {});
        require(imprecise_dead_store_result.value.has_value() &&
                    imprecise_dead_store_result.value->signed_value() == 42,
                "imprecise dead-store elimination changed semantics");

        constexpr auto global_load_source = R"(module @global_load_forward {
func @straight() -> i64 {
entry:
  %slot = stack.alloc ptr 8 align 8
  %value = const i64 42
  store i64 %value %slot align 8
  jump next()
next:
  %loaded = load i64 %slot align 8
  return %loaded
}
func @merge_same(%condition: i1) -> i64 {
entry:
  %slot = stack.alloc ptr 8 align 8
  %value = const i64 77
  store i64 %value %slot align 8
  branch %condition, left(), right()
left:
  jump merge()
right:
  jump merge()
merge:
  %loaded = load i64 %slot align 8
  return %loaded
}
func @merge_different(%condition: i1) -> i64 {
entry:
  %slot = stack.alloc ptr 8 align 8
  branch %condition, left(), right()
left:
  %left_value = const i64 11
  store i64 %left_value %slot align 8
  jump merge()
right:
  %right_value = const i64 22
  store i64 %right_value %slot align 8
  jump merge()
merge:
  %loaded = load i64 %slot align 8
  return %loaded
}
func @call_barrier() -> i64 {
entry:
  %slot = stack.alloc ptr 8 align 8
  %value = const i64 9
  store i64 %value %slot align 8
  jump invoke()
invoke:
  call void @unknown()
  %loaded = load i64 %slot align 8
  return %loaded
}
extern func @unknown() -> void
})";
        auto global_load = forge::ir::parse_module(global_load_source);
        require(global_load.ok(), "global load-forwarding fixture failed to parse");
        forge::pass::PassManager global_load_pipeline;
        global_load_pipeline.add<forge::transforms::MemoryForwardingPass>()
                            .add<forge::transforms::CopyPropagationPass>()
                            .add<forge::transforms::DeadCodeEliminationPass>();
        const auto global_load_stats = global_load_pipeline.run(*global_load.module);
        require(global_load_stats.operations_rewritten >= 2,
                "global load forwarding did not rewrite straight-line and same-value merge loads");
        const auto global_load_text = forge::ir::print_module(*global_load.module);
        const auto straight_begin = global_load_text.find("func @straight");
        const auto merge_same_begin = global_load_text.find("func @merge_same");
        const auto merge_different_begin = global_load_text.find("func @merge_different");
        const auto call_barrier_begin = global_load_text.find("func @call_barrier");
        require(straight_begin != std::string::npos && merge_same_begin != std::string::npos &&
                merge_different_begin != std::string::npos && call_barrier_begin != std::string::npos,
                "global load-forwarding functions missing after optimization");
        require(global_load_text.substr(straight_begin, merge_same_begin - straight_begin).find("load i64") == std::string::npos,
                "straight-line cross-block load was not forwarded");
        require(global_load_text.substr(merge_same_begin, merge_different_begin - merge_same_begin).find("load i64") == std::string::npos,
                "same-value merge load was not forwarded");
        require(global_load_text.substr(merge_different_begin, call_barrier_begin - merge_different_begin).find("load i64") != std::string::npos,
                "different-value merge load was unsafely forwarded");
        require(global_load_text.substr(call_barrier_begin).find("load i64") != std::string::npos,
                "load after call barrier was unsafely forwarded");
        require(forge::interpreter::execute(*global_load.module, "straight", {}).value->signed_value() == 42,
                "straight-line global load forwarding changed semantics");
        for (const auto condition : {false, true}) {
            const std::vector<forge::interpreter::Value> arguments{
                forge::interpreter::Value::integer(forge::ir::Type(forge::ir::TypeKind::i1), condition ? 1 : 0)};
            const auto same = forge::interpreter::execute(*global_load.module, "merge_same", arguments);
            const auto different = forge::interpreter::execute(*global_load.module, "merge_different", arguments);
            require(same.value.has_value() && same.value->signed_value() == 77,
                    "same-value merge forwarding changed semantics");
            require(different.value.has_value() && different.value->signed_value() == (condition ? 11 : 22),
                    "different-value merge forwarding changed semantics");
        }


        constexpr auto dead_store_source = R"(module @dead_store {
func @overwrite() -> i64 {
entry:
  %slot = stack.alloc ptr 8 align 8
  %ten = const i64 10
  %twenty = const i64 20
  store i64 %ten %slot align 8
  store i64 %twenty %slot align 8
  %result = load i64 %slot align 8
  return %result
}
func @observed() -> i64 {
entry:
  %slot = stack.alloc ptr 8 align 8
  %ten = const i64 10
  %twenty = const i64 20
  store i64 %ten %slot align 8
  %first = load i64 %slot align 8
  store i64 %twenty %slot align 8
  %second = load i64 %slot align 8
  %result = add i64 %first %second
  return %result
}
func @global_overwrite() -> i64 {
entry:
  %slot = stack.alloc ptr 8 align 8
  %ten = const i64 10
  store i64 %ten %slot align 8
  jump overwrite_block()
overwrite_block:
  %twenty = const i64 20
  store i64 %twenty %slot align 8
  jump exit()
exit:
  %result = load i64 %slot align 8
  return %result
}
func @all_paths_overwrite(%condition: i1) -> i64 {
entry:
  %slot = stack.alloc ptr 8 align 8
  %ten = const i64 10
  store i64 %ten %slot align 8
  branch %condition, left(), right()
left:
  %twenty = const i64 20
  store i64 %twenty %slot align 8
  jump merge()
right:
  %thirty = const i64 30
  store i64 %thirty %slot align 8
  jump merge()
merge:
  %result = load i64 %slot align 8
  return %result
}
func @one_path_observes(%condition: i1) -> i64 {
entry:
  %slot = stack.alloc ptr 8 align 8
  %ten = const i64 10
  store i64 %ten %slot align 8
  branch %condition, observed_path(), overwrite_path()
observed_path:
  %seen = load i64 %slot align 8
  return %seen
overwrite_path:
  %twenty = const i64 20
  store i64 %twenty %slot align 8
  %result = load i64 %slot align 8
  return %result
}
})";
        auto dead_store = forge::ir::parse_module(dead_store_source);
        require(dead_store.ok(), "dead-store fixture failed to parse");
        forge::pass::PassManager dead_store_pipeline;
        dead_store_pipeline.add<forge::transforms::DeadStoreEliminationPass>();
        const auto dead_store_stats = dead_store_pipeline.run(*dead_store.module);
        require(dead_store_stats.operations_removed == 3,
                "dead-store elimination did not remove local and global overwritten stores");
        const auto dead_store_text = forge::ir::print_module(*dead_store.module);
        const auto overwrite_begin = dead_store_text.find("func @overwrite");
        const auto observed_begin = dead_store_text.find("func @observed");
        require(overwrite_begin != std::string::npos && observed_begin != std::string::npos,
                "dead-store functions missing after optimization");
        const auto overwrite_text = dead_store_text.substr(overwrite_begin, observed_begin - overwrite_begin);
        require(overwrite_text.find("store i64 %ten") == std::string::npos,
                "overwritten store remains in optimized function");
        require(dead_store_text.substr(observed_begin).find("store i64 %ten") != std::string::npos,
                "store observed by an intervening load was incorrectly removed");
        const auto overwritten_result = forge::interpreter::execute(*dead_store.module, "overwrite", {});
        const auto observed_result = forge::interpreter::execute(*dead_store.module, "observed", {});
        const auto global_result = forge::interpreter::execute(*dead_store.module, "global_overwrite", {});
        require(overwritten_result.value.has_value() && overwritten_result.value->signed_value() == 20,
                "dead-store elimination changed overwrite semantics");
        require(observed_result.value.has_value() && observed_result.value->signed_value() == 30,
                "dead-store elimination removed an observed store");
        require(global_result.value.has_value() && global_result.value->signed_value() == 20,
                "global dead-store elimination changed straight-line CFG semantics");
        for (const auto condition : {false, true}) {
            const std::vector<forge::interpreter::Value> arguments{
                forge::interpreter::Value::integer(forge::ir::Type(forge::ir::TypeKind::i1), condition ? 1 : 0)};
            const auto all_paths = forge::interpreter::execute(
                *dead_store.module, "all_paths_overwrite", arguments);
            const auto one_path = forge::interpreter::execute(
                *dead_store.module, "one_path_observes", arguments);
            require(all_paths.value.has_value() && all_paths.value->signed_value() == (condition ? 20 : 30),
                    "global dead-store elimination changed branch-overwrite semantics");
            require(one_path.value.has_value() && one_path.value->signed_value() == (condition ? 10 : 20),
                    "global dead-store elimination removed a store observed on one successor path");
        }


        constexpr auto promotion_source = R"(module @stack_promotion {
func @promote(%input: i64) -> i64 {
entry:
  %slot = stack.alloc ptr 8 align 8
  store i64 %input %slot align 8
  %first = load i64 %slot align 8
  %one = const i64 1
  %next = add i64 %first %one
  store i64 %next %slot align 8
  %second = load i64 %slot align 8
  return %second
}
})";
        auto promotion = forge::ir::parse_module(promotion_source);
        require(promotion.ok(), "scalar stack-promotion fixture failed to parse");
        forge::pass::PassManager promotion_pipeline;
        promotion_pipeline.add<forge::transforms::ScalarStackPromotionPass>()
                          .add<forge::transforms::CopyPropagationPass>()
                          .add<forge::transforms::DeadCodeEliminationPass>();
        const auto promotion_stats = promotion_pipeline.run(*promotion.module);
        require(promotion_stats.operations_removed >= 3, "scalar stack promotion did not remove allocation/stores");
        const auto promotion_text = forge::ir::print_module(*promotion.module);
        require(promotion_text.find("stack.alloc") == std::string::npos, "promoted stack allocation remains in IR");
        require(promotion_text.find("load i64") == std::string::npos, "promoted load remains in IR");
        require(promotion_text.find("store i64") == std::string::npos, "promoted store remains in IR");
        const std::vector<forge::interpreter::Value> promotion_arguments{
            forge::interpreter::Value::integer(forge::ir::Type(forge::ir::TypeKind::i64), 41)};
        const auto promotion_result = forge::interpreter::execute(*promotion.module, "promote", promotion_arguments);
        require(promotion_result.value.has_value() && promotion_result.value->signed_value() == 42,
                "scalar stack promotion changed semantics");


        constexpr auto cross_block_promotion_source = R"(module @cross_block_promotion {
func @promote_branch(%input: i64, %condition: i1) -> i64 {
entry:
  %slot = stack.alloc ptr 8 align 8
  store i64 %input %slot align 8
  branch %condition, left(), right()
left:
  %one = const i64 1
  %left_value = add i64 %input %one
  store i64 %left_value %slot align 8
  jump merge()
right:
  %two = const i64 2
  %right_value = add i64 %input %two
  store i64 %right_value %slot align 8
  jump merge()
merge:
  %result = load i64 %slot align 8
  return %result
}
})";
        auto cross_block_promotion = forge::ir::parse_module(cross_block_promotion_source);
        require(cross_block_promotion.ok(), "cross-block stack-promotion fixture failed to parse");
        forge::pass::PassManager cross_block_promotion_pipeline;
        cross_block_promotion_pipeline.add<forge::transforms::ScalarStackPromotionPass>()
                                      .add<forge::transforms::CopyPropagationPass>()
                                      .add<forge::transforms::DeadCodeEliminationPass>();
        const auto cross_block_stats = cross_block_promotion_pipeline.run(*cross_block_promotion.module);
        require(cross_block_stats.operations_removed >= 3,
                "cross-block stack promotion did not remove allocation/stores");
        const auto cross_block_text = forge::ir::print_module(*cross_block_promotion.module);
        require(cross_block_text.find("stack.alloc") == std::string::npos,
                "cross-block promoted allocation remains in IR");
        require(cross_block_text.find("load i64") == std::string::npos,
                "cross-block promoted load remains in IR");
        require(cross_block_text.find("store i64") == std::string::npos,
                "cross-block promoted store remains in IR");
        require(cross_block_text.find("%mem2reg.slot.merge") != std::string::npos,
                "cross-block promotion did not insert a merge parameter");
        for (const auto condition : {false, true}) {
            const std::vector<forge::interpreter::Value> arguments{
                forge::interpreter::Value::integer(forge::ir::i64_type(), 40),
                forge::interpreter::Value::integer(forge::ir::Type(forge::ir::TypeKind::i1), condition ? 1 : 0)};
            const auto execution = forge::interpreter::execute(
                *cross_block_promotion.module, "promote_branch", arguments);
            const auto expected = condition ? 41 : 42;
            require(execution.value.has_value() && execution.value->signed_value() == expected,
                    "cross-block stack promotion changed branch semantics");
        }

        constexpr auto loop_mem2reg_source = R"(module @loop_mem2reg {
func @sum_loop(%limit: i64) -> i64 {
entry:
  %slot = stack.alloc ptr 8 align 8
  %zero = const i64 0
  %one = const i64 1
  store i64 %zero %slot align 8
  jump header(%zero)
header(%index: i64):
  %again = cmp.lt i64 %index %limit
  branch %again, body(%index), exit()
body(%current_index: i64):
  %current = load i64 %slot align 8
  %next_total = add i64 %current %current_index
  store i64 %next_total %slot align 8
  %next_index = add i64 %current_index %one
  jump header(%next_index)
exit:
  %result = load i64 %slot align 8
  return %result
}
})";
        auto loop_mem2reg = forge::ir::parse_module(loop_mem2reg_source);
        require(loop_mem2reg.ok(), "loop mem2reg fixture failed to parse");
        forge::pass::PassManager loop_mem2reg_pipeline;
        loop_mem2reg_pipeline.add<forge::transforms::ScalarStackPromotionPass>()
                             .add<forge::transforms::CopyPropagationPass>()
                             .add<forge::transforms::DeadCodeEliminationPass>();
        const auto loop_mem2reg_stats = loop_mem2reg_pipeline.run(*loop_mem2reg.module);
        require(loop_mem2reg_stats.changed, "loop mem2reg did not change the function");
        const auto loop_mem2reg_text = forge::ir::print_module(*loop_mem2reg.module);
        require(loop_mem2reg_text.find("stack.alloc") == std::string::npos,
                "loop mem2reg left the stack allocation in IR");
        require(loop_mem2reg_text.find("load i64") == std::string::npos,
                "loop mem2reg left loads in IR");
        require(loop_mem2reg_text.find("store i64") == std::string::npos,
                "loop mem2reg left stores in IR");
        require(loop_mem2reg_text.find("%mem2reg.slot.header") != std::string::npos,
                "loop mem2reg did not insert a loop-header parameter");
        for (const auto limit : {0LL, 1LL, 5LL, 10LL}) {
            const std::vector<forge::interpreter::Value> arguments{
                forge::interpreter::Value::integer(forge::ir::i64_type(), limit)};
            const auto execution = forge::interpreter::execute(*loop_mem2reg.module, "sum_loop", arguments);
            const auto expected = limit * (limit - 1) / 2;
            require(execution.value.has_value() && execution.value->signed_value() == expected,
                    "loop mem2reg changed counted-loop semantics");
        }

        constexpr auto loop_source = R"(module @licm {
func @loop(%start: i64) -> i64 {
entry:
  %limit = const i64 8
  jump header(%start)
header(%value: i64):
  %step = const i64 2
  %next = add i64 %value %step
  %again = cmp.lt i64 %next %limit
  branch %again, header(%next), exit(%next)
exit(%result: i64):
  return %result
}
})";
        auto loop = forge::ir::parse_module(loop_source);
        require(loop.ok(), "LICM fixture failed to parse");
        auto& loop_function = loop.module->functions().front();
        forge::analysis::FunctionAnalysisManager loop_analyses(loop_function);
        forge::transforms::LoopInvariantCodeMotionPass licm;
        const auto licm_stats = licm.run(loop_function, loop_analyses);
        require(licm_stats.changed && licm_stats.operations_rewritten >= 1, "LICM did not hoist invariant operation");
        const auto loop_diagnostics = forge::ir::verify_module(*loop.module);
        require(std::none_of(loop_diagnostics.begin(), loop_diagnostics.end(), [](const auto& diagnostic) {
            return diagnostic.severity == forge::DiagnosticSeverity::error;
        }), "LICM produced invalid IR");
        const auto loop_text = forge::ir::print_module(*loop.module);
        const auto entry_position = loop_text.find("%step = const i64 2");
        const auto header_position = loop_text.find("header(");
        require(entry_position != std::string::npos && entry_position < header_position, "LICM did not move invariant into preheader");
        const std::vector<forge::interpreter::Value> loop_arguments{
            forge::interpreter::Value::integer(forge::ir::i64_type(), 0)};
        const auto loop_result = forge::interpreter::execute(*loop.module, "loop", loop_arguments);
        require(loop_result.value.has_value() && loop_result.value->signed_value() == 8, "LICM changed loop semantics");

        constexpr auto reduction_source = R"(module @loop_reduction {
func @sum_to_wide(%limit: i64) -> i64 {
entry:
  %zero = const i64 0
  %one = const i64 1
  jump loop(%zero, %zero)
loop(%index: i64, %total: i64):
  %done = cmp.ge i64 %index %limit
  branch %done, exit(%total), body(%index, %total)
body(%current: i64, %running: i64):
  %next_total = add i64 %running %current
  %next_index = add i64 %current %one
  jump loop(%next_index, %next_total)
exit(%result: i64):
  return %result
}
})";
        auto reduction = forge::ir::parse_module(reduction_source);
        require(reduction.ok(), "loop reduction fixture failed to parse");
        auto& reduction_function = reduction.module->functions().front();
        forge::analysis::FunctionAnalysisManager reduction_analyses(reduction_function);
        forge::transforms::LoopReductionPass reduction_pass;
        const auto reduction_stats = reduction_pass.run(reduction_function, reduction_analyses);
        require(reduction_stats.changed, "loop reduction did not recognize affine sum");
        const auto reduction_text = forge::ir::print_module(*reduction.module);
        require(reduction_text.find("jump loop") == std::string::npos,
                "loop reduction left the counted loop in place");
        require(reduction_text.find("shr.unsigned") != std::string::npos &&
                reduction_text.find("mul i64") != std::string::npos,
                "loop reduction did not emit overflow-conscious closed form");
        require(reduction_text.find("div.signed") == std::string::npos,
                "unit-stride reduction retained generalized trip-count division");
        require(reduction_text.find("series_odd") != std::string::npos &&
                reduction_text.find("series_even") != std::string::npos &&
                reduction_text.find("odd_contribution") == std::string::npos,
                "loop reduction did not split parity to reduce leaf register pressure");
        require(reduction_text.find("%scale = const") == std::string::npos &&
                reduction_text.find("%bias = const") == std::string::npos &&
                reduction_text.find("%initial = const") == std::string::npos,
                "unit-stride reduction materialized neutral affine constants");
        for (const auto input : {-5LL, 0LL, 1LL, 2LL, 3LL, 100LL, 1000LL}) {
            const std::vector<forge::interpreter::Value> reduction_arguments{
                forge::interpreter::Value::integer(forge::ir::i64_type(), input)};
            const auto reduced = forge::interpreter::execute(*reduction.module, "sum_to_wide", reduction_arguments);
            const auto expected = input <= 0 ? 0LL : input * (input - 1LL) / 2LL;
            require(reduced.value.has_value() && reduced.value->signed_value() == expected,
                    "loop reduction changed affine sum semantics");
        }


        const auto check_affine_reduction = [&](std::string_view source, std::string_view function_name,
                                                const std::vector<long long>& inputs,
                                                const auto& expected) {
            auto parsed = forge::ir::parse_module(source);
            require(parsed.ok(), "generalized affine reduction fixture failed to parse");
            auto& candidate = parsed.module->functions().front();
            forge::analysis::FunctionAnalysisManager candidate_analyses(candidate);
            forge::transforms::LoopReductionPass pass;
            const auto stats = pass.run(candidate, candidate_analyses);
            require(stats.changed, "generalized affine reduction was not recognized");
            const auto diagnostics = forge::ir::verify_module(*parsed.module);
            require(std::none_of(diagnostics.begin(), diagnostics.end(), [](const auto& diagnostic) {
                return diagnostic.severity == forge::DiagnosticSeverity::error;
            }), "generalized affine reduction produced invalid IR");
            for (const auto input : inputs) {
                const std::vector<forge::interpreter::Value> arguments{
                    forge::interpreter::Value::integer(forge::ir::i64_type(), input)};
                const auto actual = forge::interpreter::execute(*parsed.module, function_name, arguments);
                require(actual.value.has_value() && actual.value->signed_value() == expected(input),
                        "generalized affine reduction changed semantics");
            }
        };

        constexpr auto stride_source = R"(module @stride_reduction {
func @sum_stride(%limit: i64) -> i64 {
entry:
  %start = const i64 3
  %initial = const i64 7
  %step = const i64 2
  jump loop(%start, %initial)
loop(%index: i64, %total: i64):
  %done = cmp.ge i64 %index %limit
  branch %done, exit(%total), body(%index, %total)
body(%current: i64, %running: i64):
  %next_total = add i64 %running %current
  %next_index = add i64 %current %step
  jump loop(%next_index, %next_total)
exit(%result: i64):
  return %result
}
})";
        check_affine_reduction(stride_source, "sum_stride", {-4, 3, 4, 10, 20}, [](long long limit) {
            long long total = 7;
            for (long long index = 3; index < limit; index += 2) total += index;
            return total;
        });
        {
            auto even_stride = forge::ir::parse_module(stride_source);
            require(even_stride.ok(), "even-stride reduction fixture failed to parse");
            auto& candidate = even_stride.module->functions().front();
            forge::analysis::FunctionAnalysisManager analyses(candidate);
            forge::transforms::LoopReductionPass pass;
            require(pass.run(candidate, analyses).changed,
                    "even-stride reduction was not recognized");
            const auto optimized = forge::ir::print_module(*even_stride.module);
            require(optimized.find("%count_lowbit") == std::string::npos &&
                    optimized.find("%odd_contribution") == std::string::npos &&
                    optimized.find("%half_endpoints") != std::string::npos,
                    "even-stride reduction retained unnecessary parity selection");
        }

        constexpr auto descending_source = R"(module @descending_reduction {
func @sum_descending(%limit: i64) -> i64 {
entry:
  %start = const i64 10
  %initial = const i64 4
  %step = const i64 2
  jump loop(%start, %initial)
loop(%index: i64, %total: i64):
  %done = cmp.le i64 %index %limit
  branch %done, exit(%total), body(%index, %total)
body(%current: i64, %running: i64):
  %next_total = add i64 %running %current
  %next_index = sub i64 %current %step
  jump loop(%next_index, %next_total)
exit(%result: i64):
  return %result
}
})";
        check_affine_reduction(descending_source, "sum_descending", {20, 10, 9, 1, -5}, [](long long limit) {
            long long total = 4;
            for (long long index = 10; index > limit; index -= 2) total += index;
            return total;
        });

        constexpr auto biased_source = R"(module @biased_reduction {
func @sum_biased(%limit: i64) -> i64 {
entry:
  %zero = const i64 0
  %one = const i64 1
  %bias = const i64 5
  jump loop(%zero, %zero)
loop(%index: i64, %total: i64):
  %done = cmp.ge i64 %index %limit
  branch %done, exit(%total), body(%index, %total)
body(%current: i64, %running: i64):
  %term = add i64 %current %bias
  %next_total = add i64 %running %term
  %next_index = add i64 %current %one
  jump loop(%next_index, %next_total)
exit(%result: i64):
  return %result
}
})";
        check_affine_reduction(biased_source, "sum_biased", {-2, 0, 1, 7, 100}, [](long long limit) {
            long long total = 0;
            for (long long index = 0; index < limit; ++index) total += index + 5;
            return total;
        });

        constexpr auto scaled_source = R"(module @scaled_reduction {
func @sum_scaled(%limit: i64) -> i64 {
entry:
  %zero = const i64 0
  %one = const i64 1
  %scale = const i64 3
  jump loop(%zero, %zero)
loop(%index: i64, %total: i64):
  %done = cmp.ge i64 %index %limit
  branch %done, exit(%total), body(%index, %total)
body(%current: i64, %running: i64):
  %term = mul i64 %current %scale
  %next_total = add i64 %running %term
  %next_index = add i64 %current %one
  jump loop(%next_index, %next_total)
exit(%result: i64):
  return %result
}
})";
        check_affine_reduction(scaled_source, "sum_scaled", {-2, 0, 1, 7, 100}, [](long long limit) {
            long long total = 0;
            for (long long index = 0; index < limit; ++index) total += index * 3;
            return total;
        });

        constexpr auto constant_trip_unroll_source = R"(module @constant_trip_unroll {
func @sum4() -> i64 {
entry:
  %zero = const i64 0
  %one = const i64 1
  %four = const i64 4
  jump loop(%zero, %zero)
loop(%i: i64, %sum: i64):
  %done = cmp.ge i64 %i %four
  branch %done, exit(%sum), body(%i, %sum)
body(%j: i64, %running: i64):
  %next_sum = add i64 %running %j
  %next_i = add i64 %j %one
  jump loop(%next_i, %next_sum)
exit(%result: i64):
  return %result
}
})";
        {
            auto parsed = forge::ir::parse_module(constant_trip_unroll_source);
            require(parsed.ok(), "constant-trip unroll fixture failed to parse");
            auto& candidate = parsed.module->functions().front();
            forge::analysis::FunctionAnalysisManager analyses(candidate);
            forge::transforms::ConstantTripLoopUnrollPass pass;
            require(pass.run(candidate, analyses).changed,
                    "small constant-trip loop was not unrolled");
            const auto optimized = forge::ir::print_module(*parsed.module);
            require(optimized.find("loop(") == std::string::npos &&
                    optimized.find("body(") == std::string::npos &&
                    optimized.find("%unroll.") != std::string::npos,
                    "constant-trip unroll retained the loop CFG");
            const auto verification = forge::ir::verify_module(*parsed.module);
            require(verification.empty(), "constant-trip unroll produced invalid IR");
        }

        constexpr auto redundant_merge_source = R"(module @redundant_merge {
func @same_value(%condition: i1, %value: i64) -> i64 {
entry:
  branch %condition, merge(%value), merge(%value)
merge(%merged: i64):
  return %merged
}
})";
        {
            auto parsed = forge::ir::parse_module(redundant_merge_source);
            require(parsed.ok(), "redundant merge-parameter fixture failed to parse");
            auto& candidate = parsed.module->functions().front();
            forge::analysis::FunctionAnalysisManager analyses(candidate);
            forge::transforms::MergeParameterSimplificationPass pass;
            require(pass.run(candidate, analyses).changed,
                    "identical incoming merge parameter was not simplified");
            const auto optimized = forge::ir::print_module(*parsed.module);
            require(optimized.find("merge(%merged: i64)") == std::string::npos,
                    "redundant merge parameter remained in optimized IR");
            const auto verification = forge::ir::verify_module(*parsed.module);
            require(verification.empty(), "merge-parameter simplification produced invalid IR");
        }


        constexpr auto algebraic_suite_source = R"(module @algebraic_suite {
func @simplify(%value: i64) -> i64 {
entry:
  %one = const i64 1
  %minus_one = const i64 -1
  %sub_self = sub i64 %value %value
  %xor_self = xor i64 %value %value
  %and_self = and i64 %value %value
  %div_one = div.signed i64 %value %one
  %rem_one = rem.unsigned i64 %value %one
  %negated = mul i64 %value %minus_one
  %sum0 = add i64 %sub_self %xor_self
  %sum1 = add i64 %sum0 %rem_one
  %sum2 = add i64 %sum1 %div_one
  %sum3 = add i64 %sum2 %and_self
  %result = add i64 %sum3 %negated
  return %result
}
})";
        {
            auto parsed = forge::ir::parse_module(algebraic_suite_source);
            require(parsed.ok(), "algebraic suite fixture failed to parse");
            auto& candidate = parsed.module->functions().front();
            forge::analysis::FunctionAnalysisManager analyses(candidate);
            forge::transforms::AlgebraicSimplificationPass pass;
            require(pass.run(candidate, analyses).changed,
                    "extended algebraic identities were not simplified");
            const auto optimized = forge::ir::print_module(*parsed.module);
            require(optimized.find("div.signed") == std::string::npos &&
                    optimized.find("rem.unsigned") == std::string::npos &&
                    optimized.find("mul i64 %value %minus_one") == std::string::npos &&
                    optimized.find("neg i64 %value") != std::string::npos,
                    "extended algebraic simplification retained neutral operations");
            const auto verification = forge::ir::verify_module(*parsed.module);
            require(verification.empty(), "extended algebraic simplification produced invalid IR");
        }

        constexpr auto commutative_cse_source = R"(module @commutative_cse {
func @reuse(%left: i64, %right: i64) -> i64 {
entry:
  %first = add i64 %left %right
  %second = add i64 %right %left
  %result = xor i64 %first %second
  return %result
}
})";
        {
            auto parsed = forge::ir::parse_module(commutative_cse_source);
            require(parsed.ok(), "commutative CSE fixture failed to parse");
            auto& candidate = parsed.module->functions().front();
            forge::analysis::FunctionAnalysisManager analyses(candidate);
            forge::transforms::CommonSubexpressionEliminationPass pass;
            require(pass.run(candidate, analyses).changed,
                    "commuted integer expression was not commoned");
            const auto optimized = forge::ir::print_module(*parsed.module);
            require(optimized.find("%second = copy i64 %first") != std::string::npos,
                    "commutative CSE did not reuse the dominating expression");
            const auto verification = forge::ir::verify_module(*parsed.module);
            require(verification.empty(), "commutative CSE produced invalid IR");
        }


        constexpr auto broad_scalar_cleanup_source = R"(module @broad_scalar_cleanup {
func @cleanup(%value: i64, %condition: i1) -> i64 {
entry:
  %minus_one = const i64 -1
  %neg0 = neg i64 %value
  %neg1 = neg i64 %neg0
  %not0 = not i64 %value
  %not1 = not i64 %not0
  %all_bits = and i64 %neg1 %minus_one
  %forced = or i64 %not1 %minus_one
  %result = xor i64 %all_bits %forced
  return %result
}
func @addresses(%base: ptr) -> ptr {
entry:
  %offset0 = ptr.offset ptr %base 8
  %offset1 = ptr.offset ptr %base 8
  return %offset1
}
})";
        {
            auto parsed = forge::ir::parse_module(broad_scalar_cleanup_source);
            require(parsed.ok(), "broad scalar cleanup fixture failed to parse");
            bool algebraic_changed = false;
            bool cse_changed = false;
            for (auto& candidate : parsed.module->functions()) {
                forge::analysis::FunctionAnalysisManager analyses(candidate);
                forge::transforms::AlgebraicSimplificationPass algebraic;
                algebraic_changed = algebraic.run(candidate, analyses).changed || algebraic_changed;
                analyses.invalidate_all();
                forge::transforms::CommonSubexpressionEliminationPass cse;
                cse_changed = cse.run(candidate, analyses).changed || cse_changed;
            }
            require(algebraic_changed,
                    "double unary and all-bits identities were not simplified");
            require(cse_changed,
                    "address/select expressions were not commoned");
            const auto optimized = forge::ir::print_module(*parsed.module);
            require(optimized.find("%neg1 = copy i64 %value") != std::string::npos &&
                    optimized.find("%not1 = copy i64 %value") != std::string::npos &&
                    optimized.find("%offset1 = copy ptr %offset0") != std::string::npos,
                    "broad scalar cleanup did not produce expected canonical copies");
            const auto verification = forge::ir::verify_module(*parsed.module);
            require(verification.empty(), "broad scalar cleanup produced invalid IR");
        }


        constexpr auto predicate_canonicalization_source = R"(module @predicate_canonicalization {
func @canonical(%left: i64, %right: i64) -> i1 {
entry:
  %minus_one = const i64 -1
  %lt = cmp.lt i64 %left %right
  %gt_swapped = cmp.gt i64 %right %left
  %inverted = xor i64 %left %minus_one
  %same = cmp.eq i1 %lt %gt_swapped
  return %same
}
})";
        {
            auto parsed = forge::ir::parse_module(predicate_canonicalization_source);
            require(parsed.ok(), "predicate canonicalization fixture failed to parse");
            auto& candidate = parsed.module->functions().front();
            forge::analysis::FunctionAnalysisManager analyses(candidate);
            forge::transforms::AlgebraicSimplificationPass algebraic;
            require(algebraic.run(candidate, analyses).changed,
                    "all-bits xor was not simplified");
            analyses.invalidate_all();
            forge::transforms::CommonSubexpressionEliminationPass cse;
            require(cse.run(candidate, analyses).changed,
                    "inverse comparison was not canonicalized for CSE");
            const auto optimized = forge::ir::print_module(*parsed.module);
            require(optimized.find("%gt_swapped = copy i1 %lt") != std::string::npos &&
                    optimized.find("%inverted = not i64 %left") != std::string::npos,
                    "predicate canonicalization did not produce expected canonical operations");
            const auto verification = forge::ir::verify_module(*parsed.module);
            require(verification.empty(), "predicate canonicalization produced invalid IR");
        }

        std::cout << "Forge optimization tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "test failure: " << error.what() << '\n';
        return 1;
    }
}
