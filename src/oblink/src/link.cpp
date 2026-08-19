// Copyright 2026 Mario Vinciguerra
// SPDX-License-Identifier: Apache-2.0

#include "oblink/link.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <variant>

#include "oblink/coff.hpp"
#include "oblink/pe.hpp"

namespace oblink {
namespace {

std::uint32_t align_up(std::uint32_t value, std::uint32_t alignment) {
  if (alignment == 0U) return value;
  return (value + alignment - 1U) / alignment * alignment;
}

std::string hex_rva(std::uint32_t value) {
  static constexpr char digits[] = "0123456789abcdef";
  std::string out;
  for (int shift = 28; shift >= 0; shift -= 4) {
    const auto nibble = (value >> shift) & 0xFU;
    if (!out.empty() || nibble != 0U || shift == 0) out.push_back(digits[nibble]);
  }
  return out;
}

// COFF section flags that have no meaning in an image. IMAGE_SCN_ALIGN_* only
// describes how the linker should place a contribution, and the IMAGE_SCN_LNK_*
// bits describe the object file; OR-ing them across contributions would
// otherwise leave a nonsensical alignment field and a stray LNK_COMDAT bit in
// the final section table.
constexpr std::uint32_t image_section_characteristics_mask =
    0x000000E0U |  // CNT_CODE | CNT_INITIALIZED_DATA | CNT_UNINITIALIZED_DATA
    0xFE000000U;   // MEM_DISCARDABLE .. MEM_WRITE

std::uint32_t image_characteristics(std::uint32_t characteristics) {
  return characteristics & image_section_characteristics_mask;
}

std::uint32_t section_alignment(std::uint32_t characteristics) {
  const std::uint32_t encoded = (characteristics >> 20U) & 0xFU;
  if (encoded >= 1U && encoded <= 14U) return 1U << (encoded - 1U);
  return 16U;
}

struct OutputSection {
  std::string name;
  std::uint32_t characteristics{};
  std::vector<std::byte> data;
  std::uint32_t virtual_size{};
  std::uint32_t rva{};
};

struct Placement {
  std::size_t output{std::numeric_limits<std::size_t>::max()};
  std::uint32_t offset{};
};
struct Address { std::size_t output{}; std::uint32_t offset{}; };

enum class TargetKind { section, image_base, absolute };
struct Target {
  TargetKind kind{TargetKind::section};
  Address address{};
  std::uint64_t absolute{};
};

std::string canonical_section(std::string_view name) {
  // COFF '$' suffixes are subsection ordering keys, not distinct PE output
  // sections. .CRT$XCA/.CRT$XCU/.CRT$XCZ and .tls$* depend on this rule for
  // constructor/TLS initialization order.
  const auto dollar = name.find('$');
  std::string_view base = dollar == std::string_view::npos ? name : name.substr(0, dollar);
  if (base.rfind(".text", 0) == 0) return ".text";
  if (base.rfind(".rdata", 0) == 0) return ".rdata";
  if (base.rfind(".data", 0) == 0) return ".data";
  if (base.rfind(".bss", 0) == 0) return ".bss";
  if (base.rfind(".tls", 0) == 0) return ".tls";
  if (base.rfind(".idata", 0) == 0) return ".idata";
  return std::string(base.substr(0, std::min<std::size_t>(8, base.size())));
}

std::uint16_t span_u16(std::span<const std::byte> data, std::size_t offset) {
  if (offset + 2U > data.size()) throw std::runtime_error("truncated 16-bit field");
  return std::uint16_t(std::to_integer<std::uint8_t>(data[offset])) |
         (std::uint16_t(std::to_integer<std::uint8_t>(data[offset + 1U])) << 8U);
}
std::uint32_t span_u32(std::span<const std::byte> data, std::size_t offset) {
  if (offset + 4U > data.size()) throw std::runtime_error("truncated 32-bit field");
  std::uint32_t value{};
  for (std::size_t i = 0; i < 4; ++i)
    value |= std::uint32_t(std::to_integer<std::uint8_t>(data[offset + i])) << (i * 8U);
  return value;
}
std::uint32_t span_be32(std::span<const std::byte> data, std::size_t offset) {
  if (offset + 4U > data.size()) throw std::runtime_error("truncated big-endian 32-bit field");
  std::uint32_t value{};
  for (std::size_t i = 0; i < 4; ++i)
    value = (value << 8U) | std::uint32_t(std::to_integer<std::uint8_t>(data[offset + i]));
  return value;
}
std::uint32_t load_u32(const std::vector<std::byte>& data, std::size_t offset) {
  if (offset + 4U > data.size()) throw std::runtime_error("32-bit relocation exceeds section data");
  std::uint32_t value{};
  for (std::size_t i = 0; i < 4; ++i)
    value |= std::uint32_t(std::to_integer<std::uint8_t>(data[offset + i])) << (i * 8U);
  return value;
}
std::uint64_t load_u64(const std::vector<std::byte>& data, std::size_t offset) {
  if (offset + 8U > data.size()) throw std::runtime_error("64-bit relocation exceeds section data");
  std::uint64_t value{};
  for (std::size_t i = 0; i < 8; ++i)
    value |= std::uint64_t(std::to_integer<std::uint8_t>(data[offset + i])) << (i * 8U);
  return value;
}
void store_u16(std::vector<std::byte>& data, std::size_t offset, std::uint16_t value) {
  if (offset + 2U > data.size()) throw std::runtime_error("16-bit relocation exceeds section data");
  for (std::size_t i = 0; i < 2; ++i)
    data[offset + i] = static_cast<std::byte>((value >> (i * 8U)) & 0xffU);
}
void store_u32(std::vector<std::byte>& data, std::size_t offset, std::uint32_t value) {
  if (offset + 4U > data.size()) throw std::runtime_error("32-bit relocation exceeds section data");
  for (std::size_t i = 0; i < 4; ++i)
    data[offset + i] = static_cast<std::byte>((value >> (i * 8U)) & 0xffU);
}
void store_u64(std::vector<std::byte>& data, std::size_t offset, std::uint64_t value) {
  if (offset + 8U > data.size()) throw std::runtime_error("64-bit relocation exceeds section data");
  for (std::size_t i = 0; i < 8; ++i)
    data[offset + i] = static_cast<std::byte>((value >> (i * 8U)) & 0xffU);
}
void store_ascii_z(std::vector<std::byte>& data, std::size_t offset, std::string_view text) {
  if (offset + text.size() + 1U > data.size()) throw std::runtime_error("import string exceeds .idata");
  for (std::size_t i = 0; i < text.size(); ++i)
    data[offset + i] = static_cast<std::byte>(static_cast<unsigned char>(text[i]));
  data[offset + text.size()] = std::byte{0};
}

void append_u16(std::vector<std::byte>& data, std::uint16_t value) {
  data.push_back(static_cast<std::byte>(value & 0xffU));
  data.push_back(static_cast<std::byte>((value >> 8U) & 0xffU));
}

void append_u32(std::vector<std::byte>& data, std::uint32_t value) {
  for (std::size_t i = 0; i < 4U; ++i)
    data.push_back(static_cast<std::byte>((value >> (i * 8U)) & 0xffU));
}

std::vector<std::byte> read_file_bytes(const std::filesystem::path& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) throw std::runtime_error("cannot open linker input: " + path.string());
  std::vector<char> raw((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  std::vector<std::byte> bytes;
  bytes.reserve(raw.size());
  for (unsigned char ch : raw) bytes.push_back(static_cast<std::byte>(ch));
  return bytes;
}

bool is_archive(std::span<const std::byte> bytes) {
  static constexpr char magic[] = "!<arch>\n";
  if (bytes.size() < 8U) return false;
  for (std::size_t i = 0; i < 8U; ++i)
    if (std::to_integer<unsigned char>(bytes[i]) != static_cast<unsigned char>(magic[i])) return false;
  return true;
}

std::string ar_field(std::span<const std::byte> bytes, std::size_t off, std::size_t count) {
  std::string out;
  for (std::size_t i = 0; i < count; ++i)
    out.push_back(static_cast<char>(std::to_integer<unsigned char>(bytes[off + i])));
  while (!out.empty() && out.back() == ' ') out.pop_back();
  return out;
}

std::size_t ar_decimal(std::span<const std::byte> bytes, std::size_t off, std::size_t count) {
  std::size_t value = 0;
  bool digit = false;
  for (std::size_t i = 0; i < count; ++i) {
    const char c = static_cast<char>(std::to_integer<unsigned char>(bytes[off + i]));
    if (c == ' ') continue;
    if (c < '0' || c > '9') throw std::runtime_error("invalid COFF archive member size");
    digit = true;
    value = value * 10U + static_cast<unsigned>(c - '0');
  }
  if (!digit) throw std::runtime_error("missing COFF archive member size");
  return value;
}

std::string c_string(std::span<const std::byte> bytes, std::size_t& offset) {
  const std::size_t begin = offset;
  while (offset < bytes.size() && bytes[offset] != std::byte{0}) ++offset;
  if (offset >= bytes.size()) throw std::runtime_error("unterminated Microsoft import-object string");
  std::string out;
  out.reserve(offset - begin);
  for (std::size_t i = begin; i < offset; ++i)
    out.push_back(static_cast<char>(std::to_integer<unsigned char>(bytes[i])));
  ++offset;
  return out;
}

std::string lowercase(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return value;
}

struct ImportObject {
  std::filesystem::path source;
  std::string symbol;
  std::string dll;
  std::string import_name;
  std::uint16_t hint_or_ordinal{};
  std::uint8_t type{};      // IMPORT_OBJECT_CODE/DATA/CONST
  std::uint8_t name_type{}; // IMPORT_OBJECT_NAME_*
  bool by_ordinal{};
};

std::string import_iat_symbol(const ImportObject& import) {
  if (import.symbol.rfind("__imp_", 0) == 0) return import.symbol;
  return "__imp_" + import.symbol;
}

std::string import_public_symbol(const ImportObject& import) {
  if (import.symbol.rfind("__imp_", 0) == 0) return import.symbol.substr(6);
  return import.symbol;
}

std::string import_name_for_name_type(std::string symbol, std::uint8_t name_type,
                                      std::string_view export_as) {
  // The transformations below follow IMPORT_OBJECT_NAME_TYPE.  x64 C++ import
  // objects overwhelmingly use NAME (no transformation), but supporting the C
  // forms here means the same parser also handles kernel32/user32-style libs.
  if (name_type == 1U) return symbol; // NAME
  if (name_type == 2U || name_type == 3U) {
    if (!symbol.empty() && (symbol.front() == '_' || symbol.front() == '@' || symbol.front() == '?'))
      symbol.erase(symbol.begin());
    if (name_type == 3U && !symbol.empty() && symbol.front() != '?') {
      const auto at = symbol.find('@');
      if (at != std::string::npos) symbol.resize(at);
    }
    return symbol;
  }
  if (name_type == 4U && !export_as.empty()) return std::string(export_as); // EXPORTAS
  return symbol;
}

std::optional<ImportObject> parse_import_object(std::span<const std::byte> member,
                                                const std::filesystem::path& source) {
  // Microsoft "short import" objects begin with Sig1=0, Sig2=0xffff. BigObj
  // shares those first four bytes, so validate machine, payload size and the
  // packed type/name fields before accepting the member as an import object.
  if (member.size() < 20U || span_u16(member, 0) != 0U || span_u16(member, 2) != 0xffffU)
    return std::nullopt;
  if (span_u16(member, 6) != coff::machine_amd64) return std::nullopt;
  const std::uint32_t size_of_data = span_u32(member, 12);
  if (size_of_data == 0U || 20U + size_of_data > member.size()) return std::nullopt;
  const std::uint16_t type_info = span_u16(member, 18);
  const std::uint8_t type = static_cast<std::uint8_t>(type_info & 0x3U);
  const std::uint8_t name_type = static_cast<std::uint8_t>((type_info >> 2U) & 0x7U);
  if (type > 2U || name_type > 4U) return std::nullopt;

  auto payload = member.subspan(20U, size_of_data);
  std::size_t cursor = 0;
  try {
    std::string symbol = c_string(payload, cursor);
    std::string dll = c_string(payload, cursor);
    if (symbol.empty() || dll.empty()) return std::nullopt;
    std::string export_as;
    if (name_type == 4U && cursor < payload.size()) export_as = c_string(payload, cursor);
    const bool by_ordinal = name_type == 0U;
    return ImportObject{source, symbol, dll,
      by_ordinal ? std::string{} : import_name_for_name_type(symbol, name_type, export_as),
      span_u16(member, 16), type, name_type, by_ordinal};
  } catch (...) {
    return std::nullopt;
  }
}

// A member of a GNU import library that carries no image content: the
// descriptor head and the null-terminator tail. What they describe is exactly
// what ObLink synthesizes itself, so selecting one contributes nothing.
struct InertMember {};

struct ArchiveMember {
  std::size_t header_offset{};
  std::size_t data_offset{};
  std::size_t size{};
  std::string name;
  std::variant<std::monostate, coff::Object, ImportObject, InertMember> parsed;
};

struct ArchiveInput {
  std::filesystem::path path;
  std::vector<std::byte> bytes;
  std::vector<ArchiveMember> members;
  std::unordered_map<std::string, std::vector<std::size_t>> providers;
  bool indexed{};
  // DLL name per `<dll>_iname` symbol. A GNU import stub names its DLL only
  // indirectly, through a relocation against that symbol, so the defining
  // member is parsed once and the answer cached here.
  std::unordered_map<std::string, std::string> dll_names;
};

std::span<const std::byte> archive_member_bytes(const ArchiveInput& archive,
                                                const ArchiveMember& member) {
  if (member.data_offset > archive.bytes.size() || member.size > archive.bytes.size() - member.data_offset)
    throw std::runtime_error(archive.path.string() + ": archive member range is invalid");
  return std::span<const std::byte>(archive.bytes).subspan(member.data_offset, member.size);
}

const coff::Section* find_section(const coff::Object& object, std::string_view name) {
  for (const auto& section : object.sections)
    if (section.name == name) return &section;
  return nullptr;
}

// True when every section is `.idata$N`, which is the shape of a GNU import
// library's descriptor head and null-terminator tail.
bool only_import_data_sections(const coff::Object& object) {
  if (object.sections.empty()) return false;
  for (const auto& section : object.sections)
    if (section.name.rfind(".idata$", 0) != 0) return false;
  return true;
}

std::string section_c_string(const coff::Section& section, std::size_t offset) {
  std::string text;
  for (std::size_t i = offset; i < section.data.size(); ++i) {
    const auto ch = std::to_integer<unsigned char>(section.data[i]);
    if (ch == 0U) return text;
    text.push_back(static_cast<char>(ch));
  }
  return {};
}

// Resolves the DLL name a GNU import stub refers to. The stub's `.idata$7`
// relocation names the archive's descriptor head; the head's descriptor names
// the tail through its Name field; and only the tail's `.idata$7` holds the
// string. Walk that chain rather than guessing at dlltool's symbol spelling.
std::string resolve_mingw_dll_name(ArchiveInput& archive, const std::string& symbol, int depth) {
  constexpr int max_depth = 4;
  if (depth > max_depth) return {};
  auto providers = archive.providers.find(symbol);
  if (providers == archive.providers.end()) return {};
  for (const std::size_t mi : providers->second) {
    if (mi >= archive.members.size()) continue;
    const auto bytes = archive_member_bytes(archive, archive.members[mi]);
    if (bytes.size() < 2U || span_u16(bytes, 0) != coff::machine_amd64) continue;
    auto parsed = coff::parse(bytes, archive.path);
    if (!parsed.ok()) continue;
    if (const auto* name = find_section(parsed.object, ".idata$7")) {
      auto text = section_c_string(*name, 0);
      if (!text.empty()) return text;
    }
    // IMAGE_IMPORT_DESCRIPTOR::Name sits at offset 12 and relocates against the
    // symbol that labels the DLL-name string.
    if (const auto* descriptor = find_section(parsed.object, ".idata$2")) {
      for (const auto& relocation : descriptor->relocations) {
        if (relocation.virtual_address != 12U) continue;
        if (relocation.symbol_index >= parsed.object.symbols.size()) continue;
        const auto& next = parsed.object.symbols[relocation.symbol_index];
        if (next.name.empty() || next.name == symbol) continue;
        auto text = resolve_mingw_dll_name(archive, next.name, depth + 1);
        if (!text.empty()) return text;
      }
    }
  }
  return {};
}

const std::string* mingw_dll_name(ArchiveInput& archive, const std::string& symbol) {
  if (auto cached = archive.dll_names.find(symbol); cached != archive.dll_names.end())
    return cached->second.empty() ? nullptr : &cached->second;
  auto [it, _] = archive.dll_names.emplace(symbol, resolve_mingw_dll_name(archive, symbol, 0));
  return it->second.empty() ? nullptr : &it->second;
}

// GNU import libraries describe each imported symbol with a stub object rather
// than a Microsoft short-import member: `.idata$6` holds the hint and name,
// `.idata$5` defines `__imp_<symbol>`, and `.text` holds a jump thunk. All of
// that is what ObLink builds for itself, so recover the import record and
// discard the object.
std::optional<ImportObject> parse_mingw_import_stub(ArchiveInput& archive, const coff::Object& object,
                                                    const std::filesystem::path& source) {
  const auto* hint_name = find_section(object, ".idata$6");
  if (hint_name == nullptr || hint_name->data.size() < 3U) return std::nullopt;
  const std::string* dll = nullptr;
  if (const auto* iname = find_section(object, ".idata$7")) {
    // The stub's own `.idata$7` is a relocation slot naming the DLL's iname
    // symbol; the string itself lives in the archive's tail member.
    for (const auto& relocation : iname->relocations) {
      if (relocation.symbol_index >= object.symbols.size()) continue;
      const auto& symbol = object.symbols[relocation.symbol_index];
      if (symbol.name.empty()) continue;
      dll = mingw_dll_name(archive, symbol.name);
      if (dll != nullptr) break;
    }
  }
  if (dll == nullptr) return std::nullopt;

  std::string imported;
  for (const auto& symbol : object.symbols) {
    if (symbol.storage_class != 2U || symbol.name.rfind("__imp_", 0) != 0) continue;
    if (symbol.section_number <= 0) continue;
    imported = symbol.name.substr(6);
    break;
  }
  if (imported.empty()) return std::nullopt;

  ImportObject import;
  import.source = source;
  import.symbol = imported;
  import.dll = *dll;
  import.hint_or_ordinal = span_u16(std::span<const std::byte>(hint_name->data), 0);
  import.import_name = section_c_string(*hint_name, 2U);
  if (import.import_name.empty()) return std::nullopt;
  import.name_type = 1U; // IMPORT_OBJECT_NAME: use the string as written.
  // A stub with executable content exports a callable address; one without is a
  // data import addressed only through its IAT slot.
  const auto* text = find_section(object, ".text");
  import.type = (text != nullptr && !text->data.empty()) ? 0U : 1U;
  import.by_ordinal = false;
  return import;
}

void parse_archive_member(ArchiveInput& archive, std::size_t index) {
  auto& member = archive.members.at(index);
  if (!std::holds_alternative<std::monostate>(member.parsed)) return;
  const auto bytes = archive_member_bytes(archive, member);
  const auto source = archive.path.string() + "(" + member.name + ")";
  if ((bytes.size() >= 2U && span_u16(bytes, 0) == coff::machine_amd64) || coff::is_bigobj(bytes)) {
    auto parsed = coff::parse(bytes, source);
    if (!parsed.ok())
      throw std::runtime_error(parsed.object.source.string() + ": " + parsed.diagnostics.front().message);
    if (auto stub = parse_mingw_import_stub(archive, parsed.object, source)) {
      member.parsed = std::move(*stub);
      return;
    }
    if (only_import_data_sections(parsed.object)) {
      member.parsed = InertMember{};
      return;
    }
    member.parsed = std::move(parsed.object);
    return;
  }
  if (auto imported = parse_import_object(bytes, source)) {
    member.parsed = std::move(*imported);
    return;
  }
  throw std::runtime_error(source + ": unsupported selected COFF archive member");
}

// Records what a member defines without interpreting or caching it. Import-stub
// recognition needs a complete provider map to resolve a DLL name, so an
// archive that lacks a linker index has to be indexed before any member is
// classified -- otherwise whether a stub is recognized would depend on where it
// happens to sit in the archive.
void index_archive_member_symbols(ArchiveInput& archive, std::size_t index) {
  const auto bytes = archive_member_bytes(archive, archive.members[index]);
  if ((bytes.size() >= 2U && span_u16(bytes, 0) == coff::machine_amd64) || coff::is_bigobj(bytes)) {
    auto parsed = coff::parse(bytes, archive.path);
    if (!parsed.ok()) return;
    for (const auto& symbol : parsed.object.symbols) {
      if (symbol.name.empty() || symbol.storage_class != 2U) continue;
      if (symbol.section_number > 0 || (symbol.section_number == 0 && symbol.value != 0U))
        archive.providers[symbol.name].push_back(index);
    }
    return;
  }
  if (auto imported = parse_import_object(bytes, archive.path)) {
    archive.providers[import_public_symbol(*imported)].push_back(index);
    archive.providers[import_iat_symbol(*imported)].push_back(index);
  }
}

ArchiveInput read_archive(const std::filesystem::path& path, std::vector<std::byte> bytes) {
  ArchiveInput archive{path, std::move(bytes), {}, {}, false};
  std::optional<std::pair<std::size_t, std::size_t>> first_linker_member;
  std::unordered_map<std::size_t, std::size_t> member_by_header_offset;
  std::size_t off = 8U;
  while (off < archive.bytes.size()) {
    const auto all = std::span<const std::byte>(archive.bytes);
    if (off + 60U > all.size()) throw std::runtime_error(path.string() + ": truncated COFF archive header");
    if (std::to_integer<unsigned char>(all[off + 58]) != '`' ||
        std::to_integer<unsigned char>(all[off + 59]) != '\n')
      throw std::runtime_error(path.string() + ": invalid COFF archive member header");
    const std::string raw_name = ar_field(all, off, 16U);
    const std::size_t size = ar_decimal(all, off + 48U, 10U);
    const std::size_t data = off + 60U;
    if (data + size > all.size()) throw std::runtime_error(path.string() + ": truncated COFF archive member");

    // The first '/' member is the MSVC/System-V archive symbol index. Keeping
    // its member-offset -> symbol mapping means a 100MB CRT library does not
    // require parsing thousands of irrelevant objects merely to resolve one
    // undefined name.
    if (raw_name == "/" && !first_linker_member)
      first_linker_member = std::make_pair(data, size);

    const bool metadata = raw_name == "/" || raw_name == "//" ||
                          raw_name == "/SYM64/" || raw_name == "/SYM64";
    if (!metadata && !raw_name.empty()) {
      const std::size_t member_index = archive.members.size();
      archive.members.push_back({off, data, size, raw_name, {}});
      member_by_header_offset.emplace(off, member_index);
    }
    off = data + size;
    if (off & 1U) ++off;
  }

  if (first_linker_member) {
    const auto [data, size] = *first_linker_member;
    const auto index = std::span<const std::byte>(archive.bytes).subspan(data, size);
    if (index.size() >= 4U) {
      const std::uint32_t count = span_be32(index, 0U);
      const std::uint64_t offsets_end = 4ULL + static_cast<std::uint64_t>(count) * 4ULL;
      if (offsets_end <= index.size()) {
        std::size_t names = static_cast<std::size_t>(offsets_end);
        bool valid = true;
        for (std::uint32_t i = 0; i < count; ++i) {
          const std::uint32_t member_offset = span_be32(index, 4U + static_cast<std::size_t>(i) * 4U);
          const std::size_t begin = names;
          while (names < index.size() && index[names] != std::byte{0}) ++names;
          if (names >= index.size()) { valid = false; break; }
          std::string symbol;
          symbol.reserve(names - begin);
          for (std::size_t p = begin; p < names; ++p)
            symbol.push_back(static_cast<char>(std::to_integer<unsigned char>(index[p])));
          ++names;
          if (auto member = member_by_header_offset.find(member_offset);
              member != member_by_header_offset.end() && !symbol.empty())
            archive.providers[symbol].push_back(member->second);
        }
        archive.indexed = valid && !archive.providers.empty();
      }
    }
  }

  // Hand-created archives and a few Unix archive variants may omit a linker
  // index. Keep them supported by building the same provider map once. Real
  // MSVC/Windows SDK libraries take the indexed path above and remain truly
  // lazy all the way down to object parsing.
  if (!archive.indexed) {
    archive.providers.clear();
    for (std::size_t i = 0; i < archive.members.size(); ++i) {
      try { index_archive_member_symbols(archive, i); }
      catch (const std::exception&) {
        // Ignore unrecognized members until/unless a real archive index names
        // them. Indexless archives used by ObLink tests contain only COFF data.
      }
    }
  }
  return archive;
}

void collect_defined(const coff::Object& object, std::unordered_set<std::string>& defined) {
  for (const auto& symbol : object.symbols)
    if (!symbol.name.empty() && symbol.storage_class == 2U &&
        (symbol.section_number > 0 || symbol.section_number == -1 ||
         (symbol.section_number == 0 && symbol.value != 0U)))
      defined.insert(symbol.name);
}

void collect_required(const coff::Object& object, const std::unordered_set<std::string>& defined,
                      std::unordered_set<std::string>& required) {
  for (const auto& section : object.sections) {
    // Debug sections never reach the image, so the symbols their relocations
    // name are not live. Counting them would drag archive members into the link
    // for the sake of records that are about to be dropped.
    if (section.name.rfind(".debug", 0) == 0) continue;
    for (const auto& reloc : section.relocations) {
      if (reloc.symbol_index >= object.symbols.size()) continue;
      const auto& symbol = object.symbols[reloc.symbol_index];
      if (symbol.section_number == 0 && symbol.value == 0U && !symbol.name.empty() && !defined.contains(symbol.name)) {
        required.insert(symbol.name);
        if (symbol.storage_class == 105U && symbol.weak_default_index < object.symbols.size()) {
          const auto& fallback = object.symbols[symbol.weak_default_index];
          if (!fallback.name.empty() && !defined.contains(fallback.name)) required.insert(fallback.name);
        }
      }
    }
  }
}


std::vector<std::string> directive_tokens(std::string_view text) {
  std::vector<std::string> tokens;
  std::string token;
  bool quoted = false;
  for (char c : text) {
    if (c == '"') { quoted = !quoted; continue; }
    if (!quoted && std::isspace(static_cast<unsigned char>(c))) {
      if (!token.empty()) { tokens.push_back(std::move(token)); token.clear(); }
    } else token.push_back(c);
  }
  if (!token.empty()) tokens.push_back(std::move(token));
  return tokens;
}

void collect_directives(const coff::Object& object,
                        std::vector<std::string>& libraries,
                        std::vector<std::filesystem::path>& library_paths,
                        std::unordered_map<std::string, std::string>& aliases,
                        std::unordered_set<std::string>& forced_symbols,
                        std::unordered_map<std::string, std::string>& fail_if_mismatch,
                        std::unordered_set<std::string>& suppressed_default_libraries,
                        bool& suppress_all_default_libraries) {
  for (const auto& section : object.sections) {
    if (section.name != ".drectve") continue;
    std::string text;
    text.reserve(section.data.size());
    for (const auto byte : section.data)
      if (byte != std::byte{0}) text.push_back(static_cast<char>(std::to_integer<unsigned char>(byte)));
    for (auto token : directive_tokens(text)) {
      const std::string lower = lowercase(token);
      constexpr std::string_view defaultlib = "/defaultlib:";
      constexpr std::string_view alt_defaultlib = "-defaultlib:";
      constexpr std::string_view libpath = "/libpath:";
      constexpr std::string_view alternatename = "/alternatename:";
      constexpr std::string_view include = "/include:";
      constexpr std::string_view mismatch = "/failifmismatch:";
      constexpr std::string_view nodefaultlib = "/nodefaultlib:";
      if (lower.rfind(defaultlib, 0) == 0 && token.size() > defaultlib.size())
        libraries.push_back(token.substr(defaultlib.size()));
      else if (lower.rfind(alt_defaultlib, 0) == 0 && token.size() > alt_defaultlib.size())
        libraries.push_back(token.substr(alt_defaultlib.size()));
      else if (lower.rfind(libpath, 0) == 0 && token.size() > libpath.size())
        library_paths.emplace_back(token.substr(libpath.size()));
      else if (lower.rfind(alternatename, 0) == 0 && token.size() > alternatename.size()) {
        const std::string value = token.substr(alternatename.size());
        const auto equal = value.find('=');
        if (equal != std::string::npos && equal != 0U && equal + 1U < value.size())
          aliases.emplace(value.substr(0, equal), value.substr(equal + 1U));
      } else if (lower.rfind(include, 0) == 0 && token.size() > include.size()) {
        forced_symbols.insert(token.substr(include.size()));
      } else if (lower.rfind(mismatch, 0) == 0 && token.size() > mismatch.size()) {
        const std::string value = token.substr(mismatch.size());
        const auto equal = value.find('=');
        if (equal != std::string::npos && equal != 0U) {
          const std::string key = lowercase(value.substr(0, equal));
          const std::string required = value.substr(equal + 1U);
          if (auto [it, inserted] = fail_if_mismatch.emplace(key, required); !inserted && it->second != required)
            throw std::runtime_error("/FAILIFMISMATCH conflict for '" + value.substr(0, equal) +
                                     "': '" + it->second + "' vs '" + required + "'");
        }
      } else if (lower == "/nodefaultlib" || lower == "-nodefaultlib") {
        suppress_all_default_libraries = true;
      } else if (lower.rfind(nodefaultlib, 0) == 0 && token.size() > nodefaultlib.size()) {
        std::string lib = lowercase(token.substr(nodefaultlib.size()));
        if (lib.size() > 4U && lib.substr(lib.size() - 4U) == ".lib") lib.resize(lib.size() - 4U);
        suppressed_default_libraries.insert(std::move(lib));
      }
    }
  }
}

bool foreign_windows_library_path(const std::filesystem::path& path) {
#if defined(_WIN32)
  const std::string lower = lowercase(path.string());
  return lower.find("strawberry") != std::string::npos ||
         lower.find("mingw") != std::string::npos ||
         lower.find("msys") != std::string::npos;
#else
  (void)path;
  return false;
#endif
}

#if defined(_WIN32)
std::optional<std::filesystem::path> newest_child_directory(const std::filesystem::path& root) {
  std::error_code ec;
  if (!std::filesystem::is_directory(root, ec)) return std::nullopt;
  std::optional<std::filesystem::path> best;
  for (const auto& entry : std::filesystem::directory_iterator(root, ec)) {
    if (ec) break;
    if (!entry.is_directory(ec)) continue;
    if (!best || entry.path().filename().string() > best->filename().string()) best = entry.path();
  }
  return best;
}

void append_windows_toolchain_paths(std::vector<std::filesystem::path>& paths) {
  // Normal installed Raz shells do not inherit VsDevCmd's LIB variable. Locate
  // the same MSVC and Windows SDK import-library roots directly so ObLink can
  // remain the linker without depending on clang-cl/link.exe for discovery.
  std::vector<std::filesystem::path> program_roots;
  for (const char* env : {"ProgramFiles", "ProgramFiles(x86)"}) {
    if (const char* raw = std::getenv(env); raw != nullptr && *raw != '\0')
      program_roots.emplace_back(raw);
  }

  for (const auto& root : program_roots) {
    const auto visual_studio = root / "Microsoft Visual Studio";
    std::error_code ec;
    if (std::filesystem::is_directory(visual_studio, ec)) {
      for (const auto& generation : std::filesystem::directory_iterator(visual_studio, ec)) {
        if (ec || !generation.is_directory(ec)) continue;
        for (const auto& edition : std::filesystem::directory_iterator(generation.path(), ec)) {
          if (ec || !edition.is_directory(ec)) continue;
          const auto tools = edition.path() / "VC" / "Tools" / "MSVC";
          if (auto version = newest_child_directory(tools)) {
            const auto lib = *version / "lib" / "x64";
            if (std::filesystem::is_directory(lib, ec)) paths.push_back(lib);
          }
        }
      }
    }

    const auto kit_lib = root / "Windows Kits" / "10" / "Lib";
    if (auto version = newest_child_directory(kit_lib)) {
      const auto ucrt = *version / "ucrt" / "x64";
      const auto um = *version / "um" / "x64";
      if (std::filesystem::is_directory(ucrt, ec)) paths.push_back(ucrt);
      if (std::filesystem::is_directory(um, ec)) paths.push_back(um);
    }
  }
}
#endif

void append_environment_paths(std::vector<std::filesystem::path>& paths, const char* name) {
  const char* raw = std::getenv(name);
  if (raw == nullptr || *raw == '\0') return;
  std::string value(raw);
#if defined(_WIN32)
  constexpr char separator = ';';
#else
  // Tests can emulate a Windows LIB list on non-Windows hosts with ';'. If no
  // semicolon is present, honor the host's conventional colon separator.
  const char separator = value.find(';') != std::string::npos ? ';' : ':';
#endif
  std::size_t begin = 0;
  while (begin <= value.size()) {
    const auto end = value.find(separator, begin);
    const auto part = value.substr(begin, end == std::string::npos ? std::string::npos : end - begin);
    if (!part.empty()) {
      std::filesystem::path path(part);
      if (!foreign_windows_library_path(path)) paths.push_back(std::move(path));
    }
    if (end == std::string::npos) break;
    begin = end + 1;
  }
}

std::optional<std::filesystem::path> find_library(std::string name,
                                                  const std::vector<std::filesystem::path>& paths) {
  if (name.size() >= 2U && name.front() == '"' && name.back() == '"')
    name = name.substr(1, name.size() - 2U);
  std::filesystem::path direct(name);
  if (direct.has_parent_path() && std::filesystem::is_regular_file(direct)) return direct;
  if (lowercase(direct.extension().string()) != ".lib") direct += ".lib";
  if (std::filesystem::is_regular_file(direct)) return direct;
  for (const auto& path : paths) {
    const auto candidate = path / direct.filename();
    if (std::filesystem::is_regular_file(candidate)) return candidate;
  }
  return std::nullopt;
}

struct LoadedInputs {
  std::vector<coff::Object> objects;
  std::vector<ImportObject> imports;
  std::unordered_map<std::string, std::string> aliases;
  std::unordered_set<std::string> forced_symbols;
  std::string entry;
  std::vector<std::string> trace;
};

LoadedInputs load_link_objects(const std::vector<std::filesystem::path>& inputs,
                               const LinkOptions& options) {
  LoadedInputs loaded;
  if (!options.entry.empty()) {
    loaded.entry = options.entry;
    loaded.forced_symbols.insert(options.entry);
  }
  std::vector<ArchiveInput> archives;
  std::unordered_set<std::string> loaded_archive_paths;
  std::vector<std::filesystem::path> search_paths = options.library_paths;
  for (const auto& input : inputs)
    if (!input.parent_path().empty()) search_paths.push_back(input.parent_path());
  append_environment_paths(search_paths, "LIB");
  append_environment_paths(search_paths, "LIBPATH");
#if defined(_WIN32)
  append_windows_toolchain_paths(search_paths);
#endif

  auto add_archive = [&](const std::filesystem::path& path) {
    std::error_code ec;
    const auto absolute = std::filesystem::absolute(path, ec);
    const std::string key = lowercase((ec ? path : absolute).lexically_normal().string());
    if (!loaded_archive_paths.insert(key).second) return false;
    const auto bytes = read_file_bytes(path);
    if (!is_archive(bytes)) throw std::runtime_error(path.string() + ": expected COFF archive library");
    archives.push_back(read_archive(path, std::move(bytes)));
    if (!path.parent_path().empty()) search_paths.push_back(path.parent_path());
    return true;
  };

  for (const auto& input : inputs) {
    const auto bytes = read_file_bytes(input);
    if (is_archive(bytes)) { add_archive(input); continue; }
    auto parsed = coff::parse(bytes, input);
    if (!parsed.ok()) throw std::runtime_error(input.string() + ": " + parsed.diagnostics.front().message);
    loaded.objects.push_back(std::move(parsed.object));
  }

  std::vector<std::string> requested_libraries = options.libraries;
  std::unordered_set<std::string> requested_seen;
  std::unordered_set<std::string> selected_import_keys;
  std::unordered_map<std::string, std::string> fail_if_mismatch;
  std::unordered_set<std::string> suppressed_default_libraries;
  bool suppress_all_default_libraries = false;
  std::vector<std::vector<bool>> used_members;

  for (;;) {
    while (used_members.size() < archives.size())
      used_members.emplace_back(archives[used_members.size()].members.size(), false);

    // Newly selected C/C++ object members can contribute /DEFAULTLIB and
    // /LIBPATH directives. Discover them every round; de-duplication below
    // makes the fixed-point inexpensive and deterministic.
    std::vector<std::string> directive_libraries;
    std::vector<std::filesystem::path> directive_paths;
    for (const auto& object : loaded.objects)
      collect_directives(object, directive_libraries, directive_paths, loaded.aliases, loaded.forced_symbols,
                         fail_if_mismatch, suppressed_default_libraries, suppress_all_default_libraries);
    search_paths.insert(search_paths.end(), directive_paths.begin(), directive_paths.end());
    if (!suppress_all_default_libraries) {
      for (const auto& library : directive_libraries) {
        std::string key = lowercase(library);
        if (key.size() > 4U && key.substr(key.size() - 4U) == ".lib") key.resize(key.size() - 4U);
        if (!suppressed_default_libraries.contains(key)) requested_libraries.push_back(library);
      }
    }

    bool changed = false;
    for (const auto& library : requested_libraries) {
      const std::string key = lowercase(library);
      if (!requested_seen.insert(key).second) continue;
      if (auto path = find_library(library, search_paths)) changed |= add_archive(*path);
      // A missing default library is not immediately fatal: another object or
      // import may satisfy all symbols without it. Any truly required symbol
      // will still produce the precise unresolved-symbol diagnostic later.
    }

    while (used_members.size() < archives.size())
      used_members.emplace_back(archives[used_members.size()].members.size(), false);

    std::unordered_set<std::string> defined;
    for (const auto& object : loaded.objects) collect_defined(object, defined);
    for (const auto& import : loaded.imports) {
      defined.insert(import_public_symbol(import));
      defined.insert(import_iat_symbol(import));
    }

    // link.exe/lld-link do not normally use C/C++ main as the PE loader entry.
    // If the selected Windows libraries provide a CRT startup root, pull that
    // archive member and use it as the image entry. A truly freestanding Raz
    // image with no startup provider retains direct-main behavior.
    if (options.entry.empty()) {
      std::string user_entry;
      std::string startup;
      if (options.subsystem == 2U) {
        if (defined.contains("wWinMain")) { user_entry = "wWinMain"; startup = "wWinMainCRTStartup"; }
        else if (defined.contains("WinMain")) { user_entry = "WinMain"; startup = "WinMainCRTStartup"; }
      } else {
        if (defined.contains("wmain")) { user_entry = "wmain"; startup = "wmainCRTStartup"; }
        else if (defined.contains("main")) { user_entry = "main"; startup = "mainCRTStartup"; }
      }
      if (user_entry.empty()) user_entry = "main";
      bool startup_available = defined.contains(startup);
      if (!startup.empty() && !startup_available) {
        for (const auto& archive : archives) {
          if (archive.providers.contains(startup)) { startup_available = true; break; }
        }
      }
      if (options.infer_crt_startup && !startup.empty() && startup_available) {
        loaded.entry = startup;
        loaded.forced_symbols.insert(startup);
      } else if (loaded.entry.empty() || loaded.entry == startup) {
        loaded.entry = user_entry;
      }
    }

    std::unordered_set<std::string> required;
    for (const auto& object : loaded.objects) collect_required(object, defined, required);
    for (const auto& forced : loaded.forced_symbols)
      if (!defined.contains(forced)) required.insert(forced);
    bool alias_added = true;
    while (alias_added) {
      alias_added = false;
      std::vector<std::string> additions;
      for (const auto& name : required) {
        auto it = loaded.aliases.find(name);
        if (it != loaded.aliases.end() && !defined.contains(it->second) && !required.contains(it->second))
          additions.push_back(it->second);
      }
      for (const auto& name : additions) alias_added |= required.insert(name).second;
    }

    // Resolve archive symbols through the linker index, one provider at a
    // time. A selected member is parsed only here, when a real undefined symbol
    // demands it. This matches the core lazy-archive behavior of link.exe/lld.
    std::vector<std::string> unresolved(required.begin(), required.end());
    std::sort(unresolved.begin(), unresolved.end());
    for (const auto& name : unresolved) {
      if (defined.contains(name)) continue;
      bool satisfied = false;
      for (std::size_t ai = 0; ai < archives.size() && !satisfied; ++ai) {
        auto providers = archives[ai].providers.find(name);
        if (providers == archives[ai].providers.end()) continue;
        for (const std::size_t mi : providers->second) {
          if (mi >= used_members[ai].size() || used_members[ai][mi]) continue;
          used_members[ai][mi] = true;
          parse_archive_member(archives[ai], mi);
          auto& member = archives[ai].members[mi];
          if (auto* object = std::get_if<coff::Object>(&member.parsed)) {
            loaded.objects.push_back(std::move(*object));
            collect_defined(loaded.objects.back(), defined);
            changed = true;
            satisfied = defined.contains(name);
            if (options.verbose)
              loaded.trace.push_back("pulled " + loaded.objects.back().source.string() + " for " + name);
          } else if (auto* import = std::get_if<ImportObject>(&member.parsed)) {
            const std::string import_key = lowercase(import->dll) + "\n" + import->symbol;
            if (selected_import_keys.insert(import_key).second) loaded.imports.push_back(*import);
            defined.insert(import_public_symbol(*import));
            defined.insert(import_iat_symbol(*import));
            changed = true;
            satisfied = defined.contains(name);
            if (options.verbose)
              loaded.trace.push_back("imported " + import->symbol + " from " + import->dll + " for " + name);
          }
          if (satisfied) break;
        }
      }
    }
    if (!changed) break;
  }
  return loaded;
}

struct RuntimeImport {
  ImportObject spec;
  std::uint32_t thunk_offset{std::numeric_limits<std::uint32_t>::max()};
  std::uint32_t iat_offset{};
  std::uint32_t hint_name_offset{};
};
struct ImportGroup {
  std::string dll;
  std::vector<std::size_t> members;
  std::uint32_t ilt_offset{};
  std::uint32_t iat_offset{};
  std::uint32_t dll_name_offset{};
};

struct ImportLayout {
  std::vector<RuntimeImport> imports;
  std::vector<ImportGroup> groups;
  std::uint32_t descriptor_offset{};
  std::uint32_t descriptor_size{};
  std::uint32_t iat_begin{};
  std::uint32_t iat_end{};
  std::uint32_t total_size{};
};

ImportLayout plan_imports(std::vector<ImportObject> imports) {
  ImportLayout layout;
  std::sort(imports.begin(), imports.end(), [](const ImportObject& a, const ImportObject& b) {
    const auto ad = lowercase(a.dll), bd = lowercase(b.dll);
    if (ad != bd) return ad < bd;
    return a.symbol < b.symbol;
  });
  for (auto& import : imports) layout.imports.push_back({std::move(import)});
  for (std::size_t i = 0; i < layout.imports.size(); ++i) {
    if (layout.groups.empty() || lowercase(layout.groups.back().dll) != lowercase(layout.imports[i].spec.dll))
      layout.groups.push_back({layout.imports[i].spec.dll, {}});
    layout.groups.back().members.push_back(i);
  }

  std::uint32_t cursor = 0;
  layout.descriptor_offset = cursor;
  layout.descriptor_size = static_cast<std::uint32_t>((layout.groups.size() + 1U) * 20U);
  cursor += layout.descriptor_size;
  cursor = align_up(cursor, 8U);
  for (auto& group : layout.groups) {
    group.ilt_offset = cursor;
    cursor += static_cast<std::uint32_t>((group.members.size() + 1U) * 8U);
  }
  cursor = align_up(cursor, 8U);
  layout.iat_begin = cursor;
  for (auto& group : layout.groups) {
    group.iat_offset = cursor;
    for (std::size_t n = 0; n < group.members.size(); ++n)
      layout.imports[group.members[n]].iat_offset = cursor + static_cast<std::uint32_t>(n * 8U);
    cursor += static_cast<std::uint32_t>((group.members.size() + 1U) * 8U);
  }
  layout.iat_end = cursor;
  cursor = align_up(cursor, 2U);
  for (auto& import : layout.imports) {
    if (import.spec.by_ordinal) continue;
    import.hint_name_offset = cursor;
    cursor += 2U + static_cast<std::uint32_t>(import.spec.import_name.size()) + 1U;
    cursor = align_up(cursor, 2U);
  }
  for (auto& group : layout.groups) {
    group.dll_name_offset = cursor;
    cursor += static_cast<std::uint32_t>(group.dll.size()) + 1U;
  }
  layout.total_size = cursor;
  return layout;
}


struct DefinitionRef {
  std::size_t object{};
  std::size_t section{};
};

std::uint32_t section_logical_size(const coff::Section& section) {
  return std::max(section.virtual_size, static_cast<std::uint32_t>(section.data.size()));
}

std::string relocation_target_name(const coff::Object& object, const coff::Relocation& relocation) {
  if (relocation.symbol_index >= object.symbols.size()) return {};
  return object.symbols[relocation.symbol_index].name;
}

bool comdat_exact_match(const coff::Object& a_object, std::size_t a_index,
                        const coff::Object& b_object, std::size_t b_index) {
  const auto& a = a_object.sections[a_index];
  const auto& b = b_object.sections[b_index];
  if (section_logical_size(a) != section_logical_size(b) || a.data != b.data ||
      a.relocations.size() != b.relocations.size()) return false;
  if (a.comdat_checksum != 0U && b.comdat_checksum != 0U && a.comdat_checksum != b.comdat_checksum)
    return false;
  for (std::size_t i = 0; i < a.relocations.size(); ++i) {
    const auto& ar = a.relocations[i];
    const auto& br = b.relocations[i];
    if (ar.virtual_address != br.virtual_address || ar.type != br.type ||
        relocation_target_name(a_object, ar) != relocation_target_name(b_object, br)) return false;
  }
  return true;
}

std::uint8_t effective_comdat_selection(const coff::Section& section) {
  return section.comdat_selection != 0U ? section.comdat_selection : coff::comdat_select_any;
}

// Decide which duplicate COMDAT sections participate before output layout.
// This is important for data COMDATs such as MSVC's std::nothrow: keeping both
// byte ranges and merely choosing one global symbol would still leave duplicate
// constructors/relocations in the final image.
std::vector<std::vector<bool>> select_coff_sections(const std::vector<coff::Object>& objects) {
  std::vector<std::vector<bool>> selected(objects.size());
  for (std::size_t oi = 0; oi < objects.size(); ++oi)
    selected[oi].assign(objects[oi].sections.size(), true);

  // COFF COMDAT selection is keyed by the leader symbol for the section. Do
  // not run duplicate selection independently for every external symbol in a
  // section: MSVC sections can contain additional externally-visible symbols
  // that are not the COMDAT key. This mirrors lld-link's pending-COMDAT model
  // closely enough for AMD64 objects while keeping ObLink's representation
  // compact.
  std::unordered_map<std::string, DefinitionRef> leaders;
  for (std::size_t oi = 0; oi < objects.size(); ++oi) {
    for (std::size_t si = 0; si < objects[oi].sections.size(); ++si) {
      const auto& current = objects[oi].sections[si];
      if (!current.is_comdat() || current.comdat_selection == coff::comdat_select_associative)
        continue;
      // A COMDAT whose only definitions are static has no name another object
      // can collide with, so it is not a duplicate candidate. MSVC produces
      // these for internal-linkage functions under /Gy. Every object keeps its
      // own copy; only externally keyed COMDATs take part in selection.
      if (current.comdat_leader_name.empty()) continue;

      auto [it, inserted] = leaders.emplace(current.comdat_leader_name, DefinitionRef{oi, si});
      if (inserted) continue;
      auto prior_ref = it->second;
      if (!selected[prior_ref.object][prior_ref.section]) {
        it->second = {oi, si};
        continue;
      }
      const auto& prior = objects[prior_ref.object].sections[prior_ref.section];
      auto prior_select = effective_comdat_selection(prior);
      auto current_select = effective_comdat_selection(current);

      // link.exe/lld-link accept ANY mixed with LARGEST and resolve the pair as
      // LARGEST (MSVC uses this for some RTTI/vftable configurations).
      if ((prior_select == coff::comdat_select_any && current_select == coff::comdat_select_largest) ||
          (current_select == coff::comdat_select_any && prior_select == coff::comdat_select_largest)) {
        prior_select = current_select = coff::comdat_select_largest;
      }

      if (prior_select == coff::comdat_select_noduplicates ||
          current_select == coff::comdat_select_noduplicates)
        throw std::runtime_error("duplicate COMDAT prohibited by NODUPLICATES: " + current.comdat_leader_name);
      if (prior_select != current_select)
        throw std::runtime_error("conflicting COMDAT selection policies for: " + current.comdat_leader_name);

      bool choose_current = false;
      switch (prior_select) {
        case coff::comdat_select_any:
          break;
        case coff::comdat_select_same_size:
          if (section_logical_size(prior) != section_logical_size(current))
            throw std::runtime_error("COMDAT SAME_SIZE mismatch for: " + current.comdat_leader_name);
          break;
        case coff::comdat_select_exact_match:
          if (!comdat_exact_match(objects[prior_ref.object], prior_ref.section, objects[oi], si))
            throw std::runtime_error("COMDAT EXACT_MATCH mismatch for: " + current.comdat_leader_name);
          break;
        case coff::comdat_select_largest:
          choose_current = section_logical_size(current) > section_logical_size(prior);
          break;
        case coff::comdat_select_newest:
          // ObLink's deterministic mode intentionally avoids object timestamps;
          // stable input/archive order provides a deterministic 'newest'.
          choose_current = true;
          break;
        case coff::comdat_select_associative:
          throw std::runtime_error("associative COMDAT unexpectedly used as a leader");
        default:
          throw std::runtime_error("unsupported COMDAT selection policy for: " + current.comdat_leader_name);
      }

      if (choose_current) {
        selected[prior_ref.object][prior_ref.section] = false;
        it->second = {oi, si};
      } else {
        selected[oi][si] = false;
      }
    }
  }

  // Associative COMDATs are followers, never independent duplicate keys. They
  // survive iff their parent section in the same object survives. Repeat for
  // nested associations.
  bool changed = true;
  while (changed) {
    changed = false;
    for (std::size_t oi = 0; oi < objects.size(); ++oi) {
      for (std::size_t si = 0; si < objects[oi].sections.size(); ++si) {
        const auto& section = objects[oi].sections[si];
        if (!selected[oi][si] || section.comdat_selection != coff::comdat_select_associative)
          continue;
        if (section.comdat_associative_section == 0U)
          throw std::runtime_error(objects[oi].source.string() + ": associative COMDAT has no parent");
        const auto parent = static_cast<std::size_t>(section.comdat_associative_section - 1U);
        if (parent >= selected[oi].size())
          throw std::runtime_error("associative COMDAT references invalid parent section");
        if (!selected[oi][parent]) {
          selected[oi][si] = false;
          changed = true;
        }
      }
    }
  }
  return selected;
}

void write_atomic(const std::filesystem::path& path, std::span<const std::byte> bytes) {
  if (!path.parent_path().empty()) std::filesystem::create_directories(path.parent_path());
  const auto tmp = std::filesystem::path(path.string() + ".tmp");
  {
    std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
    if (!out) throw std::runtime_error("cannot create output file: " + tmp.string());
    out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!out) throw std::runtime_error("failed while writing output file: " + tmp.string());
  }
  std::error_code ec;
  std::filesystem::rename(tmp, path, ec);
  if (ec) {
    std::filesystem::remove(path, ec); ec.clear();
    std::filesystem::rename(tmp, path, ec);
    if (ec) throw std::runtime_error("cannot commit output file: " + path.string());
  }
}

} // namespace

LinkResult link(const std::vector<std::filesystem::path>& inputs, const LinkOptions& options) {
  LinkResult result;
  result.output = options.output;
  result.input_count = inputs.size();
  try {
    if (inputs.empty()) throw std::runtime_error("no input object files");
    if (options.output.empty()) throw std::runtime_error("output path is required");

    LoadedInputs loaded = load_link_objects(inputs, options);
    result.trace = std::move(loaded.trace);
    auto& objects = loaded.objects;
    const auto selected_sections = select_coff_sections(objects);

    // A PE has exactly one import directory, and ObLink builds it from
    // Microsoft short-import members. MinGW-style import libraries instead ship
    // IMAGE_IMPORT_DESCRIPTOR records inside ordinary `.idata$2` sections and
    // rely on GNU `.idata$N` grouping rules to delimit each DLL's thunk arrays.
    // Replaying that data here would publish descriptors whose thunk arrays are
    // mis-delimited, so refuse the input instead of emitting an image whose
    // imports are silently wrong.
    for (std::size_t oi = 0; oi < objects.size(); ++oi)
      for (std::size_t si = 0; si < objects[oi].sections.size(); ++si)
        if (selected_sections[oi][si] && objects[oi].sections[si].name.rfind(".idata$2", 0) == 0 &&
            !objects[oi].sections[si].data.empty())
          throw std::runtime_error(
              objects[oi].source.string() +
              ": GNU-style import descriptors (.idata$2) are not supported; provide a Microsoft "
              "import library for this DLL");
    ImportLayout import_layout = plan_imports(std::move(loaded.imports));

    std::vector<OutputSection> outputs;
    std::unordered_map<std::string, std::size_t> output_by_name;
    std::vector<std::vector<Placement>> placements(objects.size());
    for (std::size_t oi = 0; oi < objects.size(); ++oi)
      placements[oi].resize(objects[oi].sections.size());

    struct Contribution { std::size_t object{}; std::size_t section{}; };
    std::vector<std::vector<Contribution>> contributions;
    for (std::size_t oi = 0; oi < objects.size(); ++oi) {
      for (std::size_t si = 0; si < objects[oi].sections.size(); ++si) {
        const auto& input = objects[oi].sections[si];
        // .debug$S/.debug$T carry CodeView records destined for a PDB, not for
        // the image. Without debug output there is nothing to publish them
        // into, and their SECREL/SECTION relocations describe symbols that need
        // not exist in the image at all, so drop them the way link.exe does
        // when no PDB is requested.
        if (!selected_sections[oi][si] || input.name == ".drectve" ||
            input.name.rfind(".debug", 0) == 0 ||
            (input.characteristics & 0x00000800U) != 0U) continue;
        const auto name = canonical_section(input.name);
        std::size_t out_index;
        if (auto it = output_by_name.find(name); it != output_by_name.end()) out_index = it->second;
        else {
          out_index = outputs.size();
          output_by_name.emplace(name, out_index);
          outputs.push_back({name, image_characteristics(input.characteristics), {}, 0, 0});
          contributions.emplace_back();
        }
        outputs[out_index].characteristics |= image_characteristics(input.characteristics);
        contributions[out_index].push_back({oi, si});
      }
    }

    // MSVC/link.exe sort contributions with the same output section by their
    // complete subsection name. Preserve object/section input order for ties.
    for (std::size_t out_index = 0; out_index < outputs.size(); ++out_index) {
      auto& list = contributions[out_index];
      std::stable_sort(list.begin(), list.end(), [&](const Contribution& a, const Contribution& b) {
        return objects[a.object].sections[a.section].name < objects[b.object].sections[b.section].name;
      });
      auto& out = outputs[out_index];
      // The placement cursor is the section's logical extent, not the number of
      // file bytes it has accumulated. A .bss contribution occupies image space
      // while adding no data, so cursoring on data.size() would hand every
      // uninitialized contribution the same offset and make distinct globals
      // alias each other.
      std::uint32_t cursor = 0;
      for (const auto& contribution : list) {
        const auto& input = objects[contribution.object].sections[contribution.section];
        const auto alignment = section_alignment(input.characteristics);
        const auto offset = align_up(cursor, alignment);
        if (!input.data.empty()) {
          out.data.resize(offset, std::byte{0});
          out.data.insert(out.data.end(), input.data.begin(), input.data.end());
        }
        cursor = offset + std::max(input.virtual_size, static_cast<std::uint32_t>(input.data.size()));
        out.virtual_size = std::max(out.virtual_size, cursor);
        placements[contribution.object][contribution.section] = {out_index, offset};
      }
    }

    // External symbols with section 0 and a non-zero value are COFF common
    // symbols. Allocate one deterministic .bss slot using the largest requested
    // size, then let any real section definition override the tentative common.
    std::map<std::string, std::uint32_t> common_sizes;
    for (const auto& object : objects) {
      for (const auto& symbol : object.symbols) {
        if (symbol.storage_class == 2U && symbol.section_number == 0 && symbol.value != 0U && !symbol.name.empty())
          common_sizes[symbol.name] = std::max(common_sizes[symbol.name], symbol.value);
      }
    }
    struct CommonPlacement { std::string name; std::size_t output{}; std::uint32_t offset{}; };
    std::vector<CommonPlacement> common_placements;
    if (!common_sizes.empty()) {
      std::size_t bss_output;
      if (auto it = output_by_name.find(".bss"); it != output_by_name.end()) bss_output = it->second;
      else {
        bss_output = outputs.size();
        output_by_name.emplace(".bss", bss_output);
        outputs.push_back({".bss", 0xC0000080U, {}, 0, 0});
      }
      auto& bss = outputs[bss_output];
      for (const auto& [name, size] : common_sizes) {
        const std::uint32_t alignment = std::min<std::uint32_t>(16U, std::max<std::uint32_t>(1U, size));
        const std::uint32_t offset = align_up(bss.virtual_size, alignment);
        common_placements.push_back({name, bss_output, offset});
        bss.virtual_size = offset + size;
      }
    }

    // Import code symbols need a linker-synthesized x64 thunk. The thunk is a
    // six-byte RIP-relative indirect jump through its IAT slot: FF 25 disp32.
    // __imp_<symbol> relocations resolve directly to the IAT address instead.
    std::size_t import_text_output = std::numeric_limits<std::size_t>::max();
    std::size_t import_data_output = std::numeric_limits<std::size_t>::max();
    std::uint32_t import_data_base = 0;
    const bool build_import_directory = !import_layout.imports.empty();
    if (build_import_directory) {
      auto it = output_by_name.find(".text");
      if (it == output_by_name.end()) {
        import_text_output = outputs.size();
        output_by_name.emplace(".text", import_text_output);
        outputs.push_back({".text", 0x60000020U, {}, 0, 0});
      } else import_text_output = it->second;
      auto& text = outputs[import_text_output];
      for (auto& import : import_layout.imports) {
        if (import.spec.type != 0U) continue;
        const auto offset = align_up(static_cast<std::uint32_t>(text.data.size()), 2U);
        text.data.resize(offset + 6U, std::byte{0});
        text.data[offset] = std::byte{0xFF};
        text.data[offset + 1U] = std::byte{0x25};
        import.thunk_offset = offset;
        text.virtual_size = std::max(text.virtual_size, offset + 6U);
      }

      // MinGW-style import libraries ship real .idata$N sections, so an output
      // named .idata may already exist. Appending to it -- rather than pushing a
      // second section with the same name -- keeps one .idata in the image and
      // keeps the descriptor writer from finding the contributed section by
      // name and overrunning it.
      if (auto existing = output_by_name.find(".idata"); existing != output_by_name.end()) {
        import_data_output = existing->second;
        auto& idata = outputs[import_data_output];
        import_data_base = align_up(std::max(idata.virtual_size,
                                             static_cast<std::uint32_t>(idata.data.size())), 8U);
        idata.data.resize(import_data_base + import_layout.total_size, std::byte{0});
        idata.virtual_size = import_data_base + import_layout.total_size;
        idata.characteristics |= 0xC0000040U;
      } else {
        import_data_output = outputs.size();
        output_by_name.emplace(".idata", import_data_output);
        outputs.push_back({".idata", 0xC0000040U,
                           std::vector<std::byte>(import_layout.total_size, std::byte{0}),
                           import_layout.total_size, 0});
      }
    }

    // Reserve a .reloc contribution when absolute-address relocations exist.
    // The final relocation blocks are written after section RVAs are known. A
    // worst-case 12 bytes per relocation (one page block per entry) keeps later
    // section RVAs stable even if the final .reloc payload is smaller.
    std::size_t absolute_relocation_count = 0;
    for (std::size_t oi = 0; oi < objects.size(); ++oi)
      for (std::size_t si = 0; si < objects[oi].sections.size(); ++si)
        if (placements[oi][si].output != std::numeric_limits<std::size_t>::max())
          for (const auto& relocation : objects[oi].sections[si].relocations)
            if (relocation.type == coff::reloc_amd64_addr64 || relocation.type == coff::reloc_amd64_addr32)
              ++absolute_relocation_count;
    if (absolute_relocation_count != 0U) {
      const std::uint32_t reserve = static_cast<std::uint32_t>(absolute_relocation_count * 12U);
      output_by_name.emplace(".reloc", outputs.size());
      outputs.push_back({".reloc", 0x42000040U, std::vector<std::byte>(reserve, std::byte{0}), reserve, 0});
    }

    // Stable PE section order improves determinism and locality.
    const std::array<std::string_view, 9> priority = {".text", ".rdata", ".data", ".bss", ".pdata", ".tls", ".idata", ".rsrc", ".reloc"};
    // An output section that ends up with neither file bytes nor image bytes is
    // not representable in a PE: every section header would have to claim the
    // same RVA as its successor, and the loader rejects the image. Forge emits
    // placeholder .rdata/.data sections for modules that define no data, so
    // this is the common case rather than an edge case. Drop them here, before
    // RVAs are assigned, because section RVAs must stay contiguous afterwards.
    std::vector<std::size_t> order;
    order.reserve(outputs.size());
    for (std::size_t i = 0; i < outputs.size(); ++i)
      if (!outputs[i].data.empty() || outputs[i].virtual_size != 0U) order.push_back(i);
    auto rank = [&](std::string_view name) {
      auto it = std::find(priority.begin(), priority.end(), name);
      return it == priority.end() ? priority.size() : static_cast<std::size_t>(it - priority.begin());
    };
    std::stable_sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) {
      const auto ra = rank(outputs[a].name), rb = rank(outputs[b].name);
      if (ra != rb) return ra < rb;
      return outputs[a].name < outputs[b].name;
    });
    std::vector<std::size_t> remap(outputs.size(), std::numeric_limits<std::size_t>::max());
    std::vector<OutputSection> sorted;
    sorted.reserve(outputs.size());
    for (std::size_t ni = 0; ni < order.size(); ++ni) {
      remap[order[ni]] = ni;
      sorted.push_back(std::move(outputs[order[ni]]));
    }
    outputs = std::move(sorted);
    if (outputs.empty()) throw std::runtime_error("link produced no image sections");
    for (auto& by_object : placements)
      for (auto& placement : by_object)
        if (placement.output != std::numeric_limits<std::size_t>::max()) placement.output = remap[placement.output];
    if (import_text_output != std::numeric_limits<std::size_t>::max()) import_text_output = remap[import_text_output];
    if (import_data_output != std::numeric_limits<std::size_t>::max()) import_data_output = remap[import_data_output];
    for (auto& common : common_placements) common.output = remap[common.output];

