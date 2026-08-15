// Copyright 2026 Mario Vinciguerra
// SPDX-License-Identifier: Apache-2.0

#include "forge/ir/builder.hpp"
#include "forge/ir/printer.hpp"
#include "forge/ir/verifier.hpp"
#include <iostream>

int main() {
    forge::ir::Context context;
    auto& module = context.create_module("tiny_language");
    forge::ir::IRBuilder builder(context, module);
    std::vector<forge::ir::ValueDecl> args{{"%a", forge::ir::i64_type()}, {"%b", forge::ir::i64_type()}};
    auto& function = builder.create_function("add", forge::ir::i64_type(), args);
    auto& entry = builder.create_block(function, "entry");
    builder.position_at_end(entry);
    builder.set_source_location({"example.tiny", 1, 1});
    auto value = builder.create_add(forge::ir::i64_type(), "%a", "%b");
    builder.create_return(value);
    if (!forge::ir::verify_module(module).empty()) return 1;
    std::cout << forge::ir::print_module(module);
}
