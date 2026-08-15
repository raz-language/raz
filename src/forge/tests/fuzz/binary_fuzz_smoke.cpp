// Copyright 2026 Mario Vinciguerra
// SPDX-License-Identifier: Apache-2.0

#include <cstddef>
#include <cstdint>
#include <iostream>
#include <random>
#include <vector>

#include "forge/ir/binary.hpp"
#include "forge/ir/verifier.hpp"

int main() {
    std::mt19937_64 random(0xF073B1A4ULL);
    for (std::size_t case_index = 0; case_index < 5000; ++case_index) {
        const auto size = static_cast<std::size_t>(random() % 1024U);
        std::vector<std::byte> bytes(size);
        for (auto& value : bytes) value = static_cast<std::byte>(random() & 0xffU);
        auto decoded = forge::ir::read_binary(bytes);
        if (decoded.ok()) {
            const auto diagnostics = forge::ir::verify_module(decoded.module);
            (void)diagnostics;
            auto encoded = forge::ir::write_binary(decoded.module);
            if (!encoded.ok()) return 1;
            auto round_trip = forge::ir::read_binary(encoded.bytes);
            if (!round_trip.ok()) return 2;
        }
    }
    std::cout << "binary fuzz smoke: 5000 cases\n";
    return 0;
}
