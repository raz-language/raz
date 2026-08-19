// Copyright 2026 Mario Vinciguerra
// SPDX-License-Identifier: Apache-2.0

#include "oblink/archive.hpp"

#include <algorithm>
#include <charconv>
#include <cstring>
#include <string_view>

#include "oblink/format.hpp"

namespace oblink {
namespace {

constexpr std::string_view archive_magic{"!<arch>\n"};
constexpr std::size_t header_size = 60;
constexpr std::uint16_t import_object_magic1 = 0x0000;
constexpr std::uint16_t import_object_magic2 = 0xFFFF;

std::uint16_t read_u16(std::span<const std::byte> bytes, std::size_t offset) {
    return static_cast<std::uint16_t>(
        std::to_integer<std::uint8_t>(bytes[offset]) |
        (static_cast<std::uint16_t>(std::to_integer<std::uint8_t>(bytes[offset + 1])) << 8U));
}

std::uint32_t read_u32_le(std::span<const std::byte> bytes, std::size_t offset) {
    std::uint32_t value{};
    for (std::size_t index = 0; index < 4; ++index)
        value |= static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(bytes[offset + index])) << (index * 8U);
    return value;
}

// The first linker member stores its offsets big-endian; the second uses
// little-endian. Both appear in libraries produced by Microsoft tools.
std::uint32_t read_u32_be(std::span<const std::byte> bytes, std::size_t offset) {
    std::uint32_t value{};
    for (std::size_t index = 0; index < 4; ++index)
        value = (value << 8U) | std::to_integer<std::uint8_t>(bytes[offset + index]);
    return value;
}

std::string trim(std::string_view value) {
    while (!value.empty() && (value.back() == ' ' || value.back() == '\0')) value.remove_suffix(1);
    while (!value.empty() && value.front() == ' ') value.remove_prefix(1);
    return std::string(value);
}

std::string read_c_string(std::span<const std::byte> bytes, std::size_t offset) {
    std::string value;
    while (offset < bytes.size()) {
        const auto ch = std::to_integer<char>(bytes[offset]);
        if (ch == '\0') break;
        value.push_back(ch);
        ++offset;
    }
    return value;
}

} // namespace

