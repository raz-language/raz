// Copyright 2026 Mario Vinciguerra
// SPDX-License-Identifier: Apache-2.0

#include "forge/object/elf.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <limits>
#include <span>
#include <type_traits>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
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
constexpr std::uint32_t r_x86_64_pc32 = 2;
constexpr std::uint32_t r_x86_64_plt32 = 4;
constexpr std::uint32_t r_x86_64_gottpoff = 22;

void add_error(Diagnostics& diagnostics, std::string message) {
    diagnostics.push_back(Diagnostic{DiagnosticSeverity::error, std::move(message)});
}

class ByteWriter {
public:
    template <typename T>
    void integer(T value) {
        using U = std::make_unsigned_t<T>;
        const auto bits = static_cast<U>(value);
        for (std::size_t index = 0; index < sizeof(T); ++index) {
            bytes_.push_back(static_cast<std::byte>(static_cast<std::uint8_t>(bits >> (index * 8U))));
        }
    }

    void raw(std::span<const std::byte> bytes) { bytes_.insert(bytes_.end(), bytes.begin(), bytes.end()); }
    void zeros(std::size_t count) { bytes_.insert(bytes_.end(), count, std::byte{0}); }
    void align(std::size_t alignment) {
        if (alignment == 0) return;
        while ((bytes_.size() % alignment) != 0) bytes_.push_back(std::byte{0});
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
        if (bytes.size() > std::numeric_limits<std::uint32_t>::max()) return 0;
        const auto offset = static_cast<std::uint32_t>(bytes.size());
        bytes.insert(bytes.end(), value.begin(), value.end());
        bytes.push_back('\0');
        offsets.emplace(key, offset);
        return offset;
    }
};

struct Symbol {
    std::uint32_t name{};
    std::uint8_t info{};
    std::uint8_t other{};
    std::uint16_t section{};
    std::uint64_t value{};
    std::uint64_t size{};
};

struct Relocation {
    std::uint64_t offset{};
    std::uint32_t symbol{};
    std::uint32_t type{};
    std::int64_t addend{};
};

struct Section {
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

std::vector<std::byte> encode_symbols(const std::vector<Symbol>& symbols) {
    ByteWriter writer;
    for (const auto& symbol : symbols) {
        writer.integer(symbol.name);
        writer.integer(symbol.info);
        writer.integer(symbol.other);
        writer.integer(symbol.section);
        writer.integer(symbol.value);
        writer.integer(symbol.size);
    }
    return std::move(writer).take();
}

std::vector<std::byte> encode_relocations(const std::vector<Relocation>& relocations) {
    ByteWriter writer;
    for (const auto& relocation : relocations) {
        writer.integer(relocation.offset);
        writer.integer((static_cast<std::uint64_t>(relocation.symbol) << 32U) | relocation.type);
        writer.integer(relocation.addend);
    }
    return std::move(writer).take();
}

std::vector<std::byte> chars_to_bytes(const std::vector<char>& chars) {
    std::vector<std::byte> bytes;
    bytes.reserve(chars.size());
    for (const char value : chars) bytes.push_back(static_cast<std::byte>(static_cast<unsigned char>(value)));
    return bytes;
}

} // namespace

