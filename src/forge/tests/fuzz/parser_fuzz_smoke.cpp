// Copyright 2026 Mario Vinciguerra
// SPDX-License-Identifier: Apache-2.0

#include <cstdint>
#include <iostream>
#include <random>
#include <string>

#include "forge/ir/parser.hpp"
#include "forge/ir/printer.hpp"
#include "forge/ir/verifier.hpp"

int main() {
    std::mt19937_64 random(0x464f524745ULL);
    static constexpr char alphabet[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789@%{}[]():,=+-*/_ \n\t\\\"";
    for (std::size_t case_index = 0; case_index < 5000; ++case_index) {
        const std::size_t size = static_cast<std::size_t>(random() % 512);
        std::string input;
        input.reserve(size);
        for (std::size_t index = 0; index < size; ++index) {
            input.push_back(alphabet[random() % (sizeof(alphabet) - 1)]);
        }
        auto parsed = forge::ir::parse_module(input);
        if (!parsed.ok()) continue;
        (void)forge::ir::verify_module(*parsed.module);
        const auto printed = forge::ir::print_module(*parsed.module);
        auto reparsed = forge::ir::parse_module(printed);
        if (!reparsed.ok()) {
            std::cerr << "canonical printer produced unparsable IR at case " << case_index << '\n';
            return 1;
        }
    }
    std::cout << "parser fuzz smoke: 5000 cases passed\n";
    return 0;
}
