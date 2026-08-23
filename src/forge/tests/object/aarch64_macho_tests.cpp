// Copyright 2026 Mario Vinciguerra
// SPDX-License-Identifier: Apache-2.0

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

#include "forge/ir/parser.hpp"
#include "forge/ir/verifier.hpp"
#include "forge/machine/lower.hpp"
#include "forge/object/macho.hpp"

namespace {
std::uint32_t u32(const std::vector<std::byte>& bytes, std::size_t offset) {
    std::uint32_t value{};
    for (unsigned shift = 0; shift < 32U; shift += 8U)
        value |= static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(bytes.at(offset + shift / 8U))) << shift;
    return value;
}

bool contains_ascii(const std::vector<std::byte>& bytes, std::string_view text) {
    if (text.empty() || text.size() > bytes.size()) return false;
    return std::search(bytes.begin(), bytes.end(), text.begin(), text.end(), [](std::byte left, char right) {
        return std::to_integer<unsigned char>(left) == static_cast<unsigned char>(right);
    }) != bytes.end();
}

bool relocation_types(const std::vector<std::byte>& bytes, std::vector<std::uint8_t>& types) {
    if (bytes.size() < 32U) return false;
    const auto commands = u32(bytes, 16U);
    std::size_t command_offset = 32U;
    for (std::uint32_t command = 0; command < commands; ++command) {
        if (command_offset + 8U > bytes.size()) return false;
        const auto kind = u32(bytes, command_offset);
        const auto size = u32(bytes, command_offset + 4U);
        if (size < 8U || command_offset + size > bytes.size()) return false;
        if (kind == 0x19U) { // LC_SEGMENT_64
            if (size < 72U) return false;
            const auto count = u32(bytes, command_offset + 64U);
            std::size_t section = command_offset + 72U;
            for (std::uint32_t index = 0; index < count; ++index, section += 80U) {
                if (section + 80U > command_offset + size) return false;
                const auto relocation_offset = u32(bytes, section + 56U);
                const auto relocation_count = u32(bytes, section + 60U);
                for (std::uint32_t relocation = 0; relocation < relocation_count; ++relocation) {
                    const auto offset = static_cast<std::size_t>(relocation_offset) + relocation * 8U;
                    if (offset + 8U > bytes.size()) return false;
                    types.push_back(static_cast<std::uint8_t>(u32(bytes, offset + 4U) >> 28U));
                }
            }
        }
        command_offset += size;
    }
    return true;
}

bool has_type(const std::vector<std::uint8_t>& types, std::uint8_t wanted) {
    return std::find(types.begin(), types.end(), wanted) != types.end();
}

bool macho_symbol_is_local(const std::vector<std::byte>& bytes, std::string_view wanted) {
    if (bytes.size() < 32U) return false;
    const auto commands = u32(bytes, 16U);
    std::size_t command_offset = 32U;
    for (std::uint32_t command = 0; command < commands; ++command) {
        if (command_offset + 24U > bytes.size()) return false;
        const auto kind = u32(bytes, command_offset);
        const auto size = u32(bytes, command_offset + 4U);
        if (size < 8U || command_offset + size > bytes.size()) return false;
        if (kind == 0x02U) { // LC_SYMTAB
            const auto symoff = u32(bytes, command_offset + 8U);
            const auto nsyms = u32(bytes, command_offset + 12U);
            const auto stroff = u32(bytes, command_offset + 16U);
            for (std::uint32_t index = 0; index < nsyms; ++index) {
                const auto symbol = static_cast<std::size_t>(symoff) + static_cast<std::size_t>(index) * 16U;
                if (symbol + 16U > bytes.size()) return false;
                const auto nameoff = u32(bytes, symbol);
                std::string name;
                for (std::size_t pos = static_cast<std::size_t>(stroff) + nameoff; pos < bytes.size(); ++pos) {
                    const auto ch = std::to_integer<unsigned char>(bytes[pos]);
                    if (ch == 0U) break;
                    name.push_back(static_cast<char>(ch));
                }
                if (name == wanted)
                    return (std::to_integer<std::uint8_t>(bytes[symbol + 4U]) & 0x01U) == 0U;
            }
            return false;
        }
        command_offset += size;
    }
    return false;
}
} // namespace

int main() {
    const std::string source = R"(
module @macho_arm64 {
  global @counter: i64 = 7
  internal global @hidden: i64 = 11
  thread_local global @tls_counter: i64 = 9
  extern func @host_add(%a: i64, %b: i64) -> i64
  func @entry(%value: i64) -> i64 {
  entry:
    %g = global.address ptr @counter
    %gv = load i64 %g align 8
    %t = tls.address ptr @tls_counter
    %tv = load i64 %t align 8
    %sum = add i64 %gv, %tv
    %r = call i64 @host_add(%sum, %value)
    return %r
  }
}
)";
    auto parsed = forge::ir::parse_module(source);
    if (!parsed.ok() || !forge::ir::verify_module(*parsed.module).empty()) return 1;
    auto lowered = forge::machine::lower_module(
        *parsed.module, {forge::machine::TargetArchitecture::aarch64});
    if (!lowered.ok()) return 2;
    auto object = forge::object::emit_macho64_aarch64(*lowered.module);
    if (!object.ok() || object.bytes.size() < 128U) return 3;

    if (u32(object.bytes, 0U) != 0xFEEDFACFU || u32(object.bytes, 4U) != 0x0100000CU ||
        u32(object.bytes, 12U) != 1U || (u32(object.bytes, 24U) & 0x2000U) == 0U) return 4;
    for (const auto name : {"__text", "__data", "__thread_data", "__thread_vars", "_entry",
                            "_counter", "_tls_counter", "_tls_counter$tlv$init", "__tlv_bootstrap", "_host_add"})
        if (!contains_ascii(object.bytes, name)) return 5;

    std::vector<std::uint8_t> types;
    if (!relocation_types(object.bytes, types)) return 6;
    for (const auto type : {std::uint8_t{0}, std::uint8_t{2}, std::uint8_t{3}, std::uint8_t{4},
                            std::uint8_t{8}, std::uint8_t{9}})
        if (!has_type(types, type)) return 7;

    if (!macho_symbol_is_local(object.bytes, "_hidden")) return 9;

    const auto duplicate = forge::object::emit_macho64_aarch64(*lowered.module);
    if (!duplicate.ok() || duplicate.bytes != object.bytes) return 8;
    std::cout << "Mach-O arm64 deterministic relocatable object: " << object.bytes.size() << " bytes\n";
    return EXIT_SUCCESS;
}
