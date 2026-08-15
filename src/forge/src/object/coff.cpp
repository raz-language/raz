// Copyright 2026 Mario Vinciguerra
// SPDX-License-Identifier: Apache-2.0

#include "forge/object/coff.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

namespace forge::object {
namespace {

constexpr std::uint16_t machine_amd64 = 0x8664;
constexpr std::uint16_t reloc_amd64_rel32 = 0x0004;
constexpr std::uint16_t reloc_amd64_secrel = 0x000B;
constexpr std::uint32_t section_text = 0x60000020;
constexpr std::uint32_t section_rdata = 0x40000040;
constexpr std::uint32_t section_data = 0xC0000040;
constexpr std::uint32_t section_tls = 0xC0000040;
constexpr std::uint8_t storage_external = 2;
constexpr std::uint16_t symbol_function = 0x20;

void add_error(Diagnostics& diagnostics, std::string message) {
    diagnostics.push_back(Diagnostic{DiagnosticSeverity::error, std::move(message)});
}

class ByteWriter {
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
    [[nodiscard]] std::size_t size() const noexcept { return bytes_.size(); }
    [[nodiscard]] std::vector<std::byte> take() && { return std::move(bytes_); }
private:
    std::vector<std::byte> bytes_;
};

struct StringTable {
    std::vector<char> bytes;
    std::unordered_map<std::string, std::uint32_t> offsets;

