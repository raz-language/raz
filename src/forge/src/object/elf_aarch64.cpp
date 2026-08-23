// Copyright 2026 Mario Vinciguerra
// SPDX-License-Identifier: Apache-2.0

#include "forge/object/elf.hpp"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <vector>

namespace forge::object {
namespace {

constexpr std::uint32_t sht_progbits = 1;
constexpr std::uint32_t sht_symtab = 2;
constexpr std::uint32_t sht_strtab = 3;
constexpr std::uint32_t sht_rela = 4;
constexpr std::uint64_t shf_write = 0x1;
constexpr std::uint64_t shf_alloc = 0x2;
constexpr std::uint64_t shf_execinstr = 0x4;
constexpr std::uint64_t shf_tls = 0x400;
constexpr std::uint16_t shn_undef = 0;
constexpr std::uint8_t stb_local = 0;
constexpr std::uint8_t stb_global = 1;
constexpr std::uint8_t stt_notype = 0;
constexpr std::uint8_t stt_object = 1;
constexpr std::uint8_t stt_func = 2;
constexpr std::uint8_t stt_section = 3;
constexpr std::uint8_t stt_tls = 6;
constexpr std::uint16_t em_aarch64 = 183;
constexpr std::uint32_t r_aarch64_adr_prel_pg_hi21 = 275;
constexpr std::uint32_t r_aarch64_add_abs_lo12_nc = 277;
constexpr std::uint32_t r_aarch64_call26 = 283;
constexpr std::uint32_t r_aarch64_tlsie_adr_gottprel_page21 = 541;
constexpr std::uint32_t r_aarch64_tlsie_ld64_gottprel_lo12_nc = 542;

void add_aarch64_error(Diagnostics& diagnostics, std::string message) {
    diagnostics.push_back({DiagnosticSeverity::error, std::move(message), {}});
}

class Aarch64ByteWriter {
public:
    template <typename T>
    void integer(T value) {
        using U = std::make_unsigned_t<T>;
        const auto bits = static_cast<U>(value);
        for (std::size_t index = 0; index < sizeof(T); ++index)
            bytes_.push_back(static_cast<std::byte>(static_cast<std::uint8_t>(bits >> (index * 8U))));
    }
    void raw(std::span<const std::byte> bytes) { bytes_.insert(bytes_.end(), bytes.begin(), bytes.end()); }
    void zeros(std::size_t count) { bytes_.insert(bytes_.end(), count, std::byte{0}); }
    void align(std::size_t alignment) {
        while (alignment != 0U && (bytes_.size() % alignment) != 0U) bytes_.push_back(std::byte{0});
    }
    [[nodiscard]] std::size_t size() const noexcept { return bytes_.size(); }
    [[nodiscard]] std::vector<std::byte> take() && { return std::move(bytes_); }
private:
    std::vector<std::byte> bytes_;
};

struct Aarch64StringTable {
    std::vector<char> bytes{0};
    std::unordered_map<std::string, std::uint32_t> offsets;
    std::uint32_t add(std::string_view value) {
        const std::string key(value);
        if (const auto found = offsets.find(key); found != offsets.end()) return found->second;
        if (bytes.size() > std::numeric_limits<std::uint32_t>::max()) return 0;
        const auto offset = static_cast<std::uint32_t>(bytes.size());
        bytes.insert(bytes.end(), value.begin(), value.end());
        bytes.push_back('\0');
        offsets.emplace(key, offset);
        return offset;
    }
};

struct Aarch64Symbol {
    std::uint32_t name{};
    std::uint8_t info{};
    std::uint8_t other{};
    std::uint16_t section{};
    std::uint64_t value{};
    std::uint64_t size{};
};

struct Aarch64Relocation {
    std::uint64_t offset{};
    std::uint32_t symbol{};
    std::uint32_t type{};
    std::int64_t addend{};
};

struct Aarch64Section {
    std::string name;
    std::uint32_t type{};
    std::uint64_t flags{};
    std::uint64_t alignment{1};
    std::uint64_t entry_size{};
    std::uint32_t link{};
    std::uint32_t info{};
    std::vector<std::byte> data;
    std::uint64_t file_offset{};
    std::uint32_t name_offset{};
};

std::vector<std::byte> aarch64_symbol_bytes(const std::vector<Aarch64Symbol>& symbols) {
    Aarch64ByteWriter writer;
    for (const auto& symbol : symbols) {
        writer.integer(symbol.name); writer.integer(symbol.info); writer.integer(symbol.other);
        writer.integer(symbol.section); writer.integer(symbol.value); writer.integer(symbol.size);
    }
    return std::move(writer).take();
}

std::vector<std::byte> aarch64_relocation_bytes(const std::vector<Aarch64Relocation>& relocations) {
    Aarch64ByteWriter writer;
    for (const auto& relocation : relocations) {
        writer.integer(relocation.offset);
        writer.integer((static_cast<std::uint64_t>(relocation.symbol) << 32U) | relocation.type);
        writer.integer(relocation.addend);
    }
    return std::move(writer).take();
}

std::vector<std::byte> aarch64_chars_to_bytes(const std::vector<char>& chars) {
    std::vector<std::byte> bytes;
    bytes.reserve(chars.size());
    for (const auto value : chars) bytes.push_back(static_cast<std::byte>(static_cast<unsigned char>(value)));
    return bytes;
}

std::uint32_t elf_relocation_type(codegen::aarch64::RelocationKind kind) noexcept {
    switch (kind) {
    case codegen::aarch64::RelocationKind::call26: return r_aarch64_call26;
    case codegen::aarch64::RelocationKind::adr_prel_pg_hi21: return r_aarch64_adr_prel_pg_hi21;
    case codegen::aarch64::RelocationKind::add_abs_lo12_nc: return r_aarch64_add_abs_lo12_nc;
    case codegen::aarch64::RelocationKind::tlsie_adr_gottprel_page21: return r_aarch64_tlsie_adr_gottprel_page21;
    case codegen::aarch64::RelocationKind::tlsie_ld64_gottprel_lo12_nc: return r_aarch64_tlsie_ld64_gottprel_lo12_nc;
    case codegen::aarch64::RelocationKind::tlvp_load_page21:
    case codegen::aarch64::RelocationKind::tlvp_load_pageoff12:
        return 0;
    }
    return 0;
}

ElfObjectResult emit_elf64_aarch64_image(codegen::aarch64::EncodedModuleImage image) {
    ElfObjectResult result;
    const bool has_tls = !image.thread_local_data.empty();
    constexpr std::uint16_t text_index = 1;
    constexpr std::uint16_t rodata_index = 2;
    constexpr std::uint16_t data_index = 3;
    const std::uint16_t tdata_index = has_tls ? 4 : 0;
    const std::uint16_t rela_text_index = has_tls ? 5 : 4;
    const std::uint16_t symtab_index = has_tls ? 6 : 5;
    const std::uint16_t strtab_index = has_tls ? 7 : 6;
    const std::uint16_t note_stack_index = has_tls ? 8 : 7;
    const std::uint16_t shstrtab_index = has_tls ? 9 : 8;

    Aarch64StringTable strings;
    std::vector<Aarch64Symbol> symbols;
    symbols.push_back({});
    symbols.push_back({0, static_cast<std::uint8_t>((stb_local << 4U) | stt_section), 0, text_index, 0, 0});
    symbols.push_back({0, static_cast<std::uint8_t>((stb_local << 4U) | stt_section), 0, rodata_index, 0, 0});
    symbols.push_back({0, static_cast<std::uint8_t>((stb_local << 4U) | stt_section), 0, data_index, 0, 0});
    if (has_tls) symbols.push_back({0, static_cast<std::uint8_t>((stb_local << 4U) | stt_section), 0, tdata_index, 0, 0});
    std::unordered_map<std::string, std::uint32_t> symbol_indexes;
    const auto define = [&](const std::string& name, std::uint8_t binding, std::uint8_t type, std::uint16_t section,
                            std::uint64_t value, std::uint64_t size) -> bool {
        const auto index = static_cast<std::uint32_t>(symbols.size());
        if (!symbol_indexes.emplace(name, index).second) return false;
        symbols.push_back({strings.add(name), static_cast<std::uint8_t>((binding << 4U) | type), 0,
                           section, value, size});
        return true;
    };

    // ELF requires all STB_LOCAL symbols before global and undefined symbols.
    // Emit module-internal data first so separately compiled Raz modules may reuse
    // compiler-generated literal names without linker collisions.
    for (const auto& global : image.globals) {
        if (!global.is_internal) continue;
        const auto section = global.section == codegen::aarch64::DataSection::read_only ? rodata_index :
            global.section == codegen::aarch64::DataSection::tls ? tdata_index : data_index;
        const auto& bytes = global.section == codegen::aarch64::DataSection::read_only ? image.read_only_data :
            global.section == codegen::aarch64::DataSection::tls ? image.thread_local_data : image.writable_data;
        std::size_t size = bytes.size() - global.data_offset;
        for (const auto& next : image.globals)
            if (next.section == global.section && next.data_offset > global.data_offset)
                size = std::min(size, next.data_offset - global.data_offset);
        const auto type = global.section == codegen::aarch64::DataSection::tls ? stt_tls : stt_object;
        if (!define(global.name, stb_local, type, section, global.data_offset, size)) {
            add_aarch64_error(result.diagnostics, "duplicate AArch64 global symbol @" + global.name);
            return result;
        }
    }
    const std::uint32_t first_global_symbol = static_cast<std::uint32_t>(symbols.size());

    for (std::size_t index = 0; index < image.entries.size(); ++index) {
        const auto& [name, offset] = image.entries[index];
        const auto next = index + 1U < image.entries.size() ? image.entries[index + 1U].second : image.code.size();
        if (!define(name, stb_global, stt_func, text_index, offset, next >= offset ? next - offset : 0U)) {
            add_aarch64_error(result.diagnostics, "duplicate AArch64 function symbol @" + name);
            return result;
        }
    }
    for (const auto& global : image.globals) {
        if (global.is_internal) continue;
        const auto section = global.section == codegen::aarch64::DataSection::read_only ? rodata_index :
            global.section == codegen::aarch64::DataSection::tls ? tdata_index : data_index;
        const auto& bytes = global.section == codegen::aarch64::DataSection::read_only ? image.read_only_data :
            global.section == codegen::aarch64::DataSection::tls ? image.thread_local_data : image.writable_data;
        std::size_t size = bytes.size() - global.data_offset;
        for (const auto& next : image.globals)
            if (next.section == global.section && next.data_offset > global.data_offset)
                size = std::min(size, next.data_offset - global.data_offset);
        const auto type = global.section == codegen::aarch64::DataSection::tls ? stt_tls : stt_object;
        if (!define(global.name, stb_global, type, section, global.data_offset, size)) {
            add_aarch64_error(result.diagnostics, "duplicate AArch64 global symbol @" + global.name);
            return result;
        }
    }

    auto require_symbol = [&](const std::string& name) -> std::uint32_t {
        if (const auto found = symbol_indexes.find(name); found != symbol_indexes.end()) return found->second;
        const auto index = static_cast<std::uint32_t>(symbols.size());
        symbol_indexes.emplace(name, index);
        symbols.push_back({strings.add(name), static_cast<std::uint8_t>((stb_global << 4U) | stt_notype), 0,
                           shn_undef, 0, 0});
        return index;
    };

    // Seed declared externals even if a particular optimization removes their
    // last reference. This keeps symbol-table behavior deterministic across
    // optimization levels and mirrors the x86 ELF emitter.
    for (const auto& name : image.external_globals) (void)require_symbol(name);
    for (const auto& name : image.external_tls) (void)require_symbol(name);

    std::vector<Aarch64Relocation> relocations;
    relocations.reserve(image.relocations.size());
    for (const auto& relocation : image.relocations) {
        if ((relocation.offset & 3U) != 0U || relocation.offset + 4U > image.code.size()) {
            add_aarch64_error(result.diagnostics, "malformed AArch64 text relocation for @" + relocation.symbol);
            return result;
        }
        const auto type = elf_relocation_type(relocation.kind);
        if (type == 0U) {
            add_aarch64_error(result.diagnostics, "unknown AArch64 relocation for @" + relocation.symbol);
            return result;
        }
        relocations.push_back({relocation.offset, require_symbol(relocation.symbol), type, relocation.addend});
    }

    std::vector<Aarch64Section> sections(has_tls ? 10U : 9U);
    sections[text_index] = {".text", sht_progbits, shf_alloc | shf_execinstr, 4, 0, 0, 0, image.code};
    sections[rodata_index] = {".rodata", sht_progbits, shf_alloc, 16, 0, 0, 0, image.read_only_data};
    sections[data_index] = {".data", sht_progbits, shf_alloc | shf_write, 16, 0, 0, 0, image.writable_data};
    if (has_tls) sections[tdata_index] = {".tdata", sht_progbits, shf_alloc | shf_write | shf_tls, 16, 0, 0, 0, image.thread_local_data};
    sections[rela_text_index] = {".rela.text", sht_rela, 0, 8, 24, symtab_index, text_index, aarch64_relocation_bytes(relocations)};
    sections[symtab_index] = {".symtab", sht_symtab, 0, 8, 24, strtab_index, first_global_symbol, aarch64_symbol_bytes(symbols)};
    sections[strtab_index] = {".strtab", sht_strtab, 0, 1, 0, 0, 0, aarch64_chars_to_bytes(strings.bytes)};
    sections[note_stack_index] = {".note.GNU-stack", sht_progbits, 0, 1, 0, 0, 0, {}};

    Aarch64StringTable section_names;
    for (std::size_t index = 1; index < sections.size(); ++index) {
        if (index == shstrtab_index) continue;
        sections[index].name_offset = section_names.add(sections[index].name);
    }
    sections[shstrtab_index] = {".shstrtab", sht_strtab, 0, 1, 0, 0, 0, {}};
    sections[shstrtab_index].name_offset = section_names.add(".shstrtab");
    sections[shstrtab_index].data = aarch64_chars_to_bytes(section_names.bytes);

    Aarch64ByteWriter file;
    file.zeros(64);
    for (std::size_t index = 1; index < sections.size(); ++index) {
        file.align(static_cast<std::size_t>(sections[index].alignment));
        sections[index].file_offset = file.size();
        file.raw(sections[index].data);
    }
    file.align(8);
    const auto section_header_offset = file.size();
    for (const auto& section : sections) {
        file.integer(section.name_offset); file.integer(section.type); file.integer(section.flags);
        file.integer(std::uint64_t{0}); file.integer(section.file_offset);
        file.integer(static_cast<std::uint64_t>(section.data.size()));
        file.integer(section.link); file.integer(section.info); file.integer(section.alignment); file.integer(section.entry_size);
    }
    result.bytes = std::move(file).take();

    const auto put = [&](std::size_t offset, auto value) {
        using T = decltype(value);
        using U = std::make_unsigned_t<T>;
        const auto bits = static_cast<U>(value);
        for (std::size_t index = 0; index < sizeof(T); ++index)
            result.bytes.at(offset + index) = static_cast<std::byte>(static_cast<std::uint8_t>(bits >> (index * 8U)));
    };
    result.bytes[0] = std::byte{0x7f}; result.bytes[1] = std::byte{'E'};
    result.bytes[2] = std::byte{'L'}; result.bytes[3] = std::byte{'F'};
    result.bytes[4] = std::byte{2}; result.bytes[5] = std::byte{1}; result.bytes[6] = std::byte{1};
    put(16, std::uint16_t{1});
    put(18, em_aarch64);
    put(20, std::uint32_t{1});
    put(40, static_cast<std::uint64_t>(section_header_offset));
    put(52, std::uint16_t{64});
    put(58, std::uint16_t{64});
    put(60, static_cast<std::uint16_t>(sections.size()));
    put(62, shstrtab_index);

    result.stats.section_count = sections.size();
    result.stats.symbol_count = symbols.size();
    result.stats.relocation_count = relocations.size();
    result.stats.external_symbol_count = static_cast<std::size_t>(std::count_if(
        symbols.begin(), symbols.end(), [](const Aarch64Symbol& symbol) {
            return symbol.section == shn_undef && symbol.name != 0U;
        }));
    return result;
}

} // namespace

ElfObjectResult emit_elf64_aarch64(codegen::aarch64::EncodedModuleImage image) {
    return emit_elf64_aarch64_image(std::move(image));
}

ElfObjectResult emit_elf64_aarch64(const machine::Module& module, codegen::aarch64::Abi abi) {
    auto encoded = codegen::aarch64::encode_image(module, abi);
    if (!encoded.ok()) {
        ElfObjectResult result;
        result.diagnostics = std::move(encoded.diagnostics);
        return result;
    }
    return emit_elf64_aarch64_image(std::move(encoded.image));
}

} // namespace forge::object
