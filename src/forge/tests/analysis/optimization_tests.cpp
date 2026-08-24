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


        // SCCP must reason through SSA block parameters, not only constants
        // defined by operations in the same block.  Both executable incoming
        // paths feed the same constant through distinct block parameters; the
        // join parameter is therefore constant even though the first branch is
        // dynamic.  That fact should prune the second branch for either input.
        constexpr auto sccp_parameter_source = R"(module @sccp_parameter {
func @through_phi(%choose: i1) -> i32 {
entry:
  %one = const i1 1
  branch %choose, left(%one), right(%one)
left(%left_value: i1):
  jump join(%left_value)
right(%right_value: i1):
  jump join(%right_value)
join(%flag: i1):
  branch %flag, live(), dead()
live:
  %answer = const i32 42
  return %answer
dead:
  %wrong = const i32 0
  return %wrong
}
})";
        auto sccp_parameter = forge::ir::parse_module(sccp_parameter_source);
        require(sccp_parameter.ok(), "SCCP block-parameter fixture failed to parse");
        for (long long choose : {0LL, 1LL}) {
            const std::vector<forge::interpreter::Value> phi_arguments{
                forge::interpreter::Value::integer(forge::ir::i1_type(), choose)};
            const auto before_phi = forge::interpreter::execute(*sccp_parameter.module, "through_phi", phi_arguments);
            require(before_phi.value.has_value() && before_phi.value->signed_value() == 42,
                    "SCCP block-parameter pre-optimization result mismatch");
        }
        forge::pass::PassManager sccp_parameter_pipeline;
        sccp_parameter_pipeline.add<forge::transforms::SparseConditionalConstantPropagationPass>()
                               .add<forge::transforms::DeadCodeEliminationPass>()
                               .add<forge::transforms::SimplifyCFGPass>();
        const auto sccp_parameter_stats = sccp_parameter_pipeline.run(*sccp_parameter.module);
        require(sccp_parameter_stats.changed, "SSA-aware SCCP reported no change");
        const auto sccp_parameter_text = forge::ir::print_module(*sccp_parameter.module);
        require(sccp_parameter_text.find("dead(") == std::string::npos,
                "SSA-aware SCCP did not prune branch controlled by constant block parameter");
        require(sccp_parameter_text.find("join(%flag") == std::string::npos,
                "SSA-aware SCCP did not materialize constant block parameter");
        const auto sccp_parameter_diagnostics = forge::ir::verify_module(*sccp_parameter.module);
        for (const auto& diagnostic : sccp_parameter_diagnostics)
            require(diagnostic.severity != forge::DiagnosticSeverity::error,
                    "SSA-aware SCCP produced invalid IR");
        for (long long choose : {0LL, 1LL}) {
            const std::vector<forge::interpreter::Value> phi_arguments{
                forge::interpreter::Value::integer(forge::ir::i1_type(), choose)};
            const auto after_phi = forge::interpreter::execute(*sccp_parameter.module, "through_phi", phi_arguments);
            require(after_phi.value.has_value() && after_phi.value->signed_value() == 42,
                    "SSA-aware SCCP changed block-parameter semantics");
        }


        // Comparison operations carry their operand type in textual Forge IR,
        // but their SSA result is always i1.  Any pass that folds a comparison
        // to `const` must therefore retag the operation as i1; otherwise a
        // subsequent branch/return sees an invalid integer-width result.  This
        // was exposed by the Raz self-host compiler at -O2.
        constexpr auto comparison_fold_source = R"(module @comparison_fold {
