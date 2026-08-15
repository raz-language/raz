// Copyright 2026 Mario Vinciguerra
// SPDX-License-Identifier: Apache-2.0

#include "forge/ir/binary.hpp"
#include "forge/ir/printer.hpp"
#include <cstddef>
#include <fstream>
#include <iostream>
#include <iterator>
#include <vector>

int main(int argc, char** argv) {
    if (argc != 2) { std::cerr << "usage: forge-dis <input.fbc>\n"; return 2; }
    std::ifstream file(argv[1], std::ios::binary);
    if (!file) { std::cerr << "error: cannot open " << argv[1] << '\n'; return 1; }
    std::vector<char> raw((std::istreambuf_iterator<char>(file)), {});
    std::vector<std::byte> bytes(raw.size());
    for (std::size_t i = 0; i < raw.size(); ++i) bytes[i] = static_cast<std::byte>(static_cast<unsigned char>(raw[i]));
    auto decoded = forge::ir::read_binary(bytes);
    if (!decoded.ok()) { for (const auto& d : decoded.diagnostics) std::cerr << "error: " << d.message << '\n'; return 1; }
    std::cout << forge::ir::print_module(decoded.module);
}