    std::uint32_t add(std::string_view value) {
        const std::string key(value);
        if (const auto found = offsets.find(key); found != offsets.end()) return found->second;
        if (bytes.size() + 4U > std::numeric_limits<std::uint32_t>::max()) return 0;
        const auto offset = static_cast<std::uint32_t>(bytes.size() + 4U);
        bytes.insert(bytes.end(), value.begin(), value.end());
        bytes.push_back('\0');
        offsets.emplace(key, offset);
        return offset;
    }
};

struct Symbol {
    std::string name;
    std::uint32_t value{};
    std::int16_t section{};
    std::uint16_t type{};
};

struct Relocation {
    std::uint32_t offset{};
    std::uint32_t symbol{};
    std::uint16_t type{reloc_amd64_rel32};
};

struct Section {
    std::array<char, 8> name{};
    std::vector<std::byte> data;
    std::vector<Relocation> relocations;
    std::uint32_t characteristics{};
    std::uint32_t raw_offset{};
    std::uint32_t relocation_offset{};
};

std::array<char, 8> short_name(std::string_view name) {
    std::array<char, 8> result{};
    const auto count = std::min(result.size(), name.size());
    std::copy_n(name.begin(), count, result.begin());
    return result;
}

void write_name(ByteWriter& writer, std::string_view name, StringTable& strings) {
    if (name.size() <= 8) {
        std::array<std::byte, 8> bytes{};
        for (std::size_t index = 0; index < name.size(); ++index)
            bytes[index] = static_cast<std::byte>(static_cast<unsigned char>(name[index]));
        writer.raw(bytes);
        return;
    }
    writer.integer(std::uint32_t{0});
    writer.integer(strings.add(name));
}

bool checked_u32(std::size_t value, std::uint32_t& output) {
    if (value > std::numeric_limits<std::uint32_t>::max()) return false;
    output = static_cast<std::uint32_t>(value);
    return true;
}

} // namespace

namespace {
CoffObjectResult emit_coff_x86_64_image(codegen::x86_64::EncodedModuleImage image) {
    CoffObjectResult result;
    codegen::x86_64::ImageEncodeResult encoded;
    encoded.image = std::move(image);

    const bool has_tls_section = !encoded.image.thread_local_data.empty();
    std::vector<Symbol> symbols;
    std::unordered_map<std::string, std::uint32_t> symbol_indexes;
    auto add_symbol = [&](std::string name, std::uint32_t value, std::int16_t section, std::uint16_t type) {
        const auto index = static_cast<std::uint32_t>(symbols.size());
        symbol_indexes.emplace(name, index);
        symbols.push_back(Symbol{std::move(name), value, section, type});
        return index;
    };
    auto add_defined_symbol = [&](const std::string& name, std::uint32_t value, std::int16_t section, std::uint16_t type) -> bool {
        if (symbol_indexes.contains(name)) {
            add_error(result.diagnostics, "duplicate defined symbol while emitting COFF object: " + name);
            return false;
        }
        (void)add_symbol(name, value, section, type);
        return true;
    };
    for (const auto& [name, offset] : encoded.image.entries) {
        std::uint32_t value{};
        if (!checked_u32(offset, value)) {
            add_error(result.diagnostics, "function offset exceeds COFF limits");
            return result;
        }
        if (!add_defined_symbol(name, value, 1, symbol_function)) return result;
    }
    for (const auto& global : encoded.image.globals) {
        std::uint32_t value{};
        if (!checked_u32(global.data_offset, value)) {
            add_error(result.diagnostics, "global offset exceeds COFF limits");
            return result;
        }
        const std::int16_t section = global.section == codegen::x86_64::DataSection::read_only ? 2 :
            (global.section == codegen::x86_64::DataSection::tls ? (has_tls_section ? 4 : 0) : 3);
        if (!add_defined_symbol(global.name, value, section, 0)) return result;
    }
    auto require_external = [&](const std::string& name) {
        if (const auto found = symbol_indexes.find(name); found != symbol_indexes.end()) return found->second;
        return add_symbol(name, 0, 0, 0);
    };

    std::vector<Relocation> text_relocations;
    auto push_relocation = [&](std::size_t offset, std::uint32_t symbol, std::uint16_t type = reloc_amd64_rel32) -> bool {
        std::uint32_t converted{};
        if (!checked_u32(offset, converted)) return false;
        text_relocations.push_back({converted, symbol, type});
        return true;
    };

    for (const auto& relocation : encoded.image.externals) {
        if (relocation.address_offset < 2 || relocation.address_offset + 9 >= encoded.image.code.size()) {
            add_error(result.diagnostics, "malformed external function thunk while emitting COFF object");
            return result;
        }
        const auto start = relocation.address_offset - 2;
        encoded.image.code[start] = std::byte{0xE9};
        for (std::size_t index = 1; index < 12; ++index) encoded.image.code[start + index] = std::byte{0x90};
        for (std::size_t index = 1; index <= 4; ++index) encoded.image.code[start + index] = std::byte{0};
        if (!push_relocation(start + 1, require_external(relocation.symbol))) {
            add_error(result.diagnostics, "COFF relocation offset exceeds format limits");
            return result;
        }
    }
    auto rewrite_global = [&](std::size_t address_offset, std::uint32_t symbol) {
        if (address_offset < 2 || address_offset + 7 >= encoded.image.code.size()) return false;
        const auto start = address_offset - 2;
        encoded.image.code[start] = std::byte{0x48};
        encoded.image.code[start + 1] = std::byte{0x8D};
        encoded.image.code[start + 2] = std::byte{0x05};
        for (std::size_t index = 3; index <= 6; ++index) encoded.image.code[start + index] = std::byte{0};
        encoded.image.code[start + 7] = std::byte{0x90};
        encoded.image.code[start + 8] = std::byte{0x90};
        encoded.image.code[start + 9] = std::byte{0x90};
        return push_relocation(start + 3, symbol);
    };
    for (const auto& relocation : encoded.image.external_globals) {
        if (!rewrite_global(relocation.address_offset, require_external(relocation.symbol))) {
            add_error(result.diagnostics, "malformed external global address while emitting COFF object");
            return result;
        }
    }
    for (const auto& relocation : encoded.image.global_relocations) {
        const auto found = std::find_if(encoded.image.globals.begin(), encoded.image.globals.end(),
            [&](const codegen::x86_64::EncodedGlobal& global) {
                return global.section == relocation.section && global.data_offset == relocation.data_offset;
            });
        if (found == encoded.image.globals.end() ||
            !rewrite_global(relocation.address_offset, symbol_indexes.at(found->name))) {
            add_error(result.diagnostics, "unable to resolve internal global relocation while emitting COFF object");
            return result;
        }
    }
    auto rewrite_tls = [&](std::size_t address_offset, std::uint32_t symbol) -> bool {
        if (address_offset + 31 >= encoded.image.code.size()) return false;
        // mov eax, dword ptr [rip + _tls_index]
        // mov rcx, qword ptr gs:[0x58]
        // mov rax, qword ptr [rcx + rax*8]
        // lea rax, [rax + symbol@SECREL32]
        const std::array<std::uint8_t, 27> code = {
            0x8B, 0x05, 0, 0, 0, 0,
            0x65, 0x48, 0x8B, 0x0C, 0x25, 0x58, 0, 0, 0,
            0x48, 0x8B, 0x04, 0xC1,
            0x48, 0x8D, 0x80, 0, 0, 0, 0
        };
        for (std::size_t index = 0; index < code.size(); ++index) encoded.image.code[address_offset + index] = static_cast<std::byte>(code[index]);
        for (std::size_t index = code.size(); index < 32; ++index) encoded.image.code[address_offset + index] = std::byte{0x90};
        if (!push_relocation(address_offset + 2, require_external("_tls_index"), reloc_amd64_rel32)) return false;
        return push_relocation(address_offset + 22, symbol, reloc_amd64_secrel);
    };
    for (const auto& relocation : encoded.image.external_tls) {
        if (!rewrite_tls(relocation.address_offset, require_external(relocation.symbol))) {
            add_error(result.diagnostics, "malformed external TLS address while emitting COFF object");
            return result;
        }
    }
    for (const auto& relocation : encoded.image.tls_relocations) {
        const auto found = std::find_if(encoded.image.globals.begin(), encoded.image.globals.end(),
            [&](const codegen::x86_64::EncodedGlobal& global) {
                return global.section == codegen::x86_64::DataSection::tls && global.data_offset == relocation.data_offset;
            });
        if (found == encoded.image.globals.end() || !rewrite_tls(relocation.address_offset, symbol_indexes.at(found->name))) {
            add_error(result.diagnostics, "unable to resolve internal TLS relocation while emitting COFF object");
            return result;
        }
    }
    std::sort(text_relocations.begin(), text_relocations.end(),
              [](const auto& left, const auto& right) { return left.offset < right.offset; });

    std::vector<Section> sections;
    sections.push_back({short_name(".text"), std::move(encoded.image.code), std::move(text_relocations), section_text});
    sections.push_back({short_name(".rdata"), std::move(encoded.image.read_only_data), {}, section_rdata});
    sections.push_back({short_name(".data"), std::move(encoded.image.writable_data), {}, section_data});
    if (has_tls_section) sections.push_back({short_name(".tls$AAA"), std::move(encoded.image.thread_local_data), {}, section_tls});

    constexpr std::size_t file_header_size = 20;
    constexpr std::size_t section_header_size = 40;
    std::size_t cursor = file_header_size + sections.size() * section_header_size;
    auto align_cursor = [](std::size_t value, std::size_t alignment) {
        return (value + alignment - 1U) & ~(alignment - 1U);
    };
    for (auto& section : sections) {
        cursor = align_cursor(cursor, 16);
        if (!checked_u32(cursor, section.raw_offset)) {
            add_error(result.diagnostics, "COFF section offset exceeds format limits");
            return result;
        }
        cursor += section.data.size();
        if (!section.relocations.empty()) {
            if (!checked_u32(cursor, section.relocation_offset)) {
                add_error(result.diagnostics, "COFF relocation table offset exceeds format limits");
                return result;
            }
            cursor += section.relocations.size() * 10U;
        }
    }
    std::uint32_t symbol_table_offset{};
    if (!checked_u32(cursor, symbol_table_offset)) {
        add_error(result.diagnostics, "COFF symbol table offset exceeds format limits");
        return result;
    }

    StringTable strings;
    ByteWriter symbol_writer;
    for (const auto& symbol : symbols) {
        write_name(symbol_writer, symbol.name, strings);
        symbol_writer.integer(symbol.value);
        symbol_writer.integer(symbol.section);
        symbol_writer.integer(symbol.type);
        symbol_writer.integer(storage_external);
        symbol_writer.integer(std::uint8_t{0});
    }
    const auto symbol_bytes = std::move(symbol_writer).take();

    ByteWriter writer;
    writer.integer(machine_amd64);
    writer.integer(static_cast<std::uint16_t>(sections.size()));
    writer.integer(std::uint32_t{0});
    writer.integer(symbol_table_offset);
    writer.integer(static_cast<std::uint32_t>(symbols.size()));
    writer.integer(std::uint16_t{0});
    writer.integer(std::uint16_t{0});
    for (const auto& section : sections) {
        for (const char value : section.name)
            writer.integer(static_cast<std::uint8_t>(static_cast<unsigned char>(value)));
        writer.integer(std::uint32_t{0});
        writer.integer(std::uint32_t{0});
        writer.integer(static_cast<std::uint32_t>(section.data.size()));
        writer.integer(section.raw_offset);
        writer.integer(section.relocation_offset);
        writer.integer(std::uint32_t{0});
        writer.integer(static_cast<std::uint16_t>(section.relocations.size()));
        writer.integer(std::uint16_t{0});
        writer.integer(section.characteristics);
    }
    for (const auto& section : sections) {
        while (writer.size() < section.raw_offset) writer.zeros(1);
        writer.raw(section.data);
        for (const auto& relocation : section.relocations) {
            writer.integer(relocation.offset);
            writer.integer(relocation.symbol);
            writer.integer(relocation.type);
        }
    }
    writer.raw(symbol_bytes);
    writer.integer(static_cast<std::uint32_t>(strings.bytes.size() + 4U));
    for (const char value : strings.bytes)
        writer.integer(static_cast<std::uint8_t>(static_cast<unsigned char>(value)));
    result.bytes = std::move(writer).take();
    result.stats.section_count = sections.size();
    result.stats.symbol_count = symbols.size();
    result.stats.relocation_count = sections.front().relocations.size();
    result.stats.external_symbol_count = static_cast<std::size_t>(std::count_if(
        symbols.begin(), symbols.end(), [](const Symbol& symbol) { return symbol.section == 0; }));
    return result;
}
} // namespace

CoffObjectResult emit_coff_x86_64(codegen::x86_64::EncodedModuleImage image) {
    return emit_coff_x86_64_image(std::move(image));
}

CoffObjectResult emit_coff_x86_64(const machine::Module& module, codegen::x86_64::Abi abi) {
    CoffObjectResult result;
    if (abi != codegen::x86_64::Abi::windows) {
        add_error(result.diagnostics, "COFF object output requires the Windows x64 ABI");
        return result;
    }
    auto encoded = codegen::x86_64::encode_image(module, abi);
    if (!encoded.ok()) {
        result.diagnostics = std::move(encoded.diagnostics);
        return result;
    }

    return emit_coff_x86_64_image(std::move(encoded.image));
}

} // namespace forge::object
