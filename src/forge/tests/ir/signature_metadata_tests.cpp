// Copyright 2026 Mario Vinciguerra
// SPDX-License-Identifier: Apache-2.0

#include "forge/ir/binary.hpp"
#include "forge/ir/parser.hpp"
#include "forge/ir/printer.hpp"
#include "forge/ir/verifier.hpp"

#include <cstdlib>
#include <iostream>
#include <string>

namespace {
void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}
}

int main() {
    const std::string source = R"FIR(module @metadata {
  extern variadic hidden c func @printf(%format: ptr) -> i32
  internal fast func @helper(%value: i64) -> i64 {
  entry:
    return %value
  }
  weak global @counter: i64 = 0
})FIR";
    auto parsed = forge::ir::parse_module(source);
    require(parsed.ok(), "metadata IR failed to parse");
    require(forge::ir::verify_module(*parsed.module).empty(), "metadata IR failed verification");
    const auto& printf = parsed.module->functions()[0];
    require(printf.variadic, "variadic metadata missing");
    require(printf.calling_convention == forge::ir::CallingConvention::c, "C calling convention missing");
    require(printf.visibility == forge::ir::SymbolVisibility::hidden, "hidden visibility missing");
    const auto& helper = parsed.module->functions()[1];
    require(helper.linkage == forge::ir::SymbolLinkage::internal, "internal linkage missing");
    require(helper.calling_convention == forge::ir::CallingConvention::fast, "fast calling convention missing");
    require(parsed.module->globals()[0].linkage == forge::ir::SymbolLinkage::weak, "weak global linkage missing");

    const auto printed = forge::ir::print_module(*parsed.module);
    require(printed.find("extern variadic hidden c func @printf") != std::string::npos,
            "canonical printer omitted function metadata");
    require(printed.find("internal fast func @helper") != std::string::npos,
            "canonical printer omitted internal fast metadata");

    const auto binary = forge::ir::write_binary(*parsed.module);
    require(binary.ok(), "metadata binary write failed");
    const auto decoded = forge::ir::read_binary(binary.bytes);
    require(decoded.ok(), "metadata binary read failed");
    require(decoded.module.functions()[0].variadic, "binary IR lost variadic metadata");
    require(decoded.module.functions()[1].calling_convention == forge::ir::CallingConvention::fast,
            "binary IR lost calling convention");

    std::cout << "signature metadata tests passed\n";
}