    std::uint32_t rva = options.section_alignment;
    for (auto& out : outputs) {
      out.rva = rva;
      rva = align_up(rva + std::max(out.virtual_size, static_cast<std::uint32_t>(out.data.size())),
                     options.section_alignment);
    }

    std::unordered_map<std::string, Address> globals;
    std::unordered_map<std::string, bool> global_comdat;
    std::vector<std::vector<Address>> symbol_addresses(objects.size());
    // A symbol whose defining section lost COMDAT selection has no address of
    // its own. Without this flag such a symbol keeps a default-constructed
    // Address, which points at offset 0 of the first output section -- calls to
    // a deduplicated inline function then land on whatever code happens to
    // start .text instead of on the prevailing definition.
    std::vector<std::vector<bool>> symbol_placed(objects.size());
    for (std::size_t oi = 0; oi < objects.size(); ++oi) {
      symbol_addresses[oi].resize(objects[oi].symbols.size());
      symbol_placed[oi].assign(objects[oi].symbols.size(), false);
      for (std::size_t sy = 0; sy < objects[oi].symbols.size(); ++sy) {
        const auto& symbol = objects[oi].symbols[sy];
        if (symbol.name.empty() || symbol.section_number <= 0) continue;
        const auto section_index = static_cast<std::size_t>(symbol.section_number - 1);
        if (section_index >= placements[oi].size())
          throw std::runtime_error("symbol references invalid COFF section: " + symbol.name);
        const auto placement = placements[oi][section_index];
        if (placement.output == std::numeric_limits<std::size_t>::max()) continue;
        symbol_placed[oi][sy] = true;
        Address address{placement.output, placement.offset + symbol.value};
        symbol_addresses[oi][sy] = address;
        if (symbol.storage_class == 2U) {
          const bool current_comdat = objects[oi].sections[section_index].is_comdat();
          auto [it, inserted] = globals.emplace(symbol.name, address);
          if (inserted) global_comdat.emplace(symbol.name, current_comdat);
          else {
            const bool prior_comdat = global_comdat[symbol.name];
            if (!prior_comdat && !current_comdat)
              throw std::runtime_error("duplicate external symbol: " + symbol.name);
            if (prior_comdat && !current_comdat) {
              it->second = address;
              global_comdat[symbol.name] = false;
            }
          }
        }
      }
    }