func @sccp_compare() -> i1 {
entry:
  %one = const i64 1
  %zero = const i64 0
  %different = cmp.ne i64 %one %zero
  return %different
}
func @algebraic_compare(%value: i64) -> i1 {
entry:
  %same = cmp.eq i64 %value %value
  return %same
}
})";
        {
            auto comparison_fold = forge::ir::parse_module(comparison_fold_source);
            require(comparison_fold.ok(), "comparison-fold fixture failed to parse");
            auto& sccp_candidate = comparison_fold.module->functions()[0];
            forge::analysis::FunctionAnalysisManager sccp_analyses(sccp_candidate);
            forge::transforms::SparseConditionalConstantPropagationPass sccp;
            require(sccp.run(sccp_candidate, sccp_analyses).changed,
                    "SCCP did not fold constant comparison");
            auto& algebraic_candidate = comparison_fold.module->functions()[1];
            forge::analysis::FunctionAnalysisManager algebraic_analyses(algebraic_candidate);
            forge::transforms::AlgebraicSimplificationPass algebraic;
            require(algebraic.run(algebraic_candidate, algebraic_analyses).changed,
                    "algebraic simplification did not fold same-value comparison");
            const auto optimized = forge::ir::print_module(*comparison_fold.module);
            require(optimized.find("%different = const i1 1") != std::string::npos,
                    "SCCP comparison fold lost i1 result type");
            require(optimized.find("%same = const i1 1") != std::string::npos,
                    "algebraic comparison fold lost i1 result type");
            const auto verification = forge::ir::verify_module(*comparison_fold.module);
            require(verification.empty(), "comparison folding produced invalid IR");
        }



        // SCCP must conservatively classify unmodelled result-producing
        // operations as overdefined even when their textual operand vector has
        // length two.  A one-argument call is represented as {callee, arg}; an
        // older evaluator accidentally treated that shape like a binary scalar
        // expression, left the result at lattice bottom, and then allowed a
        // constant from another executable block-parameter edge to dominate the
        // meet.  The resulting -O2 IR pruned the live success path in Raz's
        // project output-path preparation.
        constexpr auto opaque_call_phi_source = R"(module @opaque_call_phi {
extern func @opaque(%x: i64) -> i64
func @preserve_dynamic_edge(%choose: i1) -> i64 {
entry:
  %zero = const i64 0
  %one = const i64 1
  branch %choose, first(), second()
first:
  jump join(%one)
second:
  %value = call i64 @opaque(%one)
  %iszero = cmp.eq i64 %value %zero
  %dynamic = select i64 %iszero %one %zero
  jump join(%dynamic)
join(%flag: i64):
  %failed = cmp.ne i64 %flag %zero
  branch %failed, failure(), success()
failure:
  return %zero
success:
  return %one
}
})";
        {
            auto opaque_call_phi = forge::ir::parse_module(opaque_call_phi_source);
            require(opaque_call_phi.ok(), "opaque-call SCCP fixture failed to parse");
            forge::pass::PassManager opaque_call_pipeline;
            opaque_call_pipeline.add<forge::transforms::SparseConditionalConstantPropagationPass>()
                                .add<forge::transforms::AlgebraicSimplificationPass>()
                                .add<forge::transforms::CopyPropagationPass>()
                                .add<forge::transforms::DeadCodeEliminationPass>()
                                .add<forge::transforms::SparseConditionalConstantPropagationPass>()
                                .add<forge::transforms::SimplifyCFGPass>();
            (void)opaque_call_pipeline.run(*opaque_call_phi.module);
            const auto optimized = forge::ir::print_module(*opaque_call_phi.module);
            require(optimized.find("call i64 @opaque") != std::string::npos,
                    "SCCP incorrectly discarded dynamic call path");
            require(optimized.find("failure") != std::string::npos &&
                        optimized.find("success") != std::string::npos,
                    "SCCP incorrectly constant-folded block parameter fed by opaque call");
            const auto verification = forge::ir::verify_module(*opaque_call_phi.module);
            require(verification.empty(), "opaque-call SCCP regression produced invalid IR");
        }


        // Edge-sensitive SCCP regression: the entry choice is dynamic, so both
        // left and right blocks are executable.  The right block nevertheless
        // has a constant-false edge to `impossible`, which means only `left`
        // can actually reach `join`.  SCCP must meet block-parameter values over
        // executable edges only, prove %flag == 1, prune `dead`, and then CFG
        // simplification should collapse the now-single-predecessor join.
        constexpr auto edge_sensitive_source = R"(module @edge_sensitive {
