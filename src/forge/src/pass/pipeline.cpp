// Copyright 2026 Mario Vinciguerra
// SPDX-License-Identifier: Apache-2.0

#include "forge/pass/pipeline.hpp"

#include "forge/transforms/scalar.hpp"

namespace forge::pass {
std::optional<OptimizationLevel> parse_optimization_level(std::string_view text) {
    if (text == "-O0" || text == "O0") return OptimizationLevel::o0;
    if (text == "-O1" || text == "O1") return OptimizationLevel::o1;
    if (text == "-O2" || text == "O2") return OptimizationLevel::o2;
    if (text == "-O3" || text == "O3") return OptimizationLevel::o3;
    if (text == "-Os" || text == "Os") return OptimizationLevel::os;
    if (text == "-Oz" || text == "Oz") return OptimizationLevel::oz;
    return std::nullopt;
}

std::string_view optimization_level_name(OptimizationLevel level) {
    switch (level) {
    case OptimizationLevel::o0: return "O0";
    case OptimizationLevel::o1: return "O1";
    case OptimizationLevel::o2: return "O2";
    case OptimizationLevel::o3: return "O3";
    case OptimizationLevel::os: return "Os";
    case OptimizationLevel::oz: return "Oz";
    }
    return "O0";
}

void build_standard_pipeline(PassManager& pipeline, OptimizationLevel level) {
    if (level == OptimizationLevel::o0) return;
    if (level == OptimizationLevel::o1) {
        // O1 is the fast compile-time tier. Avoid the multi-round SCCP fixpoint
        // used by higher optimization levels; compiler-sized modules need a
        // predictable linear-ish bootstrap path more than exhaustive cleanup.
        pipeline.add<transforms::ConstantFoldPass>()
                .add<transforms::AlgebraicSimplificationPass>()
                .add<transforms::CopyPropagationPass>()
                .add<transforms::DeadCodeEliminationPass>()
                .add<transforms::SimplifyCFGPass>();
        return;
    }
    pipeline.add<transforms::SparseConditionalConstantPropagationPass>()
            .add<transforms::AlgebraicSimplificationPass>()
            .add<transforms::CopyPropagationPass>()
            .add<transforms::DeadCodeEliminationPass>();
    pipeline.add<transforms::LoopReductionPass>()
            .add<transforms::LoopInvariantGuardHoistingPass>()
            .add<transforms::CommonSubexpressionEliminationPass>()
            .add<transforms::ScalarStackPromotionPass>()
            .add<transforms::MergeParameterSimplificationPass>()
            .add<transforms::MemoryForwardingPass>()
            .add<transforms::DeadStoreEliminationPass>()
            .add<transforms::IfConversionPass>()
            .add<transforms::MergeParameterSimplificationPass>()
            .add<transforms::SparseConditionalConstantPropagationPass>()
            .add<transforms::AlgebraicSimplificationPass>()
            .add<transforms::CommonSubexpressionEliminationPass>()
            .add<transforms::CopyPropagationPass>()
            .add<transforms::DeadCodeEliminationPass>()
            .add<transforms::BranchThreadingPass>()
            .add<transforms::SimplifyCFGPass>()
            .add<transforms::ScalarCleanupFixpointPass>();
    if (level == OptimizationLevel::oz) {
        pipeline.add<transforms::CopyPropagationPass>()
                .add<transforms::DeadCodeEliminationPass>()
                .add<transforms::SimplifyCFGPass>();
        return;
    }

    if (level == OptimizationLevel::os) {
        pipeline.add<transforms::SparseConditionalConstantPropagationPass>()
                .add<transforms::DeadCodeEliminationPass>()
                .add<transforms::SimplifyCFGPass>();
        return;
    }

    if (level == OptimizationLevel::o3) {
        pipeline.add<transforms::SparseConditionalConstantPropagationPass>()
                .add<transforms::LoopReductionPass>()
                .add<transforms::LoopInvariantGuardHoistingPass>()
                .add<transforms::ConstantTripLoopUnrollPass>()
                .add<transforms::LoopInvariantCodeMotionPass>()
                .add<transforms::CommonSubexpressionEliminationPass>()
                .add<transforms::ScalarStackPromotionPass>()
                .add<transforms::MergeParameterSimplificationPass>()
                .add<transforms::MemoryForwardingPass>()
                .add<transforms::DeadStoreEliminationPass>()
                .add<transforms::IfConversionPass>()
                .add<transforms::MergeParameterSimplificationPass>()
                .add<transforms::SparseConditionalConstantPropagationPass>()
                .add<transforms::AlgebraicSimplificationPass>()
                .add<transforms::CommonSubexpressionEliminationPass>()
                .add<transforms::CopyPropagationPass>()
                .add<transforms::DeadCodeEliminationPass>()
                .add<transforms::BranchThreadingPass>()
                .add<transforms::SimplifyCFGPass>()
                .add<transforms::ScalarCleanupFixpointPass>();
    }
}
} // namespace forge::pass