    for (const auto& common : common_placements)
      globals.emplace(common.name, Address{common.output, common.offset});

    std::uint32_t import_directory_rva = 0;
    std::uint32_t import_directory_size = 0;
    std::uint32_t iat_directory_rva = 0;
    std::uint32_t iat_directory_size = 0;
    if (build_import_directory) {
      if (import_data_output == std::numeric_limits<std::size_t>::max())
        throw std::runtime_error("internal error: missing synthesized import data");
      const std::size_t idata_output = import_data_output;
      auto& idata = outputs[idata_output].data;
      // Layout offsets are relative to the synthesized block, which may sit
      // after import data contributed by input objects.
      const std::uint32_t base = import_data_base;
      const std::uint32_t idata_rva = outputs[idata_output].rva + base;

      for (std::size_t gi = 0; gi < import_layout.groups.size(); ++gi) {
        const auto& group = import_layout.groups[gi];
        const std::size_t desc = base + import_layout.descriptor_offset + gi * 20U;
        store_u32(idata, desc + 0U, idata_rva + group.ilt_offset);  // OriginalFirstThunk
        store_u32(idata, desc + 12U, idata_rva + group.dll_name_offset);
        store_u32(idata, desc + 16U, idata_rva + group.iat_offset); // FirstThunk
        store_ascii_z(idata, base + group.dll_name_offset, group.dll);
        for (std::size_t n = 0; n < group.members.size(); ++n) {
          auto& import = import_layout.imports[group.members[n]];
          std::uint64_t lookup{};
          if (import.spec.by_ordinal) lookup = 0x8000000000000000ULL | import.spec.hint_or_ordinal;
          else {
            store_u16(idata, base + import.hint_name_offset, import.spec.hint_or_ordinal);
            store_ascii_z(idata, base + import.hint_name_offset + 2U, import.spec.import_name);
            lookup = static_cast<std::uint64_t>(idata_rva + import.hint_name_offset);
          }
          store_u64(idata, base + group.ilt_offset + n * 8U, lookup);
          store_u64(idata, base + group.iat_offset + n * 8U, lookup);

          const Address iat_address{idata_output, base + import.iat_offset};
          globals.emplace(import_iat_symbol(import.spec), iat_address);
          const std::string public_symbol = import_public_symbol(import.spec);
          if (import.spec.type == 0U) {
            if (import.thunk_offset == std::numeric_limits<std::uint32_t>::max())
              throw std::runtime_error("internal error: code import lacks thunk");
            globals.emplace(public_symbol, Address{import_text_output, import.thunk_offset});
            auto& text = outputs[import_text_output].data;
            const std::int64_t thunk_rva = static_cast<std::int64_t>(outputs[import_text_output].rva) + import.thunk_offset;
            const std::int64_t target_rva = static_cast<std::int64_t>(idata_rva) + import.iat_offset;
            const std::int64_t displacement = target_rva - (thunk_rva + 6);
            if (displacement < std::numeric_limits<std::int32_t>::min() ||
                displacement > std::numeric_limits<std::int32_t>::max())
              throw std::runtime_error("import thunk displacement overflow");
            store_u32(text, import.thunk_offset + 2U,
                      static_cast<std::uint32_t>(static_cast<std::int32_t>(displacement)));
          } else {
            // Data/const imports are represented by the IAT slot itself. COFF
            // normally references __imp_<name>; defining the public symbol too
            // keeps compatibility with import objects that request direct data.
            globals.emplace(public_symbol, iat_address);
          }
        }
      }
      import_directory_rva = idata_rva + import_layout.descriptor_offset;
      import_directory_size = import_layout.descriptor_size;
      iat_directory_rva = idata_rva + import_layout.iat_begin;
      iat_directory_size = import_layout.iat_end - import_layout.iat_begin;
    }

