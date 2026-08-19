// Copyright 2026 Mario Vinciguerra
// SPDX-License-Identifier: Apache-2.0

#include <cstdint>
#include <iostream>
#include <string>

#include "oblink/image.hpp"

namespace {

int failures = 0;

void check(bool condition, const std::string& message) {
    if (condition) return;
    std::cerr << "FAIL " << message << '\n';
    ++failures;
}

std::uint16_t u16(const std::vector<std::byte>& bytes, std::size_t offset) {
    return static_cast<std::uint16_t>(std::to_integer<std::uint8_t>(bytes.at(offset))) |
           static_cast<std::uint16_t>(static_cast<std::uint16_t>(std::to_integer<std::uint8_t>(bytes.at(offset + 1))) << 8U);
}

std::uint32_t u32(const std::vector<std::byte>& bytes, std::size_t offset) {
    std::uint32_t value{};
    for (unsigned shift = 0; shift < 32; shift += 8)
        value |= static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(bytes.at(offset + shift / 8U))) << shift;
    return value;
}

oblink::ImageLayout sample_layout() {
    oblink::ImageLayout layout;
    layout.entry_rva = 0x1000;

    oblink::ImageSection text;
    text.name = ".text";
    text.rva = 0x1000;
    text.characteristics = oblink::section_code | oblink::section_memory_execute | oblink::section_memory_read;
    text.data = {std::byte{0xC3}};
    text.virtual_size = 1;
    layout.sections.push_back(std::move(text));

    oblink::ImageSection bss;
    bss.name = ".bss";
    bss.rva = 0x2000;
    bss.characteristics = oblink::section_uninitialized_data | oblink::section_memory_read | oblink::section_memory_write;
    bss.virtual_size = 0x400;
    layout.sections.push_back(std::move(bss));

    return layout;
}

void test_headers_describe_the_image() {
    const auto result = oblink::write_pe_image(sample_layout());
    for (const auto& diagnostic : result.diagnostics) std::cerr << "  " << diagnostic.message << '\n';
    check(result.ok(), "the image is written");
    if (!result.ok()) return;

    const auto& bytes = result.bytes;
    check(std::to_integer<char>(bytes.at(0)) == 'M', "the DOS signature is present");
    const auto pe_offset = u32(bytes, 0x3C);
    check(u32(bytes, pe_offset) == 0x00004550, "the PE signature is at e_lfanew");
    check(u16(bytes, pe_offset + 4) == 0x8664, "the machine type is x86-64");
    check(u16(bytes, pe_offset + 6) == 2, "both sections are described");

    const auto optional_header = pe_offset + 24;
    check(u16(bytes, optional_header) == 0x020B, "the optional header is PE32+");
    check(u32(bytes, optional_header + 16) == 0x1000, "the entry point is recorded");
    check(u32(bytes, optional_header + 56) >= 0x2400, "the image size covers the last section");
    check(u16(bytes, optional_header + 68) == oblink::subsystem_console, "the subsystem is console");
}

void test_uninitialized_sections_take_no_file_space() {
    const auto result = oblink::write_pe_image(sample_layout());
    check(result.ok(), "the image is written");
    if (!result.ok()) return;

    const auto pe_offset = u32(result.bytes, 0x3C);
    const auto sections = pe_offset + 24 + 240;
    // The second section header describes .bss.
    check(u32(result.bytes, sections + 40 + 16) == 0, "the .bss section has no raw size");
    check(u32(result.bytes, sections + 40 + 20) == 0, "the .bss section has no file offset");
    check(u32(result.bytes, sections + 40 + 8) == 0x400, "the .bss section keeps its virtual size");
}

void test_rejects_an_empty_layout() {
    oblink::ImageLayout layout;
    const auto result = oblink::write_pe_image(layout);
    check(!result.ok(), "an image with no sections is rejected");
}

void test_alignment_helper() {
    check(oblink::align_up(0, 512) == 0, "zero is already aligned");
    check(oblink::align_up(1, 512) == 512, "one rounds up to the alignment");
    check(oblink::align_up(512, 512) == 512, "an aligned value is unchanged");
    check(oblink::align_up(513, 512) == 1024, "a value past the boundary rounds up");
}

} // namespace

int main() {
    test_headers_describe_the_image();
    test_uninitialized_sections_take_no_file_space();
    test_rejects_an_empty_layout();
    test_alignment_helper();

    if (failures != 0) {
        std::cerr << "oblink image tests: " << failures << " failure(s)\n";
        return 1;
    }
    std::cout << "oblink image tests: PASS\n";
    return 0;
}
