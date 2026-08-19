// Copyright 2026 Mario Vinciguerra
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

#include "oblink/diagnostic.hpp"

namespace oblink::coff {

constexpr std::uint16_t machine_amd64 = 0x8664;
constexpr std::uint16_t reloc_amd64_absolute = 0x0000;
constexpr std::uint16_t reloc_amd64_addr64 = 0x0001;
constexpr std::uint16_t reloc_amd64_addr32 = 0x0002;
constexpr std::uint16_t reloc_amd64_addr32nb = 0x0003;
constexpr std::uint16_t reloc_amd64_rel32 = 0x0004;
constexpr std::uint16_t reloc_amd64_rel32_1 = 0x0005;
constexpr std::uint16_t reloc_amd64_rel32_2 = 0x0006;
constexpr std::uint16_t reloc_amd64_rel32_3 = 0x0007;
constexpr std::uint16_t reloc_amd64_rel32_4 = 0x0008;
constexpr std::uint16_t reloc_amd64_rel32_5 = 0x0009;
constexpr std::uint16_t reloc_amd64_section = 0x000A;
constexpr std::uint16_t reloc_amd64_secrel = 0x000B;

// IMAGE_COMDAT_SELECT_* values from winnt.h.  Keep these in the parser layer
// so ObLink can make the same duplicate-section decisions as link.exe/lld-link
// without depending on the Windows SDK headers.
constexpr std::uint8_t comdat_select_noduplicates = 1;
constexpr std::uint8_t comdat_select_any = 2;
constexpr std::uint8_t comdat_select_same_size = 3;
constexpr std::uint8_t comdat_select_exact_match = 4;
constexpr std::uint8_t comdat_select_associative = 5;
constexpr std::uint8_t comdat_select_largest = 6;
constexpr std::uint8_t comdat_select_newest = 7;

struct Relocation {
  std::uint32_t virtual_address{};
  std::uint32_t symbol_index{};
  std::uint16_t type{};
};

struct Section {
  std::string name;
  std::vector<std::byte> data;
  std::uint32_t virtual_size{};
  std::uint32_t characteristics{};
  std::vector<Relocation> relocations;

  // COFF COMDAT policy is carried by the section-definition auxiliary symbol,
  // not by the public symbol itself.  Some MSVC objects rely on this metadata
  // even when consumers cannot safely infer the policy from the section name.
  std::uint8_t comdat_selection{};
  std::uint32_t comdat_checksum{};
  std::uint32_t comdat_associative_section{}; // one-based COFF section number
  // The external symbol that keys a non-associative COMDAT. COFF selection is
  // performed by leader identity, not by treating every external in the
  // section as an independent duplicate candidate.
  std::uint32_t comdat_leader_symbol{0xffffffffU};
  std::string comdat_leader_name;

  [[nodiscard]] bool is_comdat() const noexcept {
    return (characteristics & 0x00001000U) != 0U || comdat_selection != 0U;
  }
};

struct Symbol {
  std::string name;
  std::uint32_t value{};
  std::int32_t section_number{};
  std::uint16_t type{};
  std::uint8_t storage_class{};
  std::uint8_t auxiliary_count{};
  // IMAGE_SYM_CLASS_WEAK_EXTERNAL uses its first auxiliary record to name the
  // fallback symbol and selection policy. Keeping it here lets the linker honor
  // MSVC weak aliases without exposing raw COFF table storage.
  std::uint32_t weak_default_index{};
  std::uint32_t weak_characteristics{};
};

struct Object {
  std::filesystem::path source;
  std::vector<Section> sections;
  std::vector<Symbol> symbols;
};

struct ParseResult {
  Diagnostics diagnostics;
  Object object;
  [[nodiscard]] bool ok() const noexcept { return diagnostics.empty(); }
};

[[nodiscard]] bool is_bigobj(std::span<const std::byte> bytes) noexcept;
[[nodiscard]] ParseResult parse(std::span<const std::byte> bytes,
                                std::filesystem::path source = {});
[[nodiscard]] ParseResult read(const std::filesystem::path& path);

} // namespace oblink::coff