func @edge_sensitive(%choose: i1) -> i32 {
entry:
  %one = const i1 1
  %zero = const i1 0
  branch %choose, left(), right()
left:
  jump join(%one)
right:
  branch %zero, impossible(%zero), bypass()
impossible(%wrong_flag: i1):
  jump join(%wrong_flag)
bypass:
  %seven = const i32 7
  return %seven
join(%flag: i1):
  branch %flag, live(), dead()
live:
  %answer = const i32 42
  return %answer
dead:
  %wrong = const i32 0
  return %wrong
}
})";
        auto edge_sensitive = forge::ir::parse_module(edge_sensitive_source);
        require(edge_sensitive.ok(), "edge-sensitive SCCP fixture failed to parse");
        for (long long choose : {0LL, 1LL}) {
            const std::vector<forge::interpreter::Value> edge_arguments{
                forge::interpreter::Value::integer(forge::ir::i1_type(), choose)};
            const auto before_edge = forge::interpreter::execute(*edge_sensitive.module, "edge_sensitive", edge_arguments);
            const long long expected = choose != 0 ? 42 : 7;
            require(before_edge.value.has_value() && before_edge.value->signed_value() == expected,
                    "edge-sensitive SCCP pre-optimization result mismatch");
        }
        forge::pass::PassManager edge_sensitive_pipeline;
        edge_sensitive_pipeline.add<forge::transforms::SparseConditionalConstantPropagationPass>()
                               .add<forge::transforms::DeadCodeEliminationPass>()
                               .add<forge::transforms::SimplifyCFGPass>();
        const auto edge_sensitive_stats = edge_sensitive_pipeline.run(*edge_sensitive.module);
        require(edge_sensitive_stats.changed, "edge-sensitive SCCP/CFG reported no change");
        const auto edge_sensitive_text = forge::ir::print_module(*edge_sensitive.module);
        require(edge_sensitive_text.find("impossible") == std::string::npos,
                "edge-sensitive SCCP retained an impossible predecessor");
        require(edge_sensitive_text.find("dead") == std::string::npos,
                "edge-sensitive SCCP retained a branch made impossible by the phi fact");
        require(edge_sensitive_text.find("join(") == std::string::npos,
                "CFG simplification did not collapse a single-predecessor join");
        const auto edge_sensitive_diagnostics = forge::ir::verify_module(*edge_sensitive.module);
        for (const auto& diagnostic : edge_sensitive_diagnostics)
            require(diagnostic.severity != forge::DiagnosticSeverity::error,
                    "edge-sensitive SCCP/CFG produced invalid IR");
        for (long long choose : {0LL, 1LL}) {
            const std::vector<forge::interpreter::Value> edge_arguments{
                forge::interpreter::Value::integer(forge::ir::i1_type(), choose)};
            const auto after_edge = forge::interpreter::execute(*edge_sensitive.module, "edge_sensitive", edge_arguments);
            const long long expected = choose != 0 ? 42 : 7;
            require(after_edge.value.has_value() && after_edge.value->signed_value() == expected,
                    "edge-sensitive SCCP/CFG changed program semantics");
        }


        // Jump-threading regression: both edges enter the same tiny predicate
        // block with different constant arguments.  The branch itself is
        // dynamic, so ordinary SCCP cannot collapse it globally.  Threading
        // should specialize the predicate independently for each incoming
        // edge, duplicate only the two pure scalar operations, and bypass the
        // shared control block without changing either runtime result.
        constexpr auto branch_threading_source = R"(module @branch_threading {
