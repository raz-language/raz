// Copyright 2026 Mario Vinciguerra
// SPDX-License-Identifier: Apache-2.0

#include "forge/object/macho.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
#include <set>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace forge::object {
namespace {

constexpr std::uint32_t mh_magic_64 = 0xFEEDFACFU;
constexpr std::uint32_t cpu_type_arm64 = 0x0100000CU;
constexpr std::uint32_t cpu_subtype_arm64_all = 0U;
constexpr std::uint32_t mh_object = 1U;
constexpr std::uint32_t mh_subsections_via_symbols = 0x00002000U;
constexpr std::uint32_t lc_segment_64 = 0x19U;
constexpr std::uint32_t lc_symtab = 0x02U;
constexpr std::uint32_t lc_dysymtab = 0x0BU;
constexpr std::uint32_t lc_build_version = 0x32U;
constexpr std::uint32_t platform_macos = 1U;
constexpr std::uint32_t vm_prot_rwx = 7U;
constexpr std::uint32_t s_regular = 0x00U;
constexpr std::uint32_t s_thread_local_regular = 0x11U;
constexpr std::uint32_t s_thread_local_variables = 0x13U;
constexpr std::uint32_t s_attr_pure_instructions = 0x80000000U;
constexpr std::uint32_t s_attr_some_instructions = 0x00000400U;
constexpr std::uint8_t n_ext = 0x01U;
constexpr std::uint8_t n_sect = 0x0EU;
constexpr std::uint8_t n_undef = 0x00U;
constexpr std::uint8_t arm64_reloc_unsigned = 0U;
constexpr std::uint8_t arm64_reloc_branch26 = 2U;
constexpr std::uint8_t arm64_reloc_page21 = 3U;
constexpr std::uint8_t arm64_reloc_pageoff12 = 4U;
constexpr std::uint8_t arm64_reloc_tlvp_load_page21 = 8U;
constexpr std::uint8_t arm64_reloc_tlvp_load_pageoff12 = 9U;

void add_error(Diagnostics& diagnostics, std::string message) {
    diagnostics.push_back({DiagnosticSeverity::error, std::move(message), {}});
}

std::uint64_t align_up(std::uint64_t value, std::uint64_t alignment) noexcept {
    if (alignment <= 1U) return value;
    return (value + alignment - 1U) & ~(alignment - 1U);
}

class Writer {
public:
    void u8(std::uint8_t value) { bytes_.push_back(static_cast<std::byte>(value)); }
    void u16(std::uint16_t value) {
        u8(static_cast<std::uint8_t>(value));
        u8(static_cast<std::uint8_t>(value >> 8U));
    }
    void u32(std::uint32_t value) {
        for (unsigned shift = 0; shift < 32U; shift += 8U) u8(static_cast<std::uint8_t>(value >> shift));
    }
    void u64(std::uint64_t value) {
        for (unsigned shift = 0; shift < 64U; shift += 8U) u8(static_cast<std::uint8_t>(value >> shift));
    }
    void fixed16(std::string_view value) {
        for (std::size_t index = 0; index < 16U; ++index)
            u8(index < value.size() ? static_cast<std::uint8_t>(value[index]) : 0U);
    }
    void raw(const std::vector<std::byte>& bytes) { bytes_.insert(bytes_.end(), bytes.begin(), bytes.end()); }
    void raw_chars(const std::vector<char>& bytes) {
        for (const auto value : bytes) u8(static_cast<std::uint8_t>(value));
    }
    void zeros(std::size_t count) { bytes_.insert(bytes_.end(), count, std::byte{0}); }
    void pad_to(std::size_t offset) {
        if (bytes_.size() < offset) zeros(offset - bytes_.size());
    }
    [[nodiscard]] std::size_t size() const noexcept { return bytes_.size(); }
    [[nodiscard]] std::vector<std::byte> take() && { return std::move(bytes_); }
private:
    std::vector<std::byte> bytes_;
};

struct StringTable {
    std::vector<char> bytes{0};
    std::unordered_map<std::string, std::uint32_t> offsets;