    auto resolve_global_alias = [&](std::string name) -> std::optional<Address> {
      std::unordered_set<std::string> visited;
      for (;;) {
        if (auto it = globals.find(name); it != globals.end()) return it->second;
        if (!visited.insert(name).second) return std::nullopt;
        auto alias = loaded.aliases.find(name);
        if (alias == loaded.aliases.end()) return std::nullopt;
        name = alias->second;
      }
    };

    std::function<Target(std::size_t, std::uint32_t)> resolve;
    resolve = [&](std::size_t oi, std::uint32_t index) -> Target {
      if (index >= objects[oi].symbols.size())
        throw std::runtime_error("relocation symbol index exceeds COFF symbol table");
      const auto& symbol = objects[oi].symbols[index];
      if (symbol.section_number > 0) {
        if (symbol_placed[oi][index]) return {TargetKind::section, symbol_addresses[oi][index], 0U};
        // The definition was discarded, so the reference belongs to whichever
        // copy of this COMDAT prevailed.
        if (auto address = resolve_global_alias(symbol.name)) return {TargetKind::section, *address, 0U};
        throw std::runtime_error("reference to a discarded section definition: " + symbol.name);
      }
      if (symbol.section_number == -1) return {TargetKind::absolute, {}, symbol.value};
      if (symbol.section_number == 0) {
        if (symbol.name == "__ImageBase") return {TargetKind::image_base, {}, options.image_base};
        if (auto address = resolve_global_alias(symbol.name)) return {TargetKind::section, *address, 0U};
        if (symbol.storage_class == 105U && symbol.weak_default_index < objects[oi].symbols.size() &&
            symbol.weak_default_index != index)
          return resolve(oi, symbol.weak_default_index);
        throw std::runtime_error("unresolved external symbol: " + symbol.name);
      }
      throw std::runtime_error("unsupported debug COFF symbol in relocation: " + symbol.name);
    };

