// Copyright 2026 Mario Vinciguerra
// SPDX-License-Identifier: Apache-2.0

#include "oblink/coff.hpp"

#include <algorithm>
#include <array>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <type_traits>

namespace oblink::coff {
namespace {

template <typename T>
T read_le(std::span<const std::byte> bytes, std::size_t offset) {
  if (offset + sizeof(T) > bytes.size()) throw std::runtime_error("truncated COFF input");
  using U = std::make_unsigned_t<T>;
  U value{};
  for (std::size_t i = 0; i < sizeof(T); ++i)
    value |= static_cast<U>(std::to_integer<std::uint8_t>(bytes[offset + i])) << (i * 8U);
  return static_cast<T>(value);
}

std::string short_string(std::span<const std::byte> bytes, std::size_t offset, std::size_t count) {
  std::string result;
  for (std::size_t i = 0; i < count && offset + i < bytes.size(); ++i) {
    const char ch = static_cast<char>(std::to_integer<unsigned char>(bytes[offset + i]));
    if (ch == '\0') break;
    result.push_back(ch);
  }
  return result;
}

std::string table_string(std::span<const std::byte> bytes, std::size_t strings,
                         std::uint32_t table_size, std::uint32_t offset) {
  if (offset < 4U || offset >= table_size) throw std::runtime_error("invalid COFF string-table offset");
  const std::size_t begin = strings + offset;
  const std::size_t limit = strings + table_size;
  std::string result;
  for (std::size_t p = begin; p < limit; ++p) {
    const char ch = static_cast<char>(std::to_integer<unsigned char>(bytes[p]));
    if (ch == '\0') return result;
    result.push_back(ch);
  }
  throw std::runtime_error("unterminated COFF string-table entry");
}

std::string symbol_name(std::span<const std::byte> bytes, std::size_t entry,
                        std::size_t strings, std::uint32_t string_size) {
  const auto zeroes = read_le<std::uint32_t>(bytes, entry);
  if (zeroes != 0U) return short_string(bytes, entry, 8);
  return table_string(bytes, strings, string_size, read_le<std::uint32_t>(bytes, entry + 4));
}

std::string section_name(std::span<const std::byte> bytes, std::size_t header,
                         std::size_t strings, std::uint32_t string_size) {
  std::string name = short_string(bytes, header, 8);
  if (name.size() > 1U && name.front() == '/') {
    std::uint32_t offset = 0;
    for (std::size_t i = 1; i < name.size(); ++i) {
      const char c = name[i];
      if (c < '0' || c > '9') return name;
      offset = offset * 10U + static_cast<std::uint32_t>(c - '0');
    }
    return table_string(bytes, strings, string_size, offset);
  }
  return name;
}

constexpr std::array<std::uint8_t, 16> bigobj_magic = {
    0xc7, 0xa1, 0xba, 0xd1, 0xee, 0xba, 0xa9, 0x4b,
    0xaf, 0x20, 0xfa, 0xf6, 0x6a, 0xa4, 0xdc, 0xb8};

bool bigobj_signature(std::span<const std::byte> bytes) noexcept {
  if (bytes.size() < 56U) return false;
  auto byte = [&](std::size_t i) { return std::to_integer<std::uint8_t>(bytes[i]); };
  if (byte(0) != 0U || byte(1) != 0U || byte(2) != 0xffU || byte(3) != 0xffU) return false;
  const std::uint16_t version = std::uint16_t(byte(4)) | (std::uint16_t(byte(5)) << 8U);
  const std::uint16_t machine = std::uint16_t(byte(6)) | (std::uint16_t(byte(7)) << 8U);
  if (version < 2U || machine != machine_amd64) return false;
  for (std::size_t i = 0; i < bigobj_magic.size(); ++i)
    if (byte(12U + i) != bigobj_magic[i]) return false;
  return true;
}

std::vector<std::byte> read_all(const std::filesystem::path& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) throw std::runtime_error("cannot open input object: " + path.string());
  std::vector<char> raw((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  std::vector<std::byte> bytes;
  bytes.reserve(raw.size());
  for (unsigned char ch : raw) bytes.push_back(static_cast<std::byte>(ch));
  return bytes;
}

} // namespace

bool is_bigobj(std::span<const std::byte> bytes) noexcept { return bigobj_signature(bytes); }

ParseResult parse(std::span<const std::byte> bytes, std::filesystem::path source) {
  ParseResult result;
  result.object.source = std::move(source);
  try {
    if (bytes.size() < 20U) throw std::runtime_error("COFF object is smaller than the file header");
    const bool bigobj = bigobj_signature(bytes);
    std::uint32_t section_count{};
    std::uint32_t symbol_offset{};
    std::uint32_t symbol_count{};
    std::size_t section_table{};
    std::size_t symbol_size{};
    if (bigobj) {
      section_count = read_le<std::uint32_t>(bytes, 44U);
      symbol_offset = read_le<std::uint32_t>(bytes, 48U);
      symbol_count = read_le<std::uint32_t>(bytes, 52U);
      section_table = 56U;
      symbol_size = 20U;
    } else {
      if (read_le<std::uint16_t>(bytes, 0) != machine_amd64)
        throw std::runtime_error("ObLink currently supports AMD64 COFF objects only");
      section_count = read_le<std::uint16_t>(bytes, 2U);
      symbol_offset = read_le<std::uint32_t>(bytes, 8U);
      symbol_count = read_le<std::uint32_t>(bytes, 12U);
      const auto optional_size = read_le<std::uint16_t>(bytes, 16U);
      section_table = 20U + optional_size;
      symbol_size = 18U;
    }
    if (section_table + static_cast<std::size_t>(section_count) * 40U > bytes.size())
      throw std::runtime_error("truncated COFF section table");

    if (symbol_offset > bytes.size() ||
        static_cast<std::uint64_t>(symbol_count) * symbol_size > bytes.size() - symbol_offset)
      throw std::runtime_error("invalid COFF symbol table");
    const std::size_t strings = static_cast<std::size_t>(symbol_offset) +
                                static_cast<std::size_t>(symbol_count) * symbol_size;
    if (strings + 4U > bytes.size()) throw std::runtime_error("COFF string table is missing");
    const auto string_size = read_le<std::uint32_t>(bytes, strings);
    if (string_size < 4U || string_size > bytes.size() - strings)
      throw std::runtime_error("invalid COFF string table size");

    result.object.sections.reserve(section_count);
    for (std::uint32_t i = 0; i < section_count; ++i) {
      const std::size_t header = section_table + static_cast<std::size_t>(i) * 40U;
      Section section;
      section.name = section_name(bytes, header, strings, string_size);
      section.virtual_size = read_le<std::uint32_t>(bytes, header + 8);
      const auto raw_size = read_le<std::uint32_t>(bytes, header + 16);
      const auto raw_offset = read_le<std::uint32_t>(bytes, header + 20);
      const auto relocation_offset = read_le<std::uint32_t>(bytes, header + 24);
      std::uint32_t relocation_count = read_le<std::uint16_t>(bytes, header + 32);
      section.characteristics = read_le<std::uint32_t>(bytes, header + 36);
      std::size_t relocation_start = relocation_offset;
      // IMAGE_SCN_LNK_NRELOC_OVFL: when a section has more than 65535
      // relocations, the first relocation is an overflow record whose
      // VirtualAddress stores the table count including that record.
      if (relocation_count == 0xffffU && (section.characteristics & 0x01000000U) != 0U) {
        if (relocation_offset > bytes.size() || 10U > bytes.size() - relocation_offset)
          throw std::runtime_error("truncated extended COFF relocation count");
        const std::uint32_t extended_count = read_le<std::uint32_t>(bytes, relocation_offset);
        if (extended_count == 0U)
          throw std::runtime_error("invalid extended COFF relocation count");
        relocation_count = extended_count - 1U;
        relocation_start += 10U;
      }
      // In an object file an uninitialized-data section still reports its size
      // in SizeOfRawData, but PointerToRawData is zero because the bytes do not
      // exist in the file. Reading raw_size bytes from offset zero would copy
      // the object's own COFF header into .bss and hand the image a block of
      // globals that start out non-zero.
      const bool uninitialized = (section.characteristics & 0x00000080U) != 0U || raw_offset == 0U;
      if (raw_size != 0U && !uninitialized) {
        if (raw_offset > bytes.size() || raw_size > bytes.size() - raw_offset)
          throw std::runtime_error("invalid COFF section data range");
        section.data.assign(bytes.begin() + raw_offset, bytes.begin() + raw_offset + raw_size);
      } else if (raw_size != 0U) {
        // Object files leave VirtualSize at zero and carry the real extent in
        // SizeOfRawData, so this is where a .bss section's size comes from.
        section.virtual_size = std::max(section.virtual_size, raw_size);
      }
      if (relocation_count != 0U) {
        const std::size_t bytes_needed = static_cast<std::size_t>(relocation_count) * 10U;
        if (relocation_start > bytes.size() || bytes_needed > bytes.size() - relocation_start)
          throw std::runtime_error("invalid COFF relocation table range");
        section.relocations.reserve(relocation_count);
        for (std::uint32_t r = 0; r < relocation_count; ++r) {
          const std::size_t entry = relocation_start + static_cast<std::size_t>(r) * 10U;
          section.relocations.push_back({read_le<std::uint32_t>(bytes, entry),
                                         read_le<std::uint32_t>(bytes, entry + 4),
                                         read_le<std::uint16_t>(bytes, entry + 8)});
        }
      }
      result.object.sections.push_back(std::move(section));
    }

    result.object.symbols.resize(symbol_count);
    for (std::uint32_t i = 0; i < symbol_count;) {
      const std::size_t entry = static_cast<std::size_t>(symbol_offset) + static_cast<std::size_t>(i) * symbol_size;
      Symbol symbol;
      symbol.name = symbol_name(bytes, entry, strings, string_size);
      symbol.value = read_le<std::uint32_t>(bytes, entry + 8U);
      if (bigobj) {
        symbol.section_number = read_le<std::int32_t>(bytes, entry + 12U);
        symbol.type = read_le<std::uint16_t>(bytes, entry + 16U);
        symbol.storage_class = std::to_integer<std::uint8_t>(bytes[entry + 18U]);
        symbol.auxiliary_count = std::to_integer<std::uint8_t>(bytes[entry + 19U]);
      } else {
        symbol.section_number = read_le<std::int16_t>(bytes, entry + 12U);
        symbol.type = read_le<std::uint16_t>(bytes, entry + 14U);
        symbol.storage_class = std::to_integer<std::uint8_t>(bytes[entry + 16U]);
        symbol.auxiliary_count = std::to_integer<std::uint8_t>(bytes[entry + 17U]);
      }
      if (symbol.storage_class == 105U && symbol.auxiliary_count != 0U) { // IMAGE_SYM_CLASS_WEAK_EXTERNAL
        const std::size_t auxiliary = entry + symbol_size;
        if (auxiliary + symbol_size > bytes.size()) throw std::runtime_error("truncated weak-external auxiliary record");
        symbol.weak_default_index = read_le<std::uint32_t>(bytes, auxiliary);
        symbol.weak_characteristics = read_le<std::uint32_t>(bytes, auxiliary + 4U);
      }
      // IMAGE_SYM_CLASS_STATIC section symbols carry IMAGE_AUX_SYMBOL_SECTION.
      // The Selection byte is authoritative for COMDAT duplicate semantics;
      // the Number/HighNumber pair names the parent for associative COMDATs.
      if (symbol.storage_class == 3U && symbol.section_number > 0 && symbol.value == 0U &&
          symbol.type == 0U && symbol.auxiliary_count != 0U) {
        const auto section_index = static_cast<std::size_t>(symbol.section_number - 1);
        if (section_index < result.object.sections.size()) {
          const std::size_t auxiliary = entry + symbol_size;
          if (auxiliary + symbol_size > bytes.size())
            throw std::runtime_error("truncated section-definition auxiliary record");
          auto& section = result.object.sections[section_index];
          section.comdat_checksum = read_le<std::uint32_t>(bytes, auxiliary + 8U);
          const auto low = read_le<std::uint16_t>(bytes, auxiliary + 12U);
          const auto high = read_le<std::uint16_t>(bytes, auxiliary + 16U);
          section.comdat_associative_section = static_cast<std::uint32_t>(low) |
                                               (bigobj ? (static_cast<std::uint32_t>(high) << 16U) : 0U);
          section.comdat_selection = std::to_integer<std::uint8_t>(bytes[auxiliary + 14U]);
        }
      }
      result.object.symbols[i] = std::move(symbol);
      for (std::uint8_t a = 0; a < result.object.symbols[i].auxiliary_count; ++a) {
        if (i + 1U + a >= symbol_count) throw std::runtime_error("COFF auxiliary symbol exceeds table");
        result.object.symbols[i + 1U + a] = Symbol{};
      }
      i += 1U + result.object.symbols[i].auxiliary_count;
    }

    // Resolve the key/leader symbol for each non-associative COMDAT after the
    // complete symbol table is available. MSVC/clang-cl place the section
    // definition auxiliary record on a static section symbol and the COMDAT
    // key on an external symbol in that section. The first external definition
    // in symbol-table order is the leader used by link.exe/lld-style
    // prevailing-definition selection.
    for (std::uint32_t index = 0; index < result.object.symbols.size(); ++index) {
      const auto& symbol = result.object.symbols[index];
      if (symbol.storage_class != 2U || symbol.section_number <= 0 || symbol.name.empty()) continue;
      const auto section_index = static_cast<std::size_t>(symbol.section_number - 1);
      if (section_index >= result.object.sections.size()) continue;
      auto& section = result.object.sections[section_index];
      if (!section.is_comdat() || section.comdat_selection == comdat_select_associative ||
          !section.comdat_leader_name.empty()) continue;
      section.comdat_leader_symbol = index;
      section.comdat_leader_name = symbol.name;
    }
  } catch (const std::exception& e) {
    add_error(result.diagnostics, e.what());
  }
  return result;
}

ParseResult read(const std::filesystem::path& path) {
  try {
    const auto bytes = read_all(path);
    return parse(bytes, path);
  } catch (const std::exception& e) {
    ParseResult result;
    result.object.source = path;
    add_error(result.diagnostics, e.what());
    return result;
  }
}

} // namespace oblink::coff
