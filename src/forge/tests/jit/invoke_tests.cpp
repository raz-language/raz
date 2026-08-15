// Copyright 2026 Mario Vinciguerra
// SPDX-License-Identifier: Apache-2.0

#include <array>
#include <cstdint>
#include <iostream>
#include <stdexcept>

#include "forge/jit/invoke.hpp"

namespace {
std::uint64_t recorded{};

extern "C" std::uint64_t sum_eight(
    std::uint64_t a, std::uint64_t b, std::uint64_t c, std::uint64_t d,
    std::uint64_t e, std::uint64_t f, std::uint64_t g, std::uint64_t h) {
    return a + b + c + d + e + f + g + h;
}

extern "C" void record_eight(
    std::uint64_t a, std::uint64_t b, std::uint64_t c, std::uint64_t d,
    std::uint64_t e, std::uint64_t f, std::uint64_t g, std::uint64_t h) {
    recorded = a + b + c + d + e + f + g + h;
}

std::uint64_t pointer_value = 77;
std::uint64_t* return_pointer() { return &pointer_value; }

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
}

int main() {
    try {
        const std::array<std::uint64_t, 8> arguments{1, 2, 3, 4, 5, 6, 7, 8};
        const auto value = forge::jit::invoke_integer(reinterpret_cast<void*>(&sum_eight), arguments);
        require(value.ok(), "eight-argument value invocation failed");
        require(value.bits == 36, "eight-argument value invocation returned the wrong result");

        recorded = 0;
        const auto void_result = forge::jit::invoke_integer(reinterpret_cast<void*>(&record_eight), arguments, true);
        require(void_result.ok(), "eight-argument void invocation failed");
        require(recorded == 36, "eight-argument void invocation passed arguments incorrectly");

        const std::array<std::uint64_t, 0> no_arguments{};
        const auto pointer_result = forge::jit::invoke_pointer(reinterpret_cast<void*>(&return_pointer), no_arguments);
        require(pointer_result.ok(), "pointer invocation failed");
        require(pointer_result.pointer() == &pointer_value, "pointer invocation returned the wrong address");

        const auto null_result = forge::jit::invoke_integer(nullptr, arguments);
        require(!null_result.ok(), "null entry point was accepted");

        const std::array<std::uint64_t, 9> too_many{};
        const auto oversized = forge::jit::invoke_integer(reinterpret_cast<void*>(&sum_eight), too_many);
        require(!oversized.ok(), "oversized invocation signature was accepted");

        std::cout << "Forge host JIT invocation tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "test failure: " << error.what() << '\n';
        return 1;
    }
}