    struct BaseRelocation { std::uint32_t rva{}; std::uint16_t type{}; };
    std::vector<BaseRelocation> base_relocations;

    for (std::size_t oi = 0; oi < objects.size(); ++oi) {
      for (std::size_t si = 0; si < objects[oi].sections.size(); ++si) {
        const auto& input = objects[oi].sections[si];
        const auto place = placements[oi][si];
        if (place.output == std::numeric_limits<std::size_t>::max()) continue;
        auto& data = outputs[place.output].data;
        for (const auto& reloc : input.relocations) {
          const auto target = resolve(oi, reloc.symbol_index);
          const std::size_t patch = static_cast<std::size_t>(place.offset) + reloc.virtual_address;
          const std::uint64_t target_rva = target.kind == TargetKind::section
              ? static_cast<std::uint64_t>(outputs[target.address.output].rva) + target.address.offset
              : (target.kind == TargetKind::image_base ? 0U : target.absolute);
          const std::uint64_t target_va = target.kind == TargetKind::section
              ? options.image_base + target_rva
              : (target.kind == TargetKind::image_base ? options.image_base : target.absolute);
          const std::uint64_t place_rva = static_cast<std::uint64_t>(outputs[place.output].rva) +
                                          place.offset + reloc.virtual_address;
          switch (reloc.type) {
            case coff::reloc_amd64_absolute:
              break;
            case coff::reloc_amd64_rel32:
            case coff::reloc_amd64_rel32_1:
            case coff::reloc_amd64_rel32_2:
            case coff::reloc_amd64_rel32_3:
            case coff::reloc_amd64_rel32_4:
            case coff::reloc_amd64_rel32_5: {
              const auto extra = static_cast<std::int64_t>(reloc.type - coff::reloc_amd64_rel32);
              const auto addend = static_cast<std::int32_t>(load_u32(data, patch));
              const auto value = static_cast<std::int64_t>(target_va) + addend -
                                 static_cast<std::int64_t>(options.image_base + place_rva + 4U + extra);
              if (value < std::numeric_limits<std::int32_t>::min() ||
                  value > std::numeric_limits<std::int32_t>::max())
                throw std::runtime_error("REL32 relocation overflow");
              store_u32(data, patch, static_cast<std::uint32_t>(static_cast<std::int32_t>(value)));
              break;
            }
            case coff::reloc_amd64_addr64: {
              const auto addend = load_u64(data, patch);
              store_u64(data, patch, target_va + addend);
              if (target.kind != TargetKind::absolute)
                base_relocations.push_back({static_cast<std::uint32_t>(place_rva), 10U}); // IMAGE_REL_BASED_DIR64
              break;
            }
            case coff::reloc_amd64_addr32: {
              const auto addend = load_u32(data, patch);
              const auto value = target_va + addend;
              if (value > std::numeric_limits<std::uint32_t>::max())
                throw std::runtime_error("ADDR32 relocation overflow");
              store_u32(data, patch, static_cast<std::uint32_t>(value));
              if (target.kind != TargetKind::absolute)
                base_relocations.push_back({static_cast<std::uint32_t>(place_rva), 3U}); // IMAGE_REL_BASED_HIGHLOW
              break;
            }
            case coff::reloc_amd64_addr32nb:
              if (target.kind == TargetKind::absolute)
                throw std::runtime_error("ADDR32NB relocation cannot target an absolute symbol");
              store_u32(data, patch, static_cast<std::uint32_t>(target_rva + load_u32(data, patch))); break;
            case coff::reloc_amd64_section:
              store_u16(data, patch, static_cast<std::uint16_t>(
                  target.kind == TargetKind::section ? target.address.output + 1U : outputs.size() + 1U)); break;
            case coff::reloc_amd64_secrel:
              // An absolute symbol belongs to no section, so its offset within
              // one is its own value. link.exe applies the same rule.
              store_u32(data, patch, static_cast<std::uint32_t>(
                  (target.kind == TargetKind::section ? target.address.offset
                                                      : static_cast<std::uint32_t>(target.absolute)) +
                  load_u32(data, patch)));
              break;
            default:
              throw std::runtime_error("unsupported AMD64 COFF relocation type: " + std::to_string(reloc.type));
          }
        }
      }
    }

