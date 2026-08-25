// Copyright 2026 Mario Vinciguerra
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdint>
#include <optional>

namespace forge::target {

struct Aarch64LogicalImmediate final {
    std::uint8_t n{};
    std::uint8_t immr{};
    std::uint8_t imms{};
};

// Encode the A64 logical-immediate bitmask used by AND/ORR/EOR. Legal values
// are a rotated run of ones in a power-of-two element, replicated across the
// 32- or 64-bit destination. Instead of enumerating every possible run for
// every constant, first find the smallest repeating element and then test the
// single run length implied by its population count. That keeps this helper
// cheap enough to use in the instruction selector's hot path.
[[nodiscard]] inline std::optional<Aarch64LogicalImmediate>
encode_aarch64_logical_immediate(std::uint64_t value, unsigned width) noexcept {
    if (width != 32U && width != 64U) return std::nullopt;
    const auto width_mask = width == 64U ? ~std::uint64_t{0}
                                         : (std::uint64_t{1} << width) - 1U;
    value &= width_mask;
    if (value == 0U || value == width_mask) return std::nullopt;

    const auto mask_for_width = [](unsigned bits) noexcept {
        return bits == 64U ? ~std::uint64_t{0} : (std::uint64_t{1} << bits) - 1U;
    };
    const auto rotate_right = [&](std::uint64_t bits, unsigned rotation, unsigned element_width) noexcept {
        const auto mask = mask_for_width(element_width);
        bits &= mask;
        rotation %= element_width;
        if (rotation == 0U) return bits;
        return ((bits >> rotation) | (bits << (element_width - rotation))) & mask;
    };
    const auto popcount = [](std::uint64_t bits) noexcept {
        unsigned count = 0U;
        while (bits != 0U) {
            bits &= bits - 1U;
            ++count;
        }
        return count;
    };

    for (unsigned element_width = 2U; element_width <= width; element_width <<= 1U) {
        const auto element_mask = mask_for_width(element_width);
        const auto element = value & element_mask;
        std::uint64_t repeated = 0U;
        for (unsigned offset = 0U; offset < width; offset += element_width)
            repeated |= element << offset;
        if ((repeated & width_mask) != value) continue;

        const auto ones = popcount(element);
        if (ones == 0U || ones == element_width) continue;
        const auto base = (std::uint64_t{1} << ones) - 1U;
        for (unsigned rotation = 0U; rotation < element_width; ++rotation) {
            if (rotate_right(base, rotation, element_width) != element) continue;

            unsigned len = 0U;
            for (auto bits = element_width; bits > 1U; bits >>= 1U) ++len;
            Aarch64LogicalImmediate encoded;
            encoded.n = static_cast<std::uint8_t>(element_width == 64U ? 1U : 0U);
            encoded.immr = static_cast<std::uint8_t>(rotation & 0x3FU);
            const auto size_prefix = element_width == 64U
                ? 0U : (~((1U << (len + 1U)) - 1U)) & 0x3FU;
            encoded.imms = static_cast<std::uint8_t>(size_prefix | (ones - 1U));
            return encoded;
        }
    }
    return std::nullopt;
}

} // namespace forge::target
