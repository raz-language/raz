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
            if (result.changed) analyses.invalidate_all();
            if (verify_each) verify_or_throw(module, pass->name());
        }
    }
    return report;
}

PassResult PassManager::run(ir::Module& module, bool verify_each) const {
    return run_with_report(module, verify_each).total;
}

std::vector<std::string> PassManager::pass_names() const {
    std::vector<std::string> names;
    names.reserve(passes_.size());
    for (const auto& pass : passes_) names.push_back(pass->name());
    return names;
}
} // namespace forge::pass
