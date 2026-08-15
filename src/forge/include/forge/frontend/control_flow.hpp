// Copyright 2026 Mario Vinciguerra
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include <cstdint>
#include <functional>
#include <string>
#include "forge/ir/builder.hpp"

namespace forge::frontend {

class ControlFlowBuilder {
public:
    ControlFlowBuilder(ir::IRBuilder& builder, ir::FunctionHandle function)
        : builder_(&builder), function_(function) {}
    void create_if(std::string condition,
                   const std::function<void()>& then_body,
                   const std::function<void()>& else_body = {});
    void create_while(const std::function<std::string()>& condition,
                      const std::function<void()>& body);
private:
    [[nodiscard]] std::string unique(std::string_view prefix);
    ir::IRBuilder* builder_{};
    ir::FunctionHandle function_{};
    std::uint64_t next_block_{};
};

} // namespace forge::frontend