    std::uint32_t add(std::string_view value) {
        const std::string key(value);
        if (const auto found = offsets.find(key); found != offsets.end()) return found->second;
        if (bytes.size() > std::numeric_limits<std::uint32_t>::max() - key.size() - 1U) return 0U;
        const auto offset = static_cast<std::uint32_t>(bytes.size());
        bytes.insert(bytes.end(), key.begin(), key.end());
        bytes.push_back('\0');
        offsets.emplace(key, offset);
        return offset;
    }
};

struct Symbol {
    std::string name;
    std::uint8_t type{};
    std::uint8_t section{};
    std::uint16_t description{};
    std::uint64_t value{};
};

struct Relocation {
    std::uint32_t address{};
    std::uint32_t symbol{};
    bool pcrel{};
    std::uint8_t length{};
    std::uint8_t type{};
};

struct Section {
    std::string name;
    std::string segment;
    std::vector<std::byte> data;
    std::uint32_t align_log2{};
    std::uint32_t flags{};
    std::vector<Relocation> relocations;
    std::uint64_t address{};
    std::uint32_t file_offset{};
    std::uint32_t relocation_offset{};
    std::uint8_t ordinal{};
};

std::string darwin_symbol(std::string_view name) {
    return "_" + std::string(name);
}

std::uint32_t relocation_word(const Relocation& relocation) noexcept {
    return (relocation.symbol & 0x00FFFFFFU) |
           (relocation.pcrel ? 1U << 24U : 0U) |
           ((static_cast<std::uint32_t>(relocation.length) & 0x3U) << 25U) |
           (1U << 27U) |
           ((static_cast<std::uint32_t>(relocation.type) & 0xFU) << 28U);
}

bool text_relocation_kind(codegen::aarch64::RelocationKind kind, bool& pcrel, std::uint8_t& type) noexcept {
    using Kind = codegen::aarch64::RelocationKind;
    switch (kind) {
    case Kind::call26: pcrel = true; type = arm64_reloc_branch26; return true;
    case Kind::adr_prel_pg_hi21: pcrel = true; type = arm64_reloc_page21; return true;
    case Kind::add_abs_lo12_nc: pcrel = false; type = arm64_reloc_pageoff12; return true;
    case Kind::tlvp_load_page21: pcrel = true; type = arm64_reloc_tlvp_load_page21; return true;
    case Kind::tlvp_load_pageoff12: pcrel = false; type = arm64_reloc_tlvp_load_pageoff12; return true;
    case Kind::tlsie_adr_gottprel_page21:
    case Kind::tlsie_ld64_gottprel_lo12_nc:
        return false;
    }
    return false;
}

} // namespace

MachOObjectResult emit_macho64_aarch64(const machine::Module& module) {
    auto encoded = codegen::aarch64::encode_image(module, codegen::aarch64::Abi::darwin);
    MachOObjectResult result;
    result.diagnostics = std::move(encoded.diagnostics);
    if (!result.diagnostics.empty()) return result;
    return emit_macho64_aarch64(std::move(encoded.image));
}