func @thread(%choose: i1) -> i32 {
entry:
  %one = const i32 1
  %zero = const i32 0
  branch %choose, test(%one), test(%zero)
test(%value: i32):
  %expected = const i32 1
  %matches = cmp.eq i32 %value %expected
  branch %matches, live(), bypass()
live:
  %answer = const i32 42
  return %answer
bypass:
  %seven = const i32 7
  return %seven
}
})";
        auto branch_threading = forge::ir::parse_module(branch_threading_source);
        require(branch_threading.ok(), "branch-threading fixture failed to parse");
        for (long long choose : {0LL, 1LL}) {
            const std::vector<forge::interpreter::Value> threading_arguments{
                forge::interpreter::Value::integer(forge::ir::i1_type(), choose)};
            const auto before_threading = forge::interpreter::execute(
                *branch_threading.module, "thread", threading_arguments);
            const long long expected = choose != 0 ? 42 : 7;
            require(before_threading.value.has_value() &&
                        before_threading.value->signed_value() == expected,
                    "branch-threading pre-optimization result mismatch");
        }
        forge::pass::PassManager branch_threading_pipeline;
        branch_threading_pipeline.add<forge::transforms::BranchThreadingPass>()
                                 .add<forge::transforms::DeadCodeEliminationPass>()
                                 .add<forge::transforms::SimplifyCFGPass>();
        const auto branch_threading_stats = branch_threading_pipeline.run(*branch_threading.module);
        require(branch_threading_stats.changed, "branch threading reported no change");
        const auto branch_threading_text = forge::ir::print_module(*branch_threading.module);
        require(branch_threading_text.find("test(") == std::string::npos,
                "branch threading did not bypass the specialized predicate block");
        const auto branch_threading_diagnostics = forge::ir::verify_module(*branch_threading.module);
        for (const auto& diagnostic : branch_threading_diagnostics)
            require(diagnostic.severity != forge::DiagnosticSeverity::error,
                    "branch threading produced invalid IR");
        for (long long choose : {0LL, 1LL}) {
            const std::vector<forge::interpreter::Value> threading_arguments{
                forge::interpreter::Value::integer(forge::ir::i1_type(), choose)};
            const auto after_threading = forge::interpreter::execute(
                *branch_threading.module, "thread", threading_arguments);
            const long long expected = choose != 0 ? 42 : 7;
            require(after_threading.value.has_value() &&
                        after_threading.value->signed_value() == expected,
                    "branch threading changed program semantics");
        }


        // Legality regression: a control block with an observable/trapping
        // operation must never be duplicated just because one incoming edge
        // carries a constant.  Keep this shared load block intact.
        constexpr auto branch_threading_unsafe_source = R"(module @branch_threading_unsafe {
func @thread_unsafe(%choose: i1, %ptr: ptr) -> i32 {
entry:
  %one = const i32 1
  %zero = const i32 0
  branch %choose, test(%one, %ptr), test(%zero, %ptr)
test(%value: i32, %source: ptr):
  %loaded = load i32 %source align 4
  %matches = cmp.eq i32 %value %loaded
  branch %matches, live(), bypass()
live:
  %answer = const i32 42
  return %answer
bypass:
  %seven = const i32 7
  return %seven
}
})";
        auto branch_threading_unsafe = forge::ir::parse_module(branch_threading_unsafe_source);
        require(branch_threading_unsafe.ok(), "unsafe branch-threading fixture failed to parse");
        auto& unsafe_function = branch_threading_unsafe.module->functions().front();
        forge::analysis::FunctionAnalysisManager unsafe_analyses(unsafe_function);
        forge::transforms::BranchThreadingPass unsafe_threading;
        const auto unsafe_stats = unsafe_threading.run(unsafe_function, unsafe_analyses);
        require(!unsafe_stats.changed,
                "branch threading duplicated a load-containing predicate block");
        require(forge::ir::print_module(*branch_threading_unsafe.module).find("test(") != std::string::npos,
                "unsafe branch-threading fixture unexpectedly lost its shared block");

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
        require(loop_mem2reg_stats.changed,
                "dominance-frontier mem2reg did not promote loop-carried stack state");
        const auto loop_mem2reg_text = forge::ir::print_module(*loop_mem2reg.module);
        require(loop_mem2reg_text.find("stack.alloc") == std::string::npos,
                "loop-safe mem2reg left the promoted allocation in IR");
        require(loop_mem2reg_text.find("load i64") == std::string::npos,
                "loop-safe mem2reg left promoted loads in IR");
        require(loop_mem2reg_text.find("store i64") == std::string::npos,
                "loop-safe mem2reg left promoted stores in IR");
        require(loop_mem2reg_text.find("%mem2reg.slot.header") != std::string::npos,
                "loop-safe mem2reg did not place a loop-header parameter");
        for (const auto limit : {0LL, 1LL, 5LL, 10LL}) {
            const std::vector<forge::interpreter::Value> arguments{
                forge::interpreter::Value::integer(forge::ir::i64_type(), limit)};
            const auto execution = forge::interpreter::execute(*loop_mem2reg.module, "sum_loop", arguments);
            const auto expected = limit * (limit - 1) / 2;
            require(execution.value.has_value() && execution.value->signed_value() == expected,
                    "loop mem2reg changed counted-loop semantics");
        }

        constexpr auto conditional_loop_mem2reg_source = R"(module @conditional_loop_mem2reg {
