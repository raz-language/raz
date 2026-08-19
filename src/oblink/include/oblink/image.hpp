// Copyright 2026 Mario Vinciguerra
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "oblink/diagnostic.hpp"
#include "oblink/format.hpp"

namespace oblink {

struct ImageDataDirectory {
    std::uint32_t rva{};
    std::uint32_t size{};
};

struct ImageSection {
    std::string name;
    std::uint32_t rva{};
    std::uint32_t virtual_size{};
    std::uint32_t characteristics{};
    // Empty for sections that occupy image space without file bytes.
    std::vector<std::byte> data;
};

struct ImageLayout {
    std::uint64_t image_base{0x140000000ULL};
    std::uint32_t section_alignment{0x1000};
    std::uint32_t file_alignment{0x200};
    std::uint32_t entry_rva{};
    std::uint16_t subsystem{subsystem_console};
    std::uint64_t stack_reserve{0x100000};
    std::uint64_t stack_commit{0x1000};
    std::uint64_t heap_reserve{0x100000};
    std::uint64_t heap_commit{0x1000};
    // Images without a base relocation table must load at their preferred base.
    bool relocations_stripped{true};
    bool dynamic_base{false};
    std::vector<ImageSection> sections;
    ImageDataDirectory directories[directory_count]{};
};

struct ImageWriteResult {
    std::vector<std::byte> bytes;
    Diagnostics diagnostics;
    [[nodiscard]] bool ok() const noexcept { return !has_error(diagnostics); }
};

// Serializes a PE32+ executable. Section RVAs are taken as given; the writer
// assigns file offsets, fills the headers, and pads to the file alignment.
[[nodiscard]] ImageWriteResult write_pe_image(const ImageLayout& layout);

// Rounds `value` up to a multiple of `alignment`, which must be non-zero.
[[nodiscard]] constexpr std::uint32_t align_up(std::uint32_t value, std::uint32_t alignment) noexcept {
    const auto remainder = value % alignment;
    return remainder == 0 ? value : value + (alignment - remainder);
}

} // namespace oblink
