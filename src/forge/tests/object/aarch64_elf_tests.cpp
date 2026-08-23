// Copyright 2026 Mario Vinciguerra
// SPDX-License-Identifier: Apache-2.0

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#include "forge/ir/parser.hpp"
#include "forge/ir/verifier.hpp"
#include "forge/machine/lower.hpp"
#include "forge/object/elf.hpp"

namespace {
std::uint16_t u16(const std::vector<std::byte>& bytes, std::size_t offset) {
    return static_cast<std::uint16_t>(std::to_integer<std::uint8_t>(bytes.at(offset))) |
           static_cast<std::uint16_t>(std::to_integer<std::uint8_t>(bytes.at(offset + 1U))) << 8U;
}
std::uint32_t u32(const std::vector<std::byte>& bytes, std::size_t offset) {
    std::uint32_t value{};
    for (unsigned shift = 0; shift < 32; shift += 8)
        value |= static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(bytes.at(offset + shift / 8U))) << shift;
    return value;
}
std::uint64_t u64(const std::vector<std::byte>& bytes, std::size_t offset) {
    std::uint64_t value{};
    for (unsigned shift = 0; shift < 64; shift += 8)
        value |= static_cast<std::uint64_t>(std::to_integer<std::uint8_t>(bytes.at(offset + shift / 8U))) << shift;
    return value;
}
bool has_relocation(const std::vector<std::byte>& bytes, std::uint32_t wanted) {
    const auto shoff = u64(bytes, 40);
    const auto shnum = u16(bytes, 60);
    for (std::uint16_t index = 0; index < shnum; ++index) {
        const auto header = shoff + static_cast<std::uint64_t>(index) * 64U;
        if (u32(bytes, header + 4U) != 4U) continue; // SHT_RELA
        const auto offset = u64(bytes, header + 24U);
        const auto size = u64(bytes, header + 32U);
        for (std::uint64_t entry = 0; entry + 24U <= size; entry += 24U)
            if (static_cast<std::uint32_t>(u64(bytes, offset + entry + 8U)) == wanted) return true;
    }
    return false;
}
bool elf_symbol_is_local(const std::vector<std::byte>& bytes, std::string_view wanted) {
    const auto shoff = u64(bytes, 40U);
    const auto shnum = u16(bytes, 60U);
    for (std::uint16_t index = 0; index < shnum; ++index) {
        const auto header = shoff + static_cast<std::uint64_t>(index) * 64U;
        if (u32(bytes, header + 4U) != 2U) continue; // SHT_SYMTAB
        const auto symoff = u64(bytes, header + 24U);
        const auto symsize = u64(bytes, header + 32U);
        const auto strindex = u32(bytes, header + 40U);
        const auto first_global = u32(bytes, header + 44U);
        const auto entsize = u64(bytes, header + 56U);
        if (strindex >= shnum || entsize < 24U) return false;
        const auto strheader = shoff + static_cast<std::uint64_t>(strindex) * 64U;
        const auto stroff = u64(bytes, strheader + 24U);
        for (std::uint64_t entry = 0, ordinal = 0; entry + entsize <= symsize; entry += entsize, ++ordinal) {
            const auto symbol = symoff + entry;
            const auto nameoff = u32(bytes, symbol);
            std::string name;
            for (std::uint64_t pos = stroff + nameoff; pos < bytes.size(); ++pos) {
                const auto ch = std::to_integer<unsigned char>(bytes[pos]);
                if (ch == 0U) break;
                name.push_back(static_cast<char>(ch));
            }
            if (name == wanted)
                return ordinal < first_global && (std::to_integer<std::uint8_t>(bytes[symbol + 4U]) >> 4U) == 0U;
        }
    }
    return false;
}

}

int main() {
    const std::string source = R"(
module @a64_object {
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
    auto object = forge::object::emit_elf64_aarch64(*lowered.module);
    if (!object.ok() || object.bytes.size() < 64U) return 3;
    if (object.bytes[0] != std::byte{0x7f} || object.bytes[1] != std::byte{'E'} ||
        object.bytes[2] != std::byte{'L'} || object.bytes[3] != std::byte{'F'}) return 4;
    if (u16(object.bytes, 16U) != 1U || u16(object.bytes, 18U) != 183U) return 5; // ET_REL, EM_AARCH64
    if ((u64(object.bytes, 40U) & 7U) != 0U) return 6;
    if (!has_relocation(object.bytes, 275U) || !has_relocation(object.bytes, 277U) ||
        !has_relocation(object.bytes, 283U) || !has_relocation(object.bytes, 541U) ||
        !has_relocation(object.bytes, 542U)) return 7;
    const auto duplicate = forge::object::emit_elf64_aarch64(*lowered.module);
    if (!duplicate.ok() || duplicate.bytes != object.bytes) return 8;
    if (object.stats.external_symbol_count != 1U || object.stats.relocation_count != 5U) return 9;
    if (!elf_symbol_is_local(object.bytes, "hidden")) return 10;
    std::cout << "ELF64 AArch64 deterministic relocatable object: " << object.bytes.size() << " bytes\n";
    return EXIT_SUCCESS;
}
