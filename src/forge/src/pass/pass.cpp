// Copyright 2026 Mario Vinciguerra
// SPDX-License-Identifier: Apache-2.0

#include "forge/pass/pass.hpp"

#include <stdexcept>

#include "forge/ir/verifier.hpp"

namespace forge::pass {
namespace {
void verify_or_throw(const ir::Module& module, const std::string& pass_name) {
    const auto diagnostics = ir::verify_module(module);
    for (const auto& diagnostic : diagnostics) {
        if (diagnostic.severity == DiagnosticSeverity::error)
            throw std::runtime_error("verification failed after " + pass_name + ": " + diagnostic.message);
    }
}
}

PassRunReport PassManager::run_with_report(ir::Module& module, bool verify_each) const {
    PassRunReport report;
    for (auto& function : module.functions()) {
        analysis::FunctionAnalysisManager analyses(function);
        for (const auto& pass : passes_) {
            const auto start = std::chrono::steady_clock::now();
            auto result = pass->run(function, analyses);
            const auto elapsed = std::chrono::steady_clock::now() - start;
            report.total += result;
            report.records.push_back({function.name, pass->name(), result,
                                      std::chrono::duration_cast<std::chrono::nanoseconds>(elapsed)});
            if (result.changed) {
                // New passes default to conservative invalidation until they
                // explicitly describe their mutation scope. Scalar/MIR passes
                // can preserve CFG, dominators, and loop information across
                // operation-only rewrites instead of throwing those analyses
                // away at every pass boundary.
                const auto scope = result.invalidation == analysis::InvalidationScope::none
                    ? analysis::InvalidationScope::all : result.invalidation;
                analyses.invalidate(scope, result.touched_blocks);
            }
            if (verify_each) verify_or_throw(module, pass->name());
        }
    }
    return report;
}

PassResult PassManager::run(ir::Module& module, bool verify_each) const {
    // Production compilation runs this path, and it used to delegate to
    // run_with_report, which timestamps every pass and appends a record per
    // (function x pass). None of that is observable here, so the work and the
    // allocations were pure overhead on the hot path. The scoped invalidation
    // below is deliberately identical to the reporting path: only the telemetry
    // is dropped, never the analysis-preservation behaviour.
    PassResult total;
    for (auto& function : module.functions()) {
        analysis::FunctionAnalysisManager analyses(function);
        for (const auto& pass : passes_) {
            auto result = pass->run(function, analyses);
            total += result;
            if (result.changed) {
                const auto scope = result.invalidation == analysis::InvalidationScope::none
                    ? analysis::InvalidationScope::all : result.invalidation;
                analyses.invalidate(scope, result.touched_blocks);
            }
            if (verify_each) verify_or_throw(module, pass->name());
        }
    }
    return total;
}

std::vector<std::string> PassManager::pass_names() const {
    std::vector<std::string> names;
    names.reserve(passes_.size());
    for (const auto& pass : passes_) names.push_back(pass->name());
    return names;
}
} // namespace forge::pass
