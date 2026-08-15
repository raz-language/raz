// Copyright 2026 Mario Vinciguerra
// SPDX-License-Identifier: Apache-2.0

#include "forge/frontend/control_flow.hpp"

namespace forge::frontend {
std::string ControlFlowBuilder::unique(std::string_view prefix) {
    return std::string(prefix) + "." + std::to_string(next_block_++);
}

void ControlFlowBuilder::create_if(std::string condition,
                                   const std::function<void()>& then_body,
                                   const std::function<void()>& else_body) {
    const auto then_name = unique("if.then");
    const auto else_name = unique("if.else");
    const auto merge_name = unique("if.end");
    const auto then_block = builder_->create_block_handle(function_, then_name);
    const auto else_block = builder_->create_block_handle(function_, else_name);
    const auto merge_block = builder_->create_block_handle(function_, merge_name);
    builder_->create_branch(std::move(condition), then_name, else_name);
    builder_->position_at_end(then_block);
    then_body();
    const bool then_terminated = builder_->insertion_block_terminated();
    if (!then_terminated) builder_->create_jump(merge_name);

    builder_->position_at_end(else_block);
    if (else_body) else_body();
    const bool else_terminated = builder_->insertion_block_terminated();
    if (!else_terminated) builder_->create_jump(merge_name);

    builder_->position_at_end(merge_block);
    if (then_terminated && else_terminated) builder_->create_unreachable();
}

void ControlFlowBuilder::create_while(const std::function<std::string()>& condition,
                                      const std::function<void()>& body) {
    const auto condition_name = unique("while.cond");
    const auto body_name = unique("while.body");
    const auto end_name = unique("while.end");
    const auto condition_block = builder_->create_block_handle(function_, condition_name);
    const auto body_block = builder_->create_block_handle(function_, body_name);
    const auto end_block = builder_->create_block_handle(function_, end_name);
    builder_->create_jump(condition_name);
    builder_->position_at_end(condition_block);
    builder_->create_branch(condition(), body_name, end_name);
    builder_->position_at_end(body_block);
    body();
    if (!builder_->insertion_block_terminated()) builder_->create_jump(condition_name);
    builder_->position_at_end(end_block);
}
} // namespace forge::frontend