func @conditional_sum(%limit: i64) -> i64 {
entry:
  %slot = stack.alloc ptr 8 align 8
  %zero = const i64 0
  %one = const i64 1
  %two = const i64 2
  store i64 %zero %slot align 8
  jump header(%zero)
header(%index: i64):
  %again = cmp.lt i64 %index %limit
  branch %again, body(%index), exit()
body(%current_index: i64):
  %do_update = cmp.lt i64 %current_index %two
  branch %do_update, update(%current_index), skip(%current_index)
update(%update_index: i64):
  %current = load i64 %slot align 8
  %next_total = add i64 %current %update_index
  store i64 %next_total %slot align 8
  jump latch(%update_index)
skip(%skip_index: i64):
  jump latch(%skip_index)
latch(%latch_index: i64):
  %next_index = add i64 %latch_index %one
  jump header(%next_index)
exit:
  %result = load i64 %slot align 8
  return %result
}
})";
        auto conditional_loop_mem2reg = forge::ir::parse_module(conditional_loop_mem2reg_source);
        require(conditional_loop_mem2reg.ok(), "conditional loop mem2reg fixture failed to parse");
        forge::pass::PassManager conditional_loop_mem2reg_pipeline;
        conditional_loop_mem2reg_pipeline.add<forge::transforms::ScalarStackPromotionPass>()
                                         .add<forge::transforms::CopyPropagationPass>()
                                         .add<forge::transforms::DeadCodeEliminationPass>();
        const auto conditional_loop_stats =
            conditional_loop_mem2reg_pipeline.run(*conditional_loop_mem2reg.module);
        require(conditional_loop_stats.changed,
                "dominance-frontier mem2reg did not promote conditional loop state");
        const auto conditional_loop_text = forge::ir::print_module(*conditional_loop_mem2reg.module);
        require(conditional_loop_text.find("stack.alloc") == std::string::npos,
                "conditional loop mem2reg left the promoted allocation in IR");
        require(conditional_loop_text.find("%mem2reg.slot.header") != std::string::npos,
                "conditional loop mem2reg did not place the loop-header parameter");
        require(conditional_loop_text.find("%mem2reg.slot.latch") != std::string::npos,
                "conditional loop mem2reg did not place the conditional-merge parameter");
        for (const auto limit : {0LL, 1LL, 2LL, 6LL, 9LL}) {
            const std::vector<forge::interpreter::Value> arguments{
                forge::interpreter::Value::integer(forge::ir::i64_type(), limit)};
            const auto execution = forge::interpreter::execute(
                *conditional_loop_mem2reg.module, "conditional_sum", arguments);
            long long expected = 0;
            for (long long index = 0; index < limit; ++index)
                if (index < 2) expected += index;
            require(execution.value.has_value() && execution.value->signed_value() == expected,
                    "conditional loop mem2reg changed semantics");
        }

        constexpr auto nested_loop_mem2reg_source = R"(module @nested_loop_mem2reg {