    std::uint32_t basereloc_directory_rva = 0;
    std::uint32_t basereloc_directory_size = 0;
    if (!base_relocations.empty()) {
      std::sort(base_relocations.begin(), base_relocations.end(), [](const BaseRelocation& a, const BaseRelocation& b) {
        if ((a.rva & ~0xfffU) != (b.rva & ~0xfffU)) return (a.rva & ~0xfffU) < (b.rva & ~0xfffU);
        if ((a.rva & 0xfffU) != (b.rva & 0xfffU)) return (a.rva & 0xfffU) < (b.rva & 0xfffU);
        return a.type < b.type;
      });
      auto reloc_it = std::find_if(outputs.begin(), outputs.end(), [](const OutputSection& section) { return section.name == ".reloc"; });
      if (reloc_it == outputs.end()) throw std::runtime_error("internal error: missing reserved .reloc section");
      std::vector<std::byte> payload;
      std::size_t cursor = 0;
      while (cursor < base_relocations.size()) {
        const std::uint32_t page = base_relocations[cursor].rva & ~0xfffU;
        const std::size_t block_start = payload.size();
        append_u32(payload, page);
        append_u32(payload, 0U); // block size backfilled below
        while (cursor < base_relocations.size() && (base_relocations[cursor].rva & ~0xfffU) == page) {
          const std::uint16_t slot = static_cast<std::uint16_t>((base_relocations[cursor].type << 12U) |
                                                                (base_relocations[cursor].rva & 0xfffU));
          append_u16(payload, slot);
          ++cursor;
        }
        while ((payload.size() - block_start) % 4U != 0U) append_u16(payload, 0U);
        store_u32(payload, block_start + 4U, static_cast<std::uint32_t>(payload.size() - block_start));
      }
      if (payload.size() > reloc_it->virtual_size)
        throw std::runtime_error("internal error: base-relocation reservation was too small");
      reloc_it->data = std::move(payload);
      basereloc_directory_rva = reloc_it->rva;
      basereloc_directory_size = static_cast<std::uint32_t>(reloc_it->data.size());
    }

