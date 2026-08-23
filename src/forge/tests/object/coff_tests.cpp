// Copyright 2026 Mario Vinciguerra
// SPDX-License-Identifier: Apache-2.0

#include <cstddef>
#include <cstdint>
#include <iostream>
#include <string>
#include <string_view>

#include "forge/ir/parser.hpp"
#include "forge/ir/verifier.hpp"
#include "forge/machine/lower.hpp"
#include "forge/object/coff.hpp"

namespace {
std::uint16_t u16(const std::vector<std::byte>& bytes, std::size_t offset) {
    return static_cast<std::uint16_t>(std::to_integer<std::uint8_t>(bytes.at(offset))) |
           static_cast<std::uint16_t>(std::to_integer<std::uint8_t>(bytes.at(offset + 1))) << 8U;
}

std::uint32_t u32(const std::vector<std::byte>& bytes, std::size_t offset) {
    std::uint32_t value{};
    for (unsigned shift = 0; shift < 32; shift += 8)
        value |= static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(bytes.at(offset + shift / 8U))) << shift;
    return value;
}

bool contains_ascii(const std::vector<std::byte>& bytes, std::string_view text) {
    for (std::size_t offset = 0; offset + text.size() <= bytes.size(); ++offset) {
        bool equal = true;
        for (std::size_t index = 0; index < text.size(); ++index)
            equal = equal && std::to_integer<unsigned char>(bytes[offset + index]) == static_cast<unsigned char>(text[index]);
        if (equal) return true;
    }
    return false;
}
std::uint8_t coff_storage_class(const std::vector<std::byte>& bytes, std::string_view short_symbol) {
    const auto table = u32(bytes, 8U);
    const auto count = u32(bytes, 12U);
    for (std::uint32_t index = 0; index < count; ++index) {
        const auto offset = static_cast<std::size_t>(table) + static_cast<std::size_t>(index) * 18U;
        if (offset + 18U > bytes.size()) break;
        bool equal = short_symbol.size() <= 8U;
        for (std::size_t char_index = 0; equal && char_index < 8U; ++char_index) {
            const auto expected = char_index < short_symbol.size() ? static_cast<unsigned char>(short_symbol[char_index]) : 0U;
            equal = std::to_integer<unsigned char>(bytes[offset + char_index]) == expected;
        }
        if (equal) return std::to_integer<std::uint8_t>(bytes[offset + 16U]);
    }
    return 0xffU;
}

}

int main() {
    const std::string source = R"(
module @coff_test {
  global @counter: i64 = 7
  internal global @hidden: i64 = 11
  extern func @host_add(%left: i64, %right: i64) -> i64
  func @entry(%value: i64) -> i64 {
  entry:
    %counter = global.address ptr @counter
    %loaded = load i64 %counter align 8
    %result = call i64 @host_add(%loaded, %value)
    return %result
  }
}
)";
    auto parsed = forge::ir::parse_module(source);
    if (!parsed.ok()) return 1;
    if (!forge::ir::verify_module(*parsed.module).empty()) return 2;
    auto lowered = forge::machine::lower_module(*parsed.module);
    if (!lowered.ok()) return 3;
    auto object = forge::object::emit_coff_x86_64(*lowered.module);
    if (!object.ok()) return 4;
    if (object.bytes.size() < 20 + 3 * 40) return 5;
    if (u16(object.bytes, 0) != 0x8664 || u16(object.bytes, 2) != 3) return 6;
    const auto symbol_table = u32(object.bytes, 8);
    const auto symbol_count = u32(object.bytes, 12);
    if (symbol_count < 3 || symbol_table + symbol_count * 18U + 4U > object.bytes.size()) return 7;
    const auto text_relocations = u16(object.bytes, 20 + 32);
    if (text_relocations < 2) return 8;
    if (object.stats.section_count != 3 || object.stats.relocation_count < 2 || object.stats.external_symbol_count != 1) return 9;
    if (coff_storage_class(object.bytes, "hidden") != 3U) return 17;
    const auto duplicate = forge::object::emit_coff_x86_64(*lowered.module);
    if (!duplicate.ok() || duplicate.bytes != object.bytes) return 10;
    auto bad = *lowered.module;
    bad.functions.push_back(bad.functions.front());
    if (forge::object::emit_coff_x86_64(bad).ok()) return 11;

    const std::string tls_source = R"(
module @tls_object {
  thread_local global @counter: i64 = 40
  func @tls_value() -> i64 {
  entry:
    %address = tls.address ptr @counter
    %value = load i64 %address align 8
    return %value
  }
}
)";
    auto tls_parsed = forge::ir::parse_module(tls_source);
    if (!tls_parsed.ok() || !forge::ir::verify_module(*tls_parsed.module).empty()) return 12;
    auto tls_lowered = forge::machine::lower_module(*tls_parsed.module);
    if (!tls_lowered.ok()) return 13;
    auto tls_object = forge::object::emit_coff_x86_64(*tls_lowered.module);
    if (!tls_object.ok() || tls_object.stats.section_count != 4 || tls_object.stats.relocation_count != 2) return 14;
    if (!contains_ascii(tls_object.bytes, ".tls$AAA") || !contains_ascii(tls_object.bytes, "_tls_index") || !contains_ascii(tls_object.bytes, "counter")) return 15;
    // .text has one REL32 relocation to _tls_index and one SECREL to counter.
    const auto tls_reloc_offset = u32(tls_object.bytes, 20 + 24);
    const auto tls_reloc_count = u16(tls_object.bytes, 20 + 32);
    bool found_rel32 = false, found_secrel = false;
    for (std::uint16_t index = 0; index < tls_reloc_count; ++index) {
        const auto type = u16(tls_object.bytes, tls_reloc_offset + static_cast<std::size_t>(index) * 10U + 8U);
        found_rel32 = found_rel32 || type == 0x0004;
        found_secrel = found_secrel || type == 0x000B;
    }

    if (!found_rel32 || !found_secrel) return 16;
    std::cout << "COFF AMD64 deterministic relocatable object: " << object.bytes.size() << " bytes\n";
    return 0;
}