func @nested_sum(%outer_limit: i64, %inner_limit: i64) -> i64 {
entry:
  %slot = stack.alloc ptr 8 align 8
  %zero = const i64 0
  %one = const i64 1
  store i64 %zero %slot align 8
  jump outer_header(%zero)
outer_header(%outer: i64):
  %outer_again = cmp.lt i64 %outer %outer_limit
  branch %outer_again, inner_entry(%outer), exit()
inner_entry(%outer_value: i64):
  jump inner_header(%outer_value, %zero)
inner_header(%saved_outer: i64, %inner: i64):
  %inner_again = cmp.lt i64 %inner %inner_limit
  branch %inner_again, inner_body(%saved_outer, %inner), outer_latch(%saved_outer)
inner_body(%body_outer: i64, %body_inner: i64):
  %current = load i64 %slot align 8
  %with_outer = add i64 %current %body_outer
  %next_total = add i64 %with_outer %body_inner
  store i64 %next_total %slot align 8
  %next_inner = add i64 %body_inner %one
  jump inner_header(%body_outer, %next_inner)
outer_latch(%latch_outer: i64):
  %next_outer = add i64 %latch_outer %one
  jump outer_header(%next_outer)
exit:
  %result = load i64 %slot align 8
  return %result
}
})";
        auto nested_loop_mem2reg = forge::ir::parse_module(nested_loop_mem2reg_source);
        require(nested_loop_mem2reg.ok(), "nested loop mem2reg fixture failed to parse");
        forge::pass::PassManager nested_loop_mem2reg_pipeline;
        nested_loop_mem2reg_pipeline.add<forge::transforms::ScalarStackPromotionPass>()
                                    .add<forge::transforms::CopyPropagationPass>()
                                    .add<forge::transforms::DeadCodeEliminationPass>();
        const auto nested_loop_stats = nested_loop_mem2reg_pipeline.run(*nested_loop_mem2reg.module);
        require(nested_loop_stats.changed,
                "dominance-frontier mem2reg did not promote nested-loop state");
        const auto nested_loop_text = forge::ir::print_module(*nested_loop_mem2reg.module);
        require(nested_loop_text.find("stack.alloc") == std::string::npos,
                "nested-loop mem2reg left the promoted allocation in IR");
        require(nested_loop_text.find("%mem2reg.slot.inner_header") != std::string::npos,
                "nested-loop mem2reg did not place the inner-loop parameter");
        require(nested_loop_text.find("%mem2reg.slot.outer_header") != std::string::npos,
                "nested-loop mem2reg did not place the outer-loop parameter");
        for (const auto outer_limit : {0LL, 1LL, 3LL}) {
            for (const auto inner_limit : {0LL, 1LL, 4LL}) {
                const std::vector<forge::interpreter::Value> arguments{
                    forge::interpreter::Value::integer(forge::ir::i64_type(), outer_limit),
                    forge::interpreter::Value::integer(forge::ir::i64_type(), inner_limit)};
                const auto execution = forge::interpreter::execute(
                    *nested_loop_mem2reg.module, "nested_sum", arguments);
                long long expected = 0;
                for (long long outer = 0; outer < outer_limit; ++outer)
                    for (long long inner = 0; inner < inner_limit; ++inner)
                        expected += outer + inner;
                require(execution.value.has_value() && execution.value->signed_value() == expected,
                        "nested-loop mem2reg changed semantics");
            }
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

        constexpr auto invariant_guard_source = R"(module @loop_guard {
func @guarded_loop(%enabled: i1, %limit: i64) -> i64 {
entry:
  %zero = const i64 0
  %one = const i64 1
  jump header(%zero)
header(%index: i64):
  branch %enabled, body(%index), exit(%index)
body(%current: i64):
  %next = add i64 %current %one
  %again = cmp.lt i64 %next %limit
  branch %again, header(%next), exit(%next)
exit(%result: i64):
  return %result
}
})";
        auto invariant_guard = forge::ir::parse_module(invariant_guard_source);
        require(invariant_guard.ok(), "loop-invariant guard fixture failed to parse");
        auto& guarded_function = invariant_guard.module->functions().front();
        forge::analysis::FunctionAnalysisManager guarded_analyses(guarded_function);
        forge::transforms::LoopInvariantGuardHoistingPass guard_hoisting;
        const auto guard_stats = guard_hoisting.run(guarded_function, guarded_analyses);
        require(guard_stats.changed && guard_stats.operations_rewritten == 2,
                "loop-invariant guard was not hoisted");
        const auto guarded_diagnostics = forge::ir::verify_module(*invariant_guard.module);
        require(std::none_of(guarded_diagnostics.begin(), guarded_diagnostics.end(), [](const auto& diagnostic) {
            return diagnostic.severity == forge::DiagnosticSeverity::error;
        }), "loop-invariant guard hoisting produced invalid IR");
        const auto guarded_text = forge::ir::print_module(*invariant_guard.module);
        require(guarded_text.find("branch %enabled, header(%zero), exit(%zero)") != std::string::npos,
                "loop-invariant guard was not moved to the preheader");
        require(guarded_text.find("header(%index: i64):\n    jump body(%index)") != std::string::npos,
                "loop header still re-tests the invariant condition");
        for (const auto enabled : {0LL, 1LL}) {
            for (const auto limit : {1LL, 4LL}) {
                const std::vector<forge::interpreter::Value> arguments{
                    forge::interpreter::Value::integer(forge::ir::i1_type(), enabled),
                    forge::interpreter::Value::integer(forge::ir::i64_type(), limit)};
                const auto execution = forge::interpreter::execute(
                    *invariant_guard.module, "guarded_loop", arguments);
                const long long expected = enabled == 0 ? 0 : limit;
                require(execution.value.has_value() && execution.value->signed_value() == expected,
                        "loop-invariant guard hoisting changed semantics");
            }
        }

        constexpr auto loop_variant_guard_source = R"(module @loop_variant_guard {
