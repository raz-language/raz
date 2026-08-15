// Copyright 2026 Mario Vinciguerra
// SPDX-License-Identifier: Apache-2.0

#include "forge/object/archive.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <limits>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string_view>

namespace forge::object {
namespace {

constexpr std::string_view archive_magic = "!<arch>\n";
constexpr std::size_t member_header_size = 60;

void add_error(Diagnostics& diagnostics, std::string message) {
    diagnostics.emplace_back(DiagnosticSeverity::error, std::move(message));
}

template<class T>
T read_little(std::span<const std::byte> bytes, std::size_t offset) {
    if (offset > bytes.size() || sizeof(T) > bytes.size() - offset)
        throw std::runtime_error("truncated object while reading archive symbols");
    T value{};
    for (std::size_t index = 0; index < sizeof(T); ++index)
        value |= static_cast<T>(std::to_integer<std::uint8_t>(bytes[offset + index])) << (index * 8U);
    return value;
}

std::string bounded_string(std::span<const std::byte> bytes, std::size_t offset, std::size_t limit) {
    if (offset > bytes.size()) throw std::runtime_error("invalid object string offset");
    const auto end_limit = std::min(bytes.size(), offset + limit);
    std::string result;
    for (std::size_t index = offset; index < end_limit; ++index) {
        const char ch = static_cast<char>(std::to_integer<std::uint8_t>(bytes[index]));
        if (ch == '\0') return result;
        result.push_back(ch);
    }
    if (end_limit == bytes.size() || result.size() == limit) return result;
    return result;
}

std::vector<std::string> extract_elf_symbols(std::span<const std::byte> bytes) {
    if (bytes.size() < 64 || std::to_integer<std::uint8_t>(bytes[4]) != 2U ||
        std::to_integer<std::uint8_t>(bytes[5]) != 1U)
        throw std::runtime_error("unsupported ELF object format");
    const auto section_offset = read_little<std::uint64_t>(bytes, 40);
    const auto section_size = read_little<std::uint16_t>(bytes, 58);
    const auto section_count = read_little<std::uint16_t>(bytes, 60);
    if (section_size < 64 || section_offset > bytes.size() ||
        static_cast<std::uint64_t>(section_size) * section_count > bytes.size() - section_offset)
        throw std::runtime_error("invalid ELF section table");

    std::vector<std::string> symbols;
    for (std::uint16_t section = 0; section < section_count; ++section) {
        const auto header = static_cast<std::size_t>(section_offset) + static_cast<std::size_t>(section) * section_size;
        const auto type = read_little<std::uint32_t>(bytes, header + 4);
        if (type != 2U) continue;
        const auto symbol_offset = read_little<std::uint64_t>(bytes, header + 24);
        const auto symbol_size = read_little<std::uint64_t>(bytes, header + 32);
        const auto string_section = read_little<std::uint32_t>(bytes, header + 40);
        const auto entry_size = read_little<std::uint64_t>(bytes, header + 56);
        if (entry_size < 24 || string_section >= section_count || symbol_offset > bytes.size() ||
            symbol_size > bytes.size() - symbol_offset)
            throw std::runtime_error("invalid ELF symbol table");
        const auto string_header = static_cast<std::size_t>(section_offset) + static_cast<std::size_t>(string_section) * section_size;
        const auto string_offset = read_little<std::uint64_t>(bytes, string_header + 24);
        const auto string_size = read_little<std::uint64_t>(bytes, string_header + 32);
        if (string_offset > bytes.size() || string_size > bytes.size() - string_offset)
            throw std::runtime_error("invalid ELF string table");
        for (std::uint64_t cursor = 0; cursor + entry_size <= symbol_size; cursor += entry_size) {
            const auto entry = static_cast<std::size_t>(symbol_offset + cursor);
            const auto name_offset = read_little<std::uint32_t>(bytes, entry);
            const auto info = std::to_integer<std::uint8_t>(bytes[entry + 4]);
            const auto section_index = read_little<std::uint16_t>(bytes, entry + 6);
            const auto binding = info >> 4U;
            const auto symbol_type = info & 0x0fU;
            if ((binding != 1U && binding != 2U) || section_index == 0U ||
                (symbol_type != 0U && symbol_type != 1U && symbol_type != 2U) || name_offset >= string_size)
                continue;
            auto name = bounded_string(bytes, static_cast<std::size_t>(string_offset + name_offset),
                                       static_cast<std::size_t>(string_size - name_offset));
            if (!name.empty()) symbols.push_back(std::move(name));
        }
    }
    return symbols;
}

std::vector<std::string> extract_coff_symbols(std::span<const std::byte> bytes) {
    if (bytes.size() < 20 || read_little<std::uint16_t>(bytes, 0) != 0x8664U)
        throw std::runtime_error("unsupported COFF object format");
    const auto table_offset = read_little<std::uint32_t>(bytes, 8);
    const auto symbol_count = read_little<std::uint32_t>(bytes, 12);
    constexpr std::size_t entry_size = 18;
    if (table_offset > bytes.size() || static_cast<std::uint64_t>(symbol_count) * entry_size > bytes.size() - table_offset)
        throw std::runtime_error("invalid COFF symbol table");
    const auto string_offset = static_cast<std::size_t>(table_offset) + static_cast<std::size_t>(symbol_count) * entry_size;
    if (string_offset + 4U > bytes.size()) throw std::runtime_error("missing COFF string table");
    const auto string_size = read_little<std::uint32_t>(bytes, string_offset);
    if (string_size < 4U || string_size > bytes.size() - string_offset)
        throw std::runtime_error("invalid COFF string table");

    std::vector<std::string> symbols;
    for (std::uint32_t index = 0; index < symbol_count;) {
        const auto entry = static_cast<std::size_t>(table_offset) + static_cast<std::size_t>(index) * entry_size;
        const auto section = static_cast<std::int16_t>(read_little<std::uint16_t>(bytes, entry + 12));
        const auto storage = std::to_integer<std::uint8_t>(bytes[entry + 16]);
        const auto auxiliaries = std::to_integer<std::uint8_t>(bytes[entry + 17]);
        if (storage == 2U && section > 0) {
            std::string name;
            const auto zeroes = read_little<std::uint32_t>(bytes, entry);
            if (zeroes == 0U) {
                const auto offset = read_little<std::uint32_t>(bytes, entry + 4);
                if (offset >= 4U && offset < string_size)
                    name = bounded_string(bytes, string_offset + offset, string_size - offset);
            } else {
                name = bounded_string(bytes, entry, 8);
            }
            if (!name.empty()) symbols.push_back(std::move(name));
        }
        index += 1U + auxiliaries;
    }
    return symbols;
}

std::vector<std::string> extract_symbols(std::span<const std::byte> bytes) {
    if (bytes.size() >= 4 && std::to_integer<std::uint8_t>(bytes[0]) == 0x7fU &&
        std::to_integer<std::uint8_t>(bytes[1]) == 'E' &&
        std::to_integer<std::uint8_t>(bytes[2]) == 'L' &&
        std::to_integer<std::uint8_t>(bytes[3]) == 'F')
        return extract_elf_symbols(bytes);
    if (bytes.size() >= 2 && read_little<std::uint16_t>(bytes, 0) == 0x8664U)
        return extract_coff_symbols(bytes);
    throw std::runtime_error("archive member is not an ELF64 or COFF AMD64 object");
}

void append_ascii(std::vector<std::byte>& output, std::string_view text) {
    for (const char ch : text) output.push_back(static_cast<std::byte>(static_cast<unsigned char>(ch)));
}

void append_big_u32(std::vector<std::byte>& output, std::uint32_t value) {
    for (int shift = 24; shift >= 0; shift -= 8)
        output.push_back(static_cast<std::byte>((value >> shift) & 0xffU));
}

std::string decimal_field(std::uint64_t value, std::size_t width, int base = 10) {
    std::ostringstream stream;
    if (base == 8) stream << std::oct;
    stream << value;
    auto result = stream.str();
    if (result.size() > width) throw std::runtime_error("archive field overflow");
    result.append(width - result.size(), ' ');
    return result;
}

void append_header(std::vector<std::byte>& output, std::string name, std::size_t size) {
    if (name.size() > 16U) throw std::runtime_error("archive member name field overflow");
    name.append(16U - name.size(), ' ');
    append_ascii(output, name);
    append_ascii(output, decimal_field(0, 12));
    append_ascii(output, decimal_field(0, 6));
    append_ascii(output, decimal_field(0, 6));
    append_ascii(output, decimal_field(0100644, 8, 8));
    append_ascii(output, decimal_field(size, 10));
    append_ascii(output, "`\n");
}

void append_member(std::vector<std::byte>& output, std::string name,
                   std::span<const std::byte> data) {
    append_header(output, std::move(name), data.size());
    output.insert(output.end(), data.begin(), data.end());
    if ((data.size() & 1U) != 0U) output.push_back(static_cast<std::byte>('\n'));
}

std::vector<std::byte> read_file(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("cannot open object file: " + path.string());
    std::vector<char> data((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    std::vector<std::byte> bytes;
    bytes.reserve(data.size());
    for (const char ch : data) bytes.push_back(static_cast<std::byte>(static_cast<unsigned char>(ch)));
    return bytes;
}

} // namespace

ArchiveResult emit_static_archive(std::span<const ArchiveMember> members) {
    ArchiveResult result;
    try {
        if (members.empty()) throw std::runtime_error("static archive requires at least one object member");
        struct PreparedMember {
            const ArchiveMember* member{};
            std::string basename;
            std::string header_name;
            std::vector<std::string> symbols;
            std::size_t header_offset{};
        };
        std::vector<PreparedMember> prepared;
        prepared.reserve(members.size());
        std::set<std::string> names;
        std::string long_names;
        for (const auto& member : members) {
            if (member.bytes.empty()) throw std::runtime_error("archive member is empty: " + member.name);
            auto basename = std::filesystem::path(member.name).filename().string();
            if (basename.empty()) throw std::runtime_error("archive member has no filename");
            if (!names.insert(basename).second) throw std::runtime_error("duplicate archive member name: " + basename);
            auto symbols = extract_symbols(member.bytes);
            std::sort(symbols.begin(), symbols.end());
            symbols.erase(std::unique(symbols.begin(), symbols.end()), symbols.end());
            std::string header_name;
            const bool short_name = basename.size() <= 15U && basename.find_first_of(" /\n") == std::string::npos;
            if (short_name) {
                header_name = basename + "/";
            } else {
                const auto offset = long_names.size();
                header_name = "/" + std::to_string(offset);
                long_names += basename + "/\n";
                ++result.stats.long_name_count;
            }
            prepared.push_back({&member, std::move(basename), std::move(header_name), std::move(symbols), 0});
        }

        std::size_t symbol_count = 0;
        std::size_t symbol_name_bytes = 0;
        for (const auto& member : prepared) {
            symbol_count += member.symbols.size();
            for (const auto& symbol : member.symbols) symbol_name_bytes += symbol.size() + 1U;
        }
        if (symbol_count > std::numeric_limits<std::uint32_t>::max()) throw std::runtime_error("too many archive symbols");
        const std::size_t symbol_data_size = 4U + symbol_count * 4U + symbol_name_bytes;
        std::size_t cursor = archive_magic.size() + member_header_size + symbol_data_size + (symbol_data_size & 1U);
        if (!long_names.empty()) cursor += member_header_size + long_names.size() + (long_names.size() & 1U);
        for (auto& member : prepared) {
            member.header_offset = cursor;
            cursor += member_header_size + member.member->bytes.size() + (member.member->bytes.size() & 1U);
            if (member.header_offset > std::numeric_limits<std::uint32_t>::max())
                throw std::runtime_error("archive exceeds 32-bit symbol-index offset range");
        }

        std::vector<std::byte> symbol_data;
        symbol_data.reserve(symbol_data_size);
        append_big_u32(symbol_data, static_cast<std::uint32_t>(symbol_count));
        for (const auto& member : prepared)
            for ([[maybe_unused]] const auto& symbol : member.symbols)
                append_big_u32(symbol_data, static_cast<std::uint32_t>(member.header_offset));
        for (const auto& member : prepared)
            for (const auto& symbol : member.symbols) {
                append_ascii(symbol_data, symbol);
                symbol_data.push_back(std::byte{});
            }

        append_ascii(result.bytes, archive_magic);
        append_member(result.bytes, "/", symbol_data);
        if (!long_names.empty()) {
            std::vector<std::byte> long_data;
            long_data.reserve(long_names.size());
            append_ascii(long_data, long_names);
            append_member(result.bytes, "//", long_data);
        }
        for (const auto& member : prepared)
            append_member(result.bytes, member.header_name, member.member->bytes);

        result.stats.member_count = prepared.size();
        result.stats.symbol_count = symbol_count;
    } catch (const std::exception& error) {
        add_error(result.diagnostics, error.what());
        result.bytes.clear();
    }
    return result;
}

ArchiveResult emit_static_archive_from_files(std::span<const std::filesystem::path> object_paths) {
    ArchiveResult result;
    try {
        std::vector<ArchiveMember> members;
        members.reserve(object_paths.size());
        for (const auto& path : object_paths)
            members.push_back({path.filename().string(), read_file(path)});
        return emit_static_archive(members);
    } catch (const std::exception& error) {
        add_error(result.diagnostics, error.what());
        return result;
    }
}

} // namespace forge::object