MachOObjectResult emit_macho64_aarch64(codegen::aarch64::EncodedModuleImage image) {
    MachOObjectResult result;

    const bool has_const = !image.read_only_data.empty() || std::any_of(image.globals.begin(), image.globals.end(), [](const auto& global) {
        return global.section == codegen::aarch64::DataSection::read_only;
    });
    const bool has_data = !image.writable_data.empty() || std::any_of(image.globals.begin(), image.globals.end(), [](const auto& global) {
        return global.section == codegen::aarch64::DataSection::writable;
    });
    const bool has_tls = std::any_of(image.globals.begin(), image.globals.end(), [](const auto& global) {
        return global.section == codegen::aarch64::DataSection::tls;
    });

    std::vector<Section> sections;
    sections.push_back({"__text", "__TEXT", image.code, 2U,
                        s_regular | s_attr_pure_instructions | s_attr_some_instructions, {}, 0U, 0U, 0U, 0U});
    const auto text_index = std::size_t{0};
    std::size_t const_index = std::numeric_limits<std::size_t>::max();
    std::size_t data_index = std::numeric_limits<std::size_t>::max();
    std::size_t tdata_index = std::numeric_limits<std::size_t>::max();
    std::size_t tvars_index = std::numeric_limits<std::size_t>::max();
    if (has_const) {
        const_index = sections.size();
        sections.push_back({"__const", "__TEXT", image.read_only_data, 3U, s_regular, {}, 0U, 0U, 0U, 0U});
    }
    if (has_data) {
        data_index = sections.size();
        sections.push_back({"__data", "__DATA", image.writable_data, 3U, s_regular, {}, 0U, 0U, 0U, 0U});
    }
    if (has_tls) {
        tdata_index = sections.size();
        sections.push_back({"__thread_data", "__DATA", image.thread_local_data, 3U, s_thread_local_regular, {}, 0U, 0U, 0U, 0U});
        tvars_index = sections.size();
        std::vector<std::byte> descriptors;
        const auto count = static_cast<std::size_t>(std::count_if(image.globals.begin(), image.globals.end(), [](const auto& global) {
            return global.section == codegen::aarch64::DataSection::tls;
        }));
        descriptors.resize(count * 24U, std::byte{0});
        sections.push_back({"__thread_vars", "__DATA", std::move(descriptors), 3U, s_thread_local_variables, {}, 0U, 0U, 0U, 0U});
    }
    if (sections.size() > 255U) {
        add_error(result.diagnostics, "Mach-O arm64 section count exceeds nlist_64 range");
        return result;
    }
    for (std::size_t index = 0; index < sections.size(); ++index)
        sections[index].ordinal = static_cast<std::uint8_t>(index + 1U);

    // VM addresses in MH_OBJECT are section-relative offsets in one synthetic
    // object address space. Keep them aligned exactly like the file payload.
    std::uint64_t vm_cursor = 0U;
    for (auto& section : sections) {
        vm_cursor = align_up(vm_cursor, std::uint64_t{1} << section.align_log2);
        section.address = vm_cursor;
        vm_cursor += section.data.size();
    }

    std::vector<Symbol> locals;
    std::vector<Symbol> extdefs;
    std::vector<std::pair<std::string, std::size_t>> tls_descriptors;
    std::unordered_map<std::string, std::uint32_t> defined_raw_to_placeholder;

    std::size_t tls_ordinal = 0U;
    for (const auto& global : image.globals) {
        if (global.section != codegen::aarch64::DataSection::tls) continue;
        const auto init_name = darwin_symbol(global.name) + "$tlv$init";
        locals.push_back({init_name, n_sect, sections[tdata_index].ordinal, 0U,
                          sections[tdata_index].address + global.data_offset});
        tls_descriptors.emplace_back(global.name, tls_ordinal++ * 24U);
    }

    for (const auto& [name, offset] : image.entries) {
        defined_raw_to_placeholder.emplace(name, 0U);
        extdefs.push_back({darwin_symbol(name), static_cast<std::uint8_t>(n_sect | n_ext),
                           sections[text_index].ordinal, 0U, sections[text_index].address + offset});
    }
    for (const auto& global : image.globals) {
        std::size_t section_index = data_index;
        std::uint64_t offset = global.data_offset;
        if (global.section == codegen::aarch64::DataSection::read_only) section_index = const_index;
        else if (global.section == codegen::aarch64::DataSection::tls) {
            section_index = tvars_index;
            const auto found = std::find_if(tls_descriptors.begin(), tls_descriptors.end(), [&](const auto& pair) {
                return pair.first == global.name;
            });
            offset = found == tls_descriptors.end() ? 0U : found->second;
        }
        if (section_index == std::numeric_limits<std::size_t>::max()) {
            add_error(result.diagnostics, "Mach-O arm64 global has no backing section @" + global.name);
            return result;
        }
        defined_raw_to_placeholder.emplace(global.name, 0U);
        if (global.is_internal)
            locals.push_back({darwin_symbol(global.name), n_sect, sections[section_index].ordinal, 0U,
                              sections[section_index].address + offset});
        else
            extdefs.push_back({darwin_symbol(global.name), static_cast<std::uint8_t>(n_sect | n_ext),
                               sections[section_index].ordinal, 0U, sections[section_index].address + offset});
    }

    std::set<std::string> undefined_raw;
    for (const auto& name : image.external_globals) undefined_raw.insert(name);
    for (const auto& name : image.external_tls) undefined_raw.insert(name);
    for (const auto& relocation : image.relocations)
        if (!defined_raw_to_placeholder.contains(relocation.symbol)) undefined_raw.insert(relocation.symbol);

    std::vector<Symbol> undefs;
    if (has_tls) undefs.push_back({"__tlv_bootstrap", static_cast<std::uint8_t>(n_undef | n_ext), 0U, 0U, 0U});
    for (const auto& raw : undefined_raw)
        undefs.push_back({darwin_symbol(raw), static_cast<std::uint8_t>(n_undef | n_ext), 0U, 0U, 0U});

    std::vector<Symbol> symbols;
    symbols.reserve(locals.size() + extdefs.size() + undefs.size());
    symbols.insert(symbols.end(), locals.begin(), locals.end());
    const auto first_extdef = static_cast<std::uint32_t>(symbols.size());
    symbols.insert(symbols.end(), extdefs.begin(), extdefs.end());
    const auto first_undef = static_cast<std::uint32_t>(symbols.size());
    symbols.insert(symbols.end(), undefs.begin(), undefs.end());

    std::unordered_map<std::string, std::uint32_t> symbol_indexes;
    for (std::uint32_t index = 0; index < symbols.size(); ++index)
        symbol_indexes.emplace(symbols[index].name, index);
    const auto symbol_for_raw = [&](const std::string& raw) -> std::uint32_t {
        const auto found = symbol_indexes.find(darwin_symbol(raw));
        return found == symbol_indexes.end() ? std::numeric_limits<std::uint32_t>::max() : found->second;
    };

    for (const auto& relocation : image.relocations) {
        if (relocation.addend != 0) {
            add_error(result.diagnostics, "Mach-O arm64 non-zero relocation addends are not implemented for @" + relocation.symbol);
            return result;
        }
        if ((relocation.offset & 3U) != 0U || relocation.offset + 4U > image.code.size()) {
            add_error(result.diagnostics, "malformed Mach-O arm64 text relocation for @" + relocation.symbol);
            return result;
        }
        bool pcrel = false;
        std::uint8_t type = 0U;
        if (!text_relocation_kind(relocation.kind, pcrel, type)) {
            add_error(result.diagnostics, "Linux-only AArch64 relocation reached Mach-O emitter for @" + relocation.symbol);
            return result;
        }
        const auto symbol = symbol_for_raw(relocation.symbol);
        if (symbol == std::numeric_limits<std::uint32_t>::max()) {
            add_error(result.diagnostics, "missing Mach-O arm64 symbol for relocation @" + relocation.symbol);
            return result;
        }
        sections[text_index].relocations.push_back({static_cast<std::uint32_t>(relocation.offset), symbol, pcrel, 2U, type});
    }

    if (has_tls) {
        const auto bootstrap = symbol_indexes.find("__tlv_bootstrap");
        if (bootstrap == symbol_indexes.end()) {
            add_error(result.diagnostics, "Mach-O TLS bootstrap symbol construction failed");
            return result;
        }
        for (const auto& [raw, offset] : tls_descriptors) {
            const auto init = symbol_indexes.find(darwin_symbol(raw) + "$tlv$init");
            if (init == symbol_indexes.end()) {
                add_error(result.diagnostics, "Mach-O TLS initializer symbol construction failed for @" + raw);
                return result;
            }
            sections[tvars_index].relocations.push_back({static_cast<std::uint32_t>(offset), bootstrap->second,
                                                         false, 3U, arm64_reloc_unsigned});
            sections[tvars_index].relocations.push_back({static_cast<std::uint32_t>(offset + 16U), init->second,
                                                         false, 3U, arm64_reloc_unsigned});
        }
    }

    for (auto& section : sections)
        std::stable_sort(section.relocations.begin(), section.relocations.end(), [](const auto& left, const auto& right) {
            if (left.address != right.address) return left.address > right.address;
            return left.type > right.type;
        });

    StringTable strings;
    std::vector<std::uint32_t> string_offsets;
    string_offsets.reserve(symbols.size());
    for (const auto& symbol : symbols) string_offsets.push_back(strings.add(symbol.name));

    constexpr std::uint32_t header_size = 32U;
    const auto segment_command_size = static_cast<std::uint32_t>(72U + sections.size() * 80U);
    constexpr std::uint32_t build_command_size = 24U;
    constexpr std::uint32_t symtab_command_size = 24U;
    constexpr std::uint32_t dysymtab_command_size = 80U;
    const auto sizeof_commands = segment_command_size + build_command_size + symtab_command_size + dysymtab_command_size;
    std::uint64_t cursor = header_size + sizeof_commands;
    const auto segment_file_offset = cursor;
    for (auto& section : sections) {
        cursor = align_up(cursor, std::uint64_t{1} << section.align_log2);
        if (cursor > std::numeric_limits<std::uint32_t>::max()) {
            add_error(result.diagnostics, "Mach-O arm64 section offset exceeds 32-bit field");
            return result;
        }
        section.file_offset = static_cast<std::uint32_t>(cursor);
        cursor += section.data.size();
    }
    const auto section_data_end = cursor;
    for (auto& section : sections) {
        if (section.relocations.empty()) continue;
        cursor = align_up(cursor, 4U);
        if (cursor > std::numeric_limits<std::uint32_t>::max()) {
            add_error(result.diagnostics, "Mach-O arm64 relocation offset exceeds 32-bit field");
            return result;
        }
        section.relocation_offset = static_cast<std::uint32_t>(cursor);
        cursor += section.relocations.size() * 8U;
    }
    cursor = align_up(cursor, 8U);
    if (cursor > std::numeric_limits<std::uint32_t>::max()) {
        add_error(result.diagnostics, "Mach-O arm64 symbol-table offset exceeds 32-bit field");
        return result;
    }
    const auto symbol_offset = static_cast<std::uint32_t>(cursor);
    cursor += symbols.size() * 16U;
    if (cursor > std::numeric_limits<std::uint32_t>::max()) {
        add_error(result.diagnostics, "Mach-O arm64 string-table offset exceeds 32-bit field");
        return result;
    }
    const auto string_offset = static_cast<std::uint32_t>(cursor);
    const auto string_size = static_cast<std::uint32_t>(strings.bytes.size());
    cursor += string_size;

    Writer out;
    out.u32(mh_magic_64);
    out.u32(cpu_type_arm64);
    out.u32(cpu_subtype_arm64_all);
    out.u32(mh_object);
    out.u32(4U);
    out.u32(sizeof_commands);
    out.u32(mh_subsections_via_symbols);
    out.u32(0U);

    out.u32(lc_segment_64);
    out.u32(segment_command_size);
    out.fixed16("");
    out.u64(0U);
    out.u64(vm_cursor);
    out.u64(segment_file_offset);
    out.u64(section_data_end - segment_file_offset);
    out.u32(vm_prot_rwx);
    out.u32(vm_prot_rwx);
    out.u32(static_cast<std::uint32_t>(sections.size()));
    out.u32(0U);
    for (const auto& section : sections) {
        out.fixed16(section.name);
        out.fixed16(section.segment);
        out.u64(section.address);
        out.u64(section.data.size());
        out.u32(section.file_offset);
        out.u32(section.align_log2);
        out.u32(section.relocation_offset);
        out.u32(static_cast<std::uint32_t>(section.relocations.size()));
        out.u32(section.flags);
        out.u32(0U);
        out.u32(0U);
        out.u32(0U);
    }

    out.u32(lc_build_version);
    out.u32(build_command_size);
    out.u32(platform_macos);
    out.u32(12U << 16U); // macOS 12.0 minimum; arm64 is universally available.
    out.u32(0U);
    out.u32(0U);

    out.u32(lc_symtab);
    out.u32(symtab_command_size);
    out.u32(symbol_offset);
    out.u32(static_cast<std::uint32_t>(symbols.size()));
    out.u32(string_offset);
    out.u32(string_size);

    out.u32(lc_dysymtab);
    out.u32(dysymtab_command_size);
    out.u32(0U);
    out.u32(static_cast<std::uint32_t>(locals.size()));
    out.u32(first_extdef);
    out.u32(static_cast<std::uint32_t>(extdefs.size()));
    out.u32(first_undef);
    out.u32(static_cast<std::uint32_t>(undefs.size()));
    for (unsigned index = 0; index < 12U; ++index) out.u32(0U);

    if (out.size() != header_size + sizeof_commands) {
        add_error(result.diagnostics, "internal Mach-O load-command size mismatch");
        return result;
    }
    for (const auto& section : sections) {
        out.pad_to(section.file_offset);
        out.raw(section.data);
    }
    for (const auto& section : sections) {
        if (section.relocations.empty()) continue;
        out.pad_to(section.relocation_offset);
        for (const auto& relocation : section.relocations) {
            out.u32(relocation.address);
            out.u32(relocation_word(relocation));
        }
    }
    out.pad_to(symbol_offset);
    for (std::size_t index = 0; index < symbols.size(); ++index) {
        const auto& symbol = symbols[index];
        out.u32(string_offsets[index]);
        out.u8(symbol.type);
        out.u8(symbol.section);
        out.u16(symbol.description);
        out.u64(symbol.value);
    }
    out.pad_to(string_offset);
    out.raw_chars(strings.bytes);

    if (out.size() != cursor) {
        add_error(result.diagnostics, "internal Mach-O arm64 file-size mismatch");
        return result;
    }
    result.bytes = std::move(out).take();
    return result;
}

} // namespace forge::object
