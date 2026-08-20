// Copyright 2026 Mario Vinciguerra
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "forge/pass/pass.hpp"

namespace forge::transforms {
class ConstantFoldPass final : public pass::FunctionPass {
public:
    std::string name() const override { return "constant-fold"; }
    pass::PassResult run(ir::Function&, analysis::FunctionAnalysisManager&) override;
};
class CopyPropagationPass final : public pass::FunctionPass {
public:
    std::string name() const override { return "copy-propagation"; }
    pass::PassResult run(ir::Function&, analysis::FunctionAnalysisManager&) override;
};
class BranchFoldPass final : public pass::FunctionPass {
public:
    std::string name() const override { return "branch-fold"; }
    pass::PassResult run(ir::Function&, analysis::FunctionAnalysisManager&) override;
};
class AlgebraicSimplificationPass final : public pass::FunctionPass {
public:
    std::string name() const override { return "algebraic-simplification"; }
    pass::PassResult run(ir::Function&, analysis::FunctionAnalysisManager&) override;
};
class CommonSubexpressionEliminationPass final : public pass::FunctionPass {
public:
    std::string name() const override { return "cse"; }
    pass::PassResult run(ir::Function&, analysis::FunctionAnalysisManager&) override;
};
class SparseConditionalConstantPropagationPass final : public pass::FunctionPass {
public:
    std::string name() const override { return "sccp"; }
    pass::PassResult run(ir::Function&, analysis::FunctionAnalysisManager&) override;
};

class ScalarStackPromotionPass final : public pass::FunctionPass {
public:
    std::string name() const override { return "scalar-stack-promotion"; }
    pass::PassResult run(ir::Function&, analysis::FunctionAnalysisManager&) override;
};
class MemoryForwardingPass final : public pass::FunctionPass {
public:
    std::string name() const override { return "memory-forwarding"; }
    pass::PassResult run(ir::Function&, analysis::FunctionAnalysisManager&) override;
};
class DeadStoreEliminationPass final : public pass::FunctionPass {
public:
    std::string name() const override { return "dead-store-elimination"; }
    pass::PassResult run(ir::Function&, analysis::FunctionAnalysisManager&) override;
};
class LoopReductionPass final : public pass::FunctionPass {
public:
    std::string name() const override { return "loop-reduction"; }
    pass::PassResult run(ir::Function&, analysis::FunctionAnalysisManager&) override;
};
class ConstantTripLoopUnrollPass final : public pass::FunctionPass {
public:
    std::string name() const override { return "constant-trip-loop-unroll"; }
    pass::PassResult run(ir::Function&, analysis::FunctionAnalysisManager&) override;
};
class LoopInvariantCodeMotionPass final : public pass::FunctionPass {
public:
    std::string name() const override { return "licm"; }
    pass::PassResult run(ir::Function&, analysis::FunctionAnalysisManager&) override;
};
class LoopInvariantGuardHoistingPass final : public pass::FunctionPass {
public:
    std::string name() const override { return "loop-invariant-guard-hoisting"; }
    pass::PassResult run(ir::Function&, analysis::FunctionAnalysisManager&) override;
};
class DeadCodeEliminationPass final : public pass::FunctionPass {
public:
    std::string name() const override { return "dce"; }
    pass::PassResult run(ir::Function&, analysis::FunctionAnalysisManager&) override;
};
class IfConversionPass final : public pass::FunctionPass {
public:
    std::string name() const override { return "if-conversion"; }
    pass::PassResult run(ir::Function&, analysis::FunctionAnalysisManager&) override;
};
class MergeParameterSimplificationPass final : public pass::FunctionPass {
public:
    std::string name() const override { return "merge-parameter-simplification"; }
    pass::PassResult run(ir::Function&, analysis::FunctionAnalysisManager&) override;
};
class BranchThreadingPass final : public pass::FunctionPass {
public:
    std::string name() const override { return "branch-threading"; }
    pass::PassResult run(ir::Function&, analysis::FunctionAnalysisManager&) override;
};
class SimplifyCFGPass final : public pass::FunctionPass {
public:
    std::string name() const override { return "simplify-cfg"; }
    pass::PassResult run(ir::Function&, analysis::FunctionAnalysisManager&) override;
};
class ScalarCleanupFixpointPass final : public pass::FunctionPass {
public:
    explicit ScalarCleanupFixpointPass(std::size_t max_iterations = 4)
        : max_iterations_(max_iterations) {}
    std::string name() const override { return "scalar-cleanup-fixpoint"; }
    pass::PassResult run(ir::Function&, analysis::FunctionAnalysisManager&) override;
private:
    std::size_t max_iterations_{};
};
} // namespace forge::transforms