ArchiveReadResult read_archive(std::span<const std::byte> bytes, std::string name) {
    ArchiveReadResult result;
    result.archive.name = std::move(name);

    if (bytes.size() < archive_magic.size()) {
        add_error(result.diagnostics, "archive is too small: " + result.archive.name);
        return result;
    }
    for (std::size_t index = 0; index < archive_magic.size(); ++index) {
        if (std::to_integer<char>(bytes[index]) != archive_magic[index]) {
            add_error(result.diagnostics, "not an archive: " + result.archive.name);
            return result;
        }
    }

    // Members are laid out back to back, each preceded by a fixed header and
    // padded to an even offset.
    struct RawMember {
        std::string raw_name;
        std::size_t offset{};
        std::span<const std::byte> data;
    };
    std::vector<RawMember> raw_members;
    std::span<const std::byte> long_names;
    std::span<const std::byte> first_linker;
    std::span<const std::byte> second_linker;

    std::size_t cursor = archive_magic.size();
    while (cursor + header_size <= bytes.size()) {
        const auto header = bytes.subspan(cursor, header_size);
        std::string raw_name;
        for (std::size_t index = 0; index < 16; ++index)
            raw_name.push_back(std::to_integer<char>(header[index]));
        raw_name = trim(raw_name);

        std::string size_text;
        for (std::size_t index = 48; index < 58; ++index)
            size_text.push_back(std::to_integer<char>(header[index]));
        size_text = trim(size_text);

        std::size_t size = 0;
        const auto* begin = size_text.data();
        const auto* end = begin + size_text.size();
        if (std::from_chars(begin, end, size).ec != std::errc{}) {
            add_error(result.diagnostics, "archive member has an unreadable size in " + result.archive.name);
            return result;
        }

        const auto data_offset = cursor + header_size;
        if (data_offset + size > bytes.size()) {
            add_error(result.diagnostics, "archive member extends past the end of " + result.archive.name);
            return result;
        }
        const auto data = bytes.subspan(data_offset, size);

        if (raw_name == "/") {
            if (first_linker.empty()) first_linker = data;
            else second_linker = data;
        } else if (raw_name == "//") {
            long_names = data;
        } else {
            raw_members.push_back(RawMember{raw_name, data_offset, data});
        }

        cursor = data_offset + size;
        if ((cursor % 2) != 0) ++cursor; // Members start on even offsets.
    }

    // Member names either sit inline, ending with "/", or reference the long
    // name member as "/<offset>".
    auto resolve_name = [&](const std::string& raw) {
        if (!raw.empty() && raw.front() == '/') {
            std::size_t offset = 0;
            const auto* begin = raw.data() + 1;
            const auto* end = raw.data() + raw.size();
            if (std::from_chars(begin, end, offset).ec == std::errc{} && offset < long_names.size())
                return read_c_string(long_names, offset);
            return raw;
        }
        if (!raw.empty() && raw.back() == '/') return raw.substr(0, raw.size() - 1);
        return raw;
    };

    std::unordered_map<std::size_t, std::size_t> member_by_offset;
    for (const auto& raw : raw_members) {
        ArchiveMember member;
        member.name = resolve_name(raw.raw_name);
        member.offset = raw.offset;
        member.bytes = raw.data;

        // Import libraries carry short-form members instead of objects.
        if (member.bytes.size() >= 20 && read_u16(member.bytes, 0) == import_object_magic1 &&
            read_u16(member.bytes, 2) == import_object_magic2) {
            ArchiveImport import;
            import.hint = read_u16(member.bytes, 16);
            const auto flags = read_u16(member.bytes, 18);
            import.type = static_cast<std::uint16_t>(flags & 0x3U);
            import.name_type = static_cast<std::uint16_t>((flags >> 2U) & 0x7U);
            import.symbol = read_c_string(member.bytes, 20);
            import.dll = read_c_string(member.bytes, 20 + import.symbol.size() + 1);
            result.archive.import_index.emplace(import.symbol, result.archive.imports.size());
            result.archive.imports.push_back(std::move(import));
            continue;
        }

        member_by_offset.emplace(raw.offset - header_size, result.archive.members.size());
        result.archive.members.push_back(std::move(member));
    }

    // Prefer the second linker member: it is sorted and little-endian. Fall
    // back to the first, which every archive has.
    if (!second_linker.empty() && second_linker.size() >= 4) {
        const auto member_count = read_u32_le(second_linker, 0);
        const std::size_t offsets_at = 4;
        const std::size_t symbol_count_at = offsets_at + static_cast<std::size_t>(member_count) * 4;
        if (symbol_count_at + 4 <= second_linker.size()) {
            const auto symbol_count = read_u32_le(second_linker, symbol_count_at);
            const std::size_t indices_at = symbol_count_at + 4;
            const std::size_t names_at = indices_at + static_cast<std::size_t>(symbol_count) * 2;
            std::size_t name_cursor = names_at;
            for (std::uint32_t index = 0; index < symbol_count && name_cursor < second_linker.size(); ++index) {
                const auto symbol = read_c_string(second_linker, name_cursor);
                name_cursor += symbol.size() + 1;
                const auto member_ordinal = read_u16(second_linker, indices_at + static_cast<std::size_t>(index) * 2);
                if (member_ordinal == 0 || member_ordinal > member_count) continue;
                const auto member_offset =
                    read_u32_le(second_linker, offsets_at + static_cast<std::size_t>(member_ordinal - 1) * 4);
                if (const auto found = member_by_offset.find(member_offset); found != member_by_offset.end())
                    result.archive.symbol_index.emplace(symbol, found->second);
            }
        }
    } else if (!first_linker.empty() && first_linker.size() >= 4) {
        const auto symbol_count = read_u32_be(first_linker, 0);
        const std::size_t offsets_at = 4;
        std::size_t name_cursor = offsets_at + static_cast<std::size_t>(symbol_count) * 4;
        for (std::uint32_t index = 0; index < symbol_count && name_cursor < first_linker.size(); ++index) {
            const auto symbol = read_c_string(first_linker, name_cursor);
            name_cursor += symbol.size() + 1;
            const auto member_offset = read_u32_be(first_linker, offsets_at + static_cast<std::size_t>(index) * 4);
            if (const auto found = member_by_offset.find(member_offset); found != member_by_offset.end())
                result.archive.symbol_index.emplace(symbol, found->second);
        }
    }

    return result;
}

} // namespace oblink
