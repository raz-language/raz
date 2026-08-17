// Copyright 2026 Mario Vinciguerra
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "forge/machine/module.hpp"

#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace forge::machine {

class RegisterBitSet {
public:
    RegisterBitSet() = default;
    explicit RegisterBitSet(std::size_t bit_count) { reset_size(bit_count); }

    void reset_size(std::size_t bit_count) {
        bit_count_ = bit_count;
        words_.assign((bit_count + 63U) / 64U, 0U);
    }
    void clear() noexcept { std::fill(words_.begin(), words_.end(), 0U); }
    [[nodiscard]] std::size_t size() const noexcept { return bit_count_; }
    [[nodiscard]] bool empty() const noexcept {
        for (const auto word : words_) if (word != 0U) return false;
        return true;
    }
    [[nodiscard]] bool test(std::size_t bit) const noexcept {
        return bit < bit_count_ && ((words_[bit >> 6U] >> (bit & 63U)) & 1U) != 0U;
    }
    void set(std::size_t bit) noexcept {
        if (bit < bit_count_) words_[bit >> 6U] |= std::uint64_t{1} << (bit & 63U);
    }
    void reset(std::size_t bit) noexcept {
        if (bit < bit_count_) words_[bit >> 6U] &= ~(std::uint64_t{1} << (bit & 63U));
    }
    [[nodiscard]] bool operator[](std::size_t bit) const noexcept { return test(bit); }

    void union_with(const RegisterBitSet& other) noexcept {
        const auto count = std::min(words_.size(), other.words_.size());
        for (std::size_t i = 0; i < count; ++i) words_[i] |= other.words_[i];
    }
    void assign_union_minus(const RegisterBitSet& uses, const RegisterBitSet& out,
                            const RegisterBitSet& defs) noexcept {
        const auto count = words_.size();
        for (std::size_t i = 0; i < count; ++i)
            words_[i] = uses.words_[i] | (out.words_[i] & ~defs.words_[i]);
        trim_tail();
    }
    [[nodiscard]] std::size_t count() const noexcept {
        std::size_t total = 0U;
        for (const auto word : words_) total += static_cast<std::size_t>(std::popcount(word));
        return total;
    }

    template <typename Fn>
    void for_each_set_bit(Fn&& fn) const {
        for (std::size_t word_index = 0; word_index < words_.size(); ++word_index) {
            auto word = words_[word_index];
            while (word != 0U) {
                const auto offset = static_cast<unsigned>(std::countr_zero(word));
                const auto bit = (word_index << 6U) + offset;
                if (bit < bit_count_) fn(static_cast<VirtualRegister>(bit));
                word &= word - 1U;
            }
        }
    }

    friend bool operator==(const RegisterBitSet&, const RegisterBitSet&) = default;

private:
    void trim_tail() noexcept {
        if (words_.empty() || (bit_count_ & 63U) == 0U) return;
        words_.back() &= (std::uint64_t{1} << (bit_count_ & 63U)) - 1U;
    }

    std::size_t bit_count_{};
    std::vector<std::uint64_t> words_;
};

struct LivenessAnalysis {
    std::vector<RegisterBitSet> uses;
    std::vector<RegisterBitSet> defs;
    std::vector<RegisterBitSet> live_in;
    std::vector<RegisterBitSet> live_out;
    std::vector<std::vector<RegisterBitSet>> live_after;
    std::vector<std::vector<std::size_t>> successors;
    std::uint32_t fixed_point_iterations{};
    std::uint32_t cross_block_live_values{};
};

[[nodiscard]] LivenessAnalysis analyze_liveness(const Function& function, bool include_instruction_liveness = true);

} // namespace forge::machine