    std::uint32_t exception_directory_rva = 0;
    std::uint32_t exception_directory_size = 0;
    if (auto pdata = std::find_if(outputs.begin(), outputs.end(), [](const OutputSection& section) { return section.name == ".pdata"; });
        pdata != outputs.end() && !pdata->data.empty()) {
      // IMAGE_RUNTIME_FUNCTION_ENTRY on AMD64 is three uint32 RVAs and the PE
      // exception directory must be sorted by BeginAddress. Input/archive and
      // .text$ subsection ordering do not guarantee that .pdata arrives in the
      // same order, so sort only after all relocations have resolved.
      constexpr std::size_t runtime_function_size = 12U;
      if (pdata->data.size() % runtime_function_size != 0U)
        throw std::runtime_error("malformed AMD64 .pdata section size");
      std::vector<std::array<std::byte, runtime_function_size>> records;
      records.reserve(pdata->data.size() / runtime_function_size);
      for (std::size_t offset = 0; offset < pdata->data.size(); offset += runtime_function_size) {
        std::array<std::byte, runtime_function_size> record{};
        std::copy_n(pdata->data.begin() + static_cast<std::ptrdiff_t>(offset), runtime_function_size, record.begin());
        records.push_back(record);
      }
      auto begin_rva = [](const auto& record) {
        return std::uint32_t(std::to_integer<std::uint8_t>(record[0])) |
               (std::uint32_t(std::to_integer<std::uint8_t>(record[1])) << 8U) |
               (std::uint32_t(std::to_integer<std::uint8_t>(record[2])) << 16U) |
               (std::uint32_t(std::to_integer<std::uint8_t>(record[3])) << 24U);
      };
      std::stable_sort(records.begin(), records.end(), [&](const auto& a, const auto& b) {
        return begin_rva(a) < begin_rva(b);
      });
      std::size_t offset = 0;
      for (const auto& record : records) {
        std::copy(record.begin(), record.end(), pdata->data.begin() + static_cast<std::ptrdiff_t>(offset));
        offset += runtime_function_size;
      }
      exception_directory_rva = pdata->rva;
      exception_directory_size = static_cast<std::uint32_t>(pdata->data.size());
    }