namespace {
ElfObjectResult emit_elf64_x86_64_image(codegen::x86_64::EncodedModuleImage image) {
    ElfObjectResult result;
    codegen::x86_64::ImageEncodeResult encoded;
    encoded.image = std::move(image);

    // Preserve the historical section layout when a module has no defined TLS.
    const bool has_tls_section = !encoded.image.thread_local_data.empty();
    constexpr std::uint16_t text_index = 1;
    constexpr std::uint16_t rodata_index = 2;
    constexpr std::uint16_t data_index = 3;
    const std::uint16_t tdata_index = has_tls_section ? 4 : 0;
    const std::uint16_t rela_text_index = has_tls_section ? 5 : 4;
    const std::uint16_t symtab_index = has_tls_section ? 6 : 5;
    const std::uint16_t strtab_index = has_tls_section ? 7 : 6;
    const std::uint16_t note_stack_index = has_tls_section ? 8 : 7;
    const std::uint16_t shstrtab_index = has_tls_section ? 9 : 8;

    StringTable strings;
    std::vector<Symbol> symbols;
    symbols.push_back({});
    // Local section symbols must precede global symbols.
    symbols.push_back({0, static_cast<std::uint8_t>((stb_local << 4U) | stt_section), 0, text_index, 0, 0});
    symbols.push_back({0, static_cast<std::uint8_t>((stb_local << 4U) | stt_section), 0, rodata_index, 0, 0});
    symbols.push_back({0, static_cast<std::uint8_t>((stb_local << 4U) | stt_section), 0, data_index, 0, 0});
    if (has_tls_section)
        symbols.push_back({0, static_cast<std::uint8_t>((stb_local << 4U) | stt_section), 0, tdata_index, 0, 0});
    const std::uint32_t first_global_symbol = has_tls_section ? 5U : 4U;

    std::unordered_map<std::string, std::uint32_t> symbol_indexes;
    auto add_defined_symbol = [&](const std::string& name, std::uint32_t index) -> bool {
        if (!symbol_indexes.emplace(name, index).second) {
            add_error(result.diagnostics, "duplicate defined symbol while emitting ELF object: " + name);
            return false;
        }
        return true;
    };
    for (std::size_t index = 0; index < encoded.image.entries.size(); ++index) {
        const auto& [name, offset] = encoded.image.entries[index];
        const auto next = index + 1 < encoded.image.entries.size()
            ? encoded.image.entries[index + 1].second
            : encoded.image.code.size();
        const auto symbol_index = static_cast<std::uint32_t>(symbols.size());
        if (!add_defined_symbol(name, symbol_index)) return result;
        symbols.push_back({strings.add(name), static_cast<std::uint8_t>((stb_global << 4U) | stt_func), 0,
                           text_index, offset, next >= offset ? next - offset : 0});
    }
    for (const auto& global : encoded.image.globals) {
        const auto section = global.section == codegen::x86_64::DataSection::read_only ? rodata_index :
            (global.section == codegen::x86_64::DataSection::tls ? tdata_index : data_index);
        const auto& bytes = global.section == codegen::x86_64::DataSection::read_only ? encoded.image.read_only_data :
            (global.section == codegen::x86_64::DataSection::tls ? encoded.image.thread_local_data : encoded.image.writable_data);
        std::size_t size = bytes.size() - global.data_offset;
        for (const auto& candidate : encoded.image.globals) {
            if (candidate.section == global.section && candidate.data_offset > global.data_offset)
                size = std::min(size, candidate.data_offset - global.data_offset);
        }
        const auto symbol_index = static_cast<std::uint32_t>(symbols.size());
        if (!add_defined_symbol(global.name, symbol_index)) return result;
        const auto symbol_type = global.section == codegen::x86_64::DataSection::tls ? stt_tls : stt_object;
        symbols.push_back({strings.add(global.name), static_cast<std::uint8_t>((stb_global << 4U) | symbol_type), 0,
                           section, global.data_offset, size});
    }

    auto require_external = [&](const std::string& name) -> std::uint32_t {
        if (const auto found = symbol_indexes.find(name); found != symbol_indexes.end()) return found->second;
        const auto index = static_cast<std::uint32_t>(symbols.size());
        symbol_indexes.emplace(name, index);
        symbols.push_back({strings.add(name), static_cast<std::uint8_t>((stb_global << 4U) | stt_notype), 0,
                           shn_undef, 0, 0});
        return index;
    };

    std::vector<Relocation> relocations;
    // JIT images use absolute-address thunks. Relocatable objects rewrite those
    // fixed-width sequences to PC-relative ELF forms while preserving offsets.
    for (const auto& relocation : encoded.image.externals) {
        if (relocation.address_offset < 2 || relocation.address_offset + 9 >= encoded.image.code.size()) {
            add_error(result.diagnostics, "malformed external function thunk while emitting ELF object");
            return result;
        }
        const auto start = relocation.address_offset - 2;
        encoded.image.code[start] = std::byte{0xE9}; // jmp rel32
        for (std::size_t index = 1; index < 12; ++index) encoded.image.code[start + index] = std::byte{0x90};
        for (std::size_t index = 1; index <= 4; ++index) encoded.image.code[start + index] = std::byte{0};
        relocations.push_back({start + 1, require_external(relocation.symbol), r_x86_64_plt32, -4});
    }
    auto rewrite_global_address = [&](std::size_t address_offset, std::uint32_t symbol) -> bool {
        if (address_offset < 2 || address_offset + 7 >= encoded.image.code.size()) return false;
        const auto start = address_offset - 2;
        encoded.image.code[start] = std::byte{0x48};
        encoded.image.code[start + 1] = std::byte{0x8D};
        encoded.image.code[start + 2] = std::byte{0x05}; // lea rax, [rip + rel32]
        for (std::size_t index = 3; index <= 6; ++index) encoded.image.code[start + index] = std::byte{0};
        encoded.image.code[start + 7] = std::byte{0x90};
        encoded.image.code[start + 8] = std::byte{0x90};
        encoded.image.code[start + 9] = std::byte{0x90};
        relocations.push_back({start + 3, symbol, r_x86_64_pc32, -4});
        return true;
    };
    for (const auto& relocation : encoded.image.external_globals) {
        if (!rewrite_global_address(relocation.address_offset, require_external(relocation.symbol))) {
            add_error(result.diagnostics, "malformed external global address while emitting ELF object");
            return result;
        }
    }
    for (const auto& relocation : encoded.image.global_relocations) {
        const auto found = std::find_if(encoded.image.globals.begin(), encoded.image.globals.end(),
            [&](const codegen::x86_64::EncodedGlobal& global) {
                return global.section == relocation.section && global.data_offset == relocation.data_offset;
            });
        if (found == encoded.image.globals.end()) {
            add_error(result.diagnostics, "unable to resolve internal global relocation while emitting ELF object");
            return result;
        }
        if (!rewrite_global_address(relocation.address_offset, symbol_indexes.at(found->name))) {
            add_error(result.diagnostics, "malformed internal global address while emitting ELF object");
            return result;
        }
    }
    auto rewrite_tls_address = [&](std::size_t address_offset, std::uint32_t symbol) -> bool {
        if (address_offset + 31 >= encoded.image.code.size()) return false;
        // Initial-exec TLS: mov rax, [rip + symbol@GOTTPOFF]; add rax, qword ptr fs:0.
        const std::array<std::uint8_t, 16> code = {
            0x48, 0x8B, 0x05, 0, 0, 0, 0,
            0x64, 0x48, 0x03, 0x04, 0x25, 0, 0, 0, 0
        };
        for (std::size_t index = 0; index < code.size(); ++index) encoded.image.code[address_offset + index] = static_cast<std::byte>(code[index]);
        for (std::size_t index = code.size(); index < 32; ++index) encoded.image.code[address_offset + index] = std::byte{0x90};
        relocations.push_back({address_offset + 3, symbol, r_x86_64_gottpoff, -4});
        return true;
    };
    for (const auto& relocation : encoded.image.external_tls) {
        if (!rewrite_tls_address(relocation.address_offset, require_external(relocation.symbol))) {
            add_error(result.diagnostics, "malformed external TLS address while emitting ELF object");
            return result;
        }
    }
    for (const auto& relocation : encoded.image.tls_relocations) {
        const auto found = std::find_if(encoded.image.globals.begin(), encoded.image.globals.end(),
            [&](const codegen::x86_64::EncodedGlobal& global) {
                return global.section == codegen::x86_64::DataSection::tls && global.data_offset == relocation.data_offset;
            });
        if (found == encoded.image.globals.end() ||
            !rewrite_tls_address(relocation.address_offset, symbol_indexes.at(found->name))) {
            add_error(result.diagnostics, "unable to resolve internal TLS relocation while emitting ELF object");
            return result;
        }
    }
    std::sort(relocations.begin(), relocations.end(), [](const auto& left, const auto& right) {
        return left.offset < right.offset;
    });

    std::vector<Section> sections(has_tls_section ? 10U : 9U);
    sections[text_index] = {".text", sht_progbits, shf_alloc | shf_execinstr, 16, 0, 0, 0, encoded.image.code};
    sections[rodata_index] = {".rodata", sht_progbits, shf_alloc, 16, 0, 0, 0, encoded.image.read_only_data};
    sections[data_index] = {".data", sht_progbits, shf_alloc | shf_write, 16, 0, 0, 0, encoded.image.writable_data};
    if (has_tls_section)
        sections[tdata_index] = {".tdata", sht_progbits, shf_alloc | shf_write | shf_tls, 16, 0, 0, 0, encoded.image.thread_local_data};
    sections[rela_text_index] = {".rela.text", sht_rela, 0, 8, 24, symtab_index, text_index, encode_relocations(relocations)};
    sections[symtab_index] = {".symtab", sht_symtab, 0, 8, 24, strtab_index, first_global_symbol, encode_symbols(symbols)};
    sections[strtab_index] = {".strtab", sht_strtab, 0, 1, 0, 0, 0, chars_to_bytes(strings.bytes)};
    sections[note_stack_index] = {".note.GNU-stack", sht_progbits, 0, 1, 0, 0, 0, {}};

    StringTable section_names;
    for (std::size_t index = 1; index < sections.size(); ++index) {
        if (index == shstrtab_index) continue;
        sections[index].name_offset = section_names.add(sections[index].name);
    }
    sections[shstrtab_index] = {".shstrtab", sht_strtab, 0, 1, 0, 0, 0, {}};
    sections[shstrtab_index].name_offset = section_names.add(".shstrtab");
    sections[shstrtab_index].data = chars_to_bytes(section_names.bytes);

    ByteWriter file;
    file.zeros(64); // ELF header, patched by construction below.
    for (std::size_t index = 1; index < sections.size(); ++index) {
        file.align(static_cast<std::size_t>(sections[index].alignment));
        sections[index].file_offset = file.size();
        file.raw(sections[index].data);
    }
    file.align(8);
    const auto section_header_offset = file.size();
    for (const auto& section : sections) {
        file.integer(section.name_offset);
        file.integer(section.type);
        file.integer(section.flags);
        file.integer(std::uint64_t{0}); // address in relocatable file
        file.integer(section.file_offset);
        file.integer(static_cast<std::uint64_t>(section.data.size()));
        file.integer(section.link);
        file.integer(section.info);
        file.integer(section.alignment);
        file.integer(section.entry_size);
    }
    result.bytes = std::move(file).take();

    // ELF64 header.
    auto put = [&](std::size_t offset, auto value) {
        using T = decltype(value);
        using U = std::make_unsigned_t<T>;
        const auto bits = static_cast<U>(value);
        for (std::size_t index = 0; index < sizeof(T); ++index)
            result.bytes.at(offset + index) = static_cast<std::byte>(static_cast<std::uint8_t>(bits >> (index * 8U)));
    };
    result.bytes[0] = std::byte{0x7f}; result.bytes[1] = std::byte{'E'};
    result.bytes[2] = std::byte{'L'}; result.bytes[3] = std::byte{'F'};
    result.bytes[4] = std::byte{2}; // ELFCLASS64
    result.bytes[5] = std::byte{1}; // little endian
    result.bytes[6] = std::byte{1}; // current version
    result.bytes[7] = std::byte{0}; // System V ABI
    put(16, std::uint16_t{1});      // ET_REL
    put(18, std::uint16_t{62});     // EM_X86_64
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
        symbols.begin(), symbols.end(), [](const Symbol& symbol) { return symbol.section == shn_undef && symbol.name != 0; }));
    return result;
}
} // namespace

ElfObjectResult emit_elf64_x86_64(codegen::x86_64::EncodedModuleImage image) {
    return emit_elf64_x86_64_image(std::move(image));
}

ElfObjectResult emit_elf64_x86_64(const machine::Module& module, codegen::x86_64::Abi abi) {
    ElfObjectResult result;
    if (abi != codegen::x86_64::Abi::system_v) {
        add_error(result.diagnostics, "ELF64 object output currently requires the System V x86-64 ABI");
        return result;
    }
    auto encoded = codegen::x86_64::encode_image(module, abi);
    if (!encoded.ok()) {
        result.diagnostics = std::move(encoded.diagnostics);
        return result;
    }

    return emit_elf64_x86_64_image(std::move(encoded.image));
}

} // namespace forge::object
