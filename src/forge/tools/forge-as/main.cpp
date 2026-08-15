// Copyright 2026 Mario Vinciguerra
// SPDX-License-Identifier: Apache-2.0

#include "forge/ir/binary.hpp"
#include "forge/ir/parser.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <sstream>

int main(int argc, char** argv) {
    if (argc < 2 || argc > 4) { std::cerr << "usage: forge-as <input.fir> [-o output.fbc]\n"; return 2; }
    std::filesystem::path input = argv[1];
    std::filesystem::path output = input; output.replace_extension(".fbc");
    if (argc == 4) { if (std::string_view(argv[2]) != "-o") { std::cerr << "error: expected -o\n"; return 2; } output = argv[3]; }
    std::ifstream file(input);
    if (!file) { std::cerr << "error: cannot open " << input.string() << '\n'; return 1; }
    std::ostringstream source; source << file.rdbuf();
    auto parsed = forge::ir::parse_module(source.str());
    if (!parsed.ok()) { for (const auto& d : parsed.diagnostics) std::cerr << "error: " << d.message << '\n'; return 1; }
    auto encoded = forge::ir::write_binary(*parsed.module);
    if (!encoded.ok()) { for (const auto& d : encoded.diagnostics) std::cerr << "error: " << d.message << '\n'; return 1; }
    std::ofstream destination(output, std::ios::binary);
    destination.write(reinterpret_cast<const char*>(encoded.bytes.data()), static_cast<std::streamsize>(encoded.bytes.size()));
    if (!destination) { std::cerr << "error: failed to write " << output.string() << '\n'; return 1; }
    std::cout << "FORGE  Assembled " << input.string() << " -> " << output.string() << " (" << encoded.bytes.size() << " bytes)\n";
}
