// Copyright 2026 Mario Vinciguerra
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <algorithm>
#include <chrono>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "forge/analysis/function_analysis.hpp"
#include "forge/ir/module.hpp"

namespace forge::pass {

struct PassResult {
    bool changed{};
    std::size_t operations_removed{};
    std::size_t operations_rewritten{};
    std::size_t blocks_removed{};
    analysis::InvalidationScope invalidation{analysis::InvalidationScope::none};
    std::vector<std::string> touched_blocks;

    void touch_block(const std::string& block) {
        if (std::find(touched_blocks.begin(), touched_blocks.end(), block) == touched_blocks.end())
            touched_blocks.push_back(block);
    }

    PassResult& operator+=(const PassResult& other) {
        changed = changed || other.changed;
        operations_removed += other.operations_removed;
        operations_rewritten += other.operations_rewritten;
        blocks_removed += other.blocks_removed;
        if (other.changed && static_cast<unsigned>(other.invalidation) > static_cast<unsigned>(invalidation))
            invalidation = other.invalidation;
        for (const auto& block : other.touched_blocks) touch_block(block);
        return *this;
    }
};

class FunctionPass {
public:
    virtual ~FunctionPass() = default;
    [[nodiscard]] virtual std::string name() const = 0;
    virtual PassResult run(ir::Function&, analysis::FunctionAnalysisManager&) = 0;
};

struct PassExecutionRecord {
    std::string function_name;
    std::string pass_name;
    PassResult result;
    std::chrono::nanoseconds elapsed{};
};

struct PassRunReport {
    PassResult total;
    std::vector<PassExecutionRecord> records;
};

class PassManager {
public:
    template<class T, class... A>
    PassManager& add(A&&... arguments) {
        passes_.push_back(std::make_unique<T>(std::forward<A>(arguments)...));
        return *this;
    }

    [[nodiscard]] PassResult run(ir::Module& module, bool verify_each = true) const;
    [[nodiscard]] PassRunReport run_with_report(ir::Module& module, bool verify_each = true) const;
    [[nodiscard]] std::vector<std::string> pass_names() const;

private:
    std::vector<std::unique_ptr<FunctionPass>> passes_;
};

} // namespace forge::pass