    std::uint32_t tls_directory_rva = 0;
    std::uint32_t tls_directory_size = 0;
    for (const std::string_view tls_name : {std::string_view{"_tls_used"}, std::string_view{"__tls_used"}}) {
      if (auto tls = globals.find(std::string(tls_name)); tls != globals.end()) {
        tls_directory_rva = outputs[tls->second.output].rva + tls->second.offset;
        tls_directory_size = 40U; // IMAGE_TLS_DIRECTORY64
        break;
      }
    }

    const std::string entry_name = loaded.entry.empty() ? (options.entry.empty() ? "main" : options.entry) : loaded.entry;
    auto entry = globals.find(entry_name);
    if (entry == globals.end()) {
      if (auto decorated = globals.find("_" + entry_name); decorated != globals.end()) entry = decorated;
      else throw std::runtime_error("entry symbol not found: " + entry_name);
    }
    const std::uint32_t entry_rva = outputs[entry->second.output].rva + entry->second.offset;

    if (options.verbose) {
      result.trace.push_back("entry " + entry_name + " at rva 0x" + hex_rva(entry_rva));
      for (const auto& out : outputs)
        result.trace.push_back("section " + out.name + " rva 0x" + hex_rva(out.rva) + " raw " +
                               std::to_string(out.data.size()) + " virtual " +
                               std::to_string(std::max(out.virtual_size,
                                                       static_cast<std::uint32_t>(out.data.size()))));
    }

    if (!options.map_output.empty()) {
      std::ofstream map(options.map_output, std::ios::trunc);
      if (!map) throw std::runtime_error("cannot create link map: " + options.map_output.string());
      map << "ObLink map for " << options.output.string() << "\n\nSections\n";
      for (const auto& out : outputs)
        map << "  " << hex_rva(out.rva) << ' ' << out.name << " raw=" << out.data.size()
            << " virtual=" << std::max(out.virtual_size, static_cast<std::uint32_t>(out.data.size()))
            << " characteristics=" << hex_rva(out.characteristics) << '\n';
      map << "\nEntry\n  " << hex_rva(entry_rva) << ' ' << entry_name << "\n\nSymbols\n";
      std::vector<std::pair<std::uint32_t, std::string>> symbols;
      symbols.reserve(globals.size());
      for (const auto& [name, address] : globals)
        symbols.emplace_back(outputs[address.output].rva + address.offset, name);
      std::sort(symbols.begin(), symbols.end());
      for (const auto& [rva, name] : symbols) map << "  " << hex_rva(rva) << ' ' << name << '\n';
      if (!map) throw std::runtime_error("failed while writing link map: " + options.map_output.string());
    }

    std::vector<pe::SectionImage> image_sections;
    image_sections.reserve(outputs.size());
    for (auto& out : outputs)
      image_sections.push_back({out.name, out.rva, out.characteristics, std::move(out.data), out.virtual_size});
    pe::ImageOptions pe_options{options.image_base, entry_rva, options.file_alignment,
      options.section_alignment, options.subsystem,
      options.stack_reserve, options.stack_commit, options.heap_reserve, options.heap_commit,
      options.deterministic,
      import_directory_rva, import_directory_size,
      exception_directory_rva, exception_directory_size,
      basereloc_directory_rva, basereloc_directory_size,
      tls_directory_rva, tls_directory_size,
      iat_directory_rva, iat_directory_size};
    const auto image = pe::build_pe32_plus(image_sections, pe_options);
    write_atomic(options.output, image);
    result.output_bytes = image.size();
  } catch (const std::exception& e) {
    add_error(result.diagnostics, e.what());
  }
  return result;
}

} // namespace oblink