func @variant_guard(%limit: i64) -> i64 {
entry:
  %zero = const i64 0
  %one = const i64 1
  jump header(%zero)
header(%index: i64):
  %keep_going = cmp.lt i64 %index %limit
  branch %keep_going, body(%index), exit(%index)
body(%current: i64):
  %next = add i64 %current %one
  jump header(%next)
exit(%result: i64):
  return %result
}
})";
        auto loop_variant_guard = forge::ir::parse_module(loop_variant_guard_source);
        require(loop_variant_guard.ok(), "loop-variant guard fixture failed to parse");
        auto& variant_function = loop_variant_guard.module->functions().front();
        forge::analysis::FunctionAnalysisManager variant_analyses(variant_function);
        const auto variant_stats = guard_hoisting.run(variant_function, variant_analyses);
        require(!variant_stats.changed,
                "loop-invariant guard hoisting rewrote a loop-variant predicate");

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



        constexpr auto trivial_loop_phi_source = R"(module @trivial_loop_phi {
func @choose(%condition: i1, %value: i64) -> i64 {
entry:
  jump header(%value)
header(%current: i64):
  branch %condition, header(%current), exit(%current)
exit(%result: i64):
  return %result
}
})";
        {
            auto parsed = forge::ir::parse_module(trivial_loop_phi_source);
            require(parsed.ok(), "trivial loop-phi fixture failed to parse");
            auto& candidate = parsed.module->functions().front();
            forge::analysis::FunctionAnalysisManager analyses(candidate);
            forge::transforms::MergeParameterSimplificationPass pass;
            require(pass.run(candidate, analyses).changed,
                    "self-referential loop phi was not simplified");
            const auto optimized = forge::ir::print_module(*parsed.module);
            require(optimized.find("header(%current: i64)") == std::string::npos,
                    "trivial loop phi remained after SSA simplification");
            require(optimized.find("exit(%result: i64)") == std::string::npos,
                    "phi simplification did not reach a fixed point");
            const auto verification = forge::ir::verify_module(*parsed.module);
            require(verification.empty(), "loop-phi simplification produced invalid IR");
            const std::vector<forge::interpreter::Value> arguments{
                forge::interpreter::Value::integer(forge::ir::i1_type(), 0),
                forge::interpreter::Value::integer(forge::ir::i64_type(), 73)};
            const auto execution = forge::interpreter::execute(*parsed.module, "choose", arguments);
            require(execution.value.has_value() && execution.value->signed_value() == 73,
                    "loop-phi simplification changed semantics");
        }

        constexpr auto phi_chain_source = R"(module @phi_chain {
func @forward(%condition: i1, %value: i64) -> i64 {
entry:
  branch %condition, left(%value), right(%value)
left(%left_value: i64):
  jump merge(%left_value)
right(%right_value: i64):
  jump merge(%right_value)
merge(%merged: i64):
  return %merged
}
})";
        {
            auto parsed = forge::ir::parse_module(phi_chain_source);
            require(parsed.ok(), "phi forwarding fixture failed to parse");
            auto& candidate = parsed.module->functions().front();
            forge::analysis::FunctionAnalysisManager analyses(candidate);
            forge::transforms::MergeParameterSimplificationPass pass;
            require(pass.run(candidate, analyses).changed,
                    "phi-of-phi forwarding did not simplify");
            const auto optimized = forge::ir::print_module(*parsed.module);
            require(optimized.find("%left_value") == std::string::npos &&
                    optimized.find("%right_value") == std::string::npos &&
                    optimized.find("%merged") == std::string::npos,
                    "phi forwarding left redundant block parameters behind");
            const auto verification = forge::ir::verify_module(*parsed.module);
            require(verification.empty(), "phi forwarding produced invalid IR");
            for (const auto condition : {0LL, 1LL}) {
                const std::vector<forge::interpreter::Value> arguments{
                    forge::interpreter::Value::integer(forge::ir::i1_type(), condition),
                    forge::interpreter::Value::integer(forge::ir::i64_type(), 19)};
                const auto execution = forge::interpreter::execute(*parsed.module, "forward", arguments);
                require(execution.value.has_value() && execution.value->signed_value() == 19,
                        "phi forwarding changed branch semantics");
            }
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
