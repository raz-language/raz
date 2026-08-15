// Copyright 2026 Mario Vinciguerra
// SPDX-License-Identifier: Apache-2.0

#include <forge/ir/builder.hpp>
#include <forge/ir/printer.hpp>
#include <forge/ir/verifier.hpp>
#include <iostream>

int main() {
    forge::ir::Context context;
    auto& module = context.create_module("my_language");
    forge::ir::IRBuilder builder(context, module);
    auto& function = builder.create_function("main", forge::ir::i64_type());
    auto& entry = builder.create_block(function, "entry");
    builder.position_at_end(entry);
    const auto answer = builder.create_constant(forge::ir::i64_type(), "42");
    builder.create_return(answer);
    const auto diagnostics = forge::ir::verify_module(module);
    if (!diagnostics.empty()) {
        std::cerr << diagnostics.front().message << '\n';
        return 1;
    }

    std::cout << forge::ir::print_module(module);
}
