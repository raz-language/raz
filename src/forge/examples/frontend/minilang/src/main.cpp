// Copyright 2026 Mario Vinciguerra
// SPDX-License-Identifier: Apache-2.0

#include <cstdint>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>
#include <forge/interpreter/interpreter.hpp>
#include <forge/ir/printer.hpp>
#include <forge/ir/source_map.hpp>
#include <forge/jit/engine.hpp>
#include <forge/jit/invoke.hpp>
#include <forge/machine/lower.hpp>
#include <forge/machine/optimize.hpp>
#include <forge/machine/verifier.hpp>
#include "minilang/codegen.hpp"
#include "minilang/lexer.hpp"
#include "minilang/parser.hpp"

namespace {

#if defined(_WIN32)
constexpr auto host_abi = forge::codegen::x86_64::Abi::windows;
#else
constexpr auto host_abi = forge::codegen::x86_64::Abi::system_v;
#endif

void print_lines(const std::vector<std::string>& diagnostics) {
    for (const auto& diagnostic : diagnostics) std::cerr << diagnostic << '\n';
}

void print_forge_diagnostics(const forge::Diagnostics& diagnostics) {
    for (const auto& diagnostic : diagnostics) std::cerr << diagnostic.message << '\n';
}

std::string read_file(const std::string& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("cannot open " + path);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

} // namespace

int main(int argc, char** argv) {
    const std::string path = argc > 1 ? argv[1] : "examples/frontend/minilang/example.mini";
    const std::string entry = argc > 2 ? argv[2] : "main";
    const bool emit_source_map = argc > 3 && std::string_view(argv[3]) == "--source-map";

    try {
        const auto source = read_file(path);
        auto lexed = minilang::lex(source, path);
        if (!lexed.ok()) { print_lines(lexed.diagnostics); return 1; }

        auto parsed = minilang::parse(std::move(lexed.tokens));
        if (!parsed.ok()) { print_lines(parsed.diagnostics); return 1; }

        auto lowered = minilang::lower_to_forge(*parsed.program);
        if (!lowered.ok()) { print_lines(lowered.diagnostics); return 1; }

        std::cout << "=== Forge IR ===\n" << forge::ir::print_module(*lowered.module) << '\n';
        if (emit_source_map) {
            std::cout << "=== Source map ===\n"
                      << forge::ir::build_source_map_json(*lowered.module) << '\n';
        }

        const auto interpreted = forge::interpreter::execute(*lowered.module, entry, {});
        if (!interpreted.diagnostics.empty() || !interpreted.value.has_value()) {
            print_forge_diagnostics(interpreted.diagnostics);
            return 1;
        }
        std::cout << "interpreter: " << interpreted.value->bits() << '\n';

#if defined(__x86_64__) || defined(_M_X64) || defined(__amd64__)
        auto machine = forge::machine::lower_module(*lowered.module);
        if (!machine.ok()) { print_forge_diagnostics(machine.diagnostics); return 1; }
        const auto optimization = forge::machine::optimize_module(*machine.module);
        (void)optimization;
        const auto machine_diagnostics = forge::machine::verify_module(*machine.module);
        if (!machine_diagnostics.empty()) { print_forge_diagnostics(machine_diagnostics); return 1; }

        auto loaded = forge::jit::load(*machine.module, host_abi);
        if (!loaded.ok()) { print_forge_diagnostics(loaded.diagnostics); return 1; }
        const auto invoked = forge::jit::invoke_integer(loaded.engine->lookup(entry), {});
        if (!invoked.ok()) { print_forge_diagnostics(invoked.diagnostics); return 1; }
        std::cout << "jit:         " << invoked.bits << '\n';
        if (invoked.bits != interpreted.value->bits()) {
            std::cerr << "interpreter/JIT mismatch\n";
            return 1;
        }
#else
        std::cout << "jit:         skipped (x86-64 host required)\n";
#endif
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "minilang: " << error.what() << '\n';
        return 1;
    }
}
