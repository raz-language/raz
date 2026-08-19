// Copyright 2026 Mario Vinciguerra
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdint>
#include <string>
#include <vector>

// Minimal static/import library writer for the oblink tests, producing the
// member layout and symbol index that Microsoft tools emit.
namespace oblink::testing {

class ArchiveBuilder {
public:
    void add_member(std::string name, std::vector<std::byte> object, std::vector<std::string> symbols) {
        Member member;
        member.name = std::move(name);
        member.bytes.reserve(object.size());
        for (const auto byte : object) member.bytes.push_back(std::to_integer<std::uint8_t>(byte));
        member.symbols = std::move(symbols);
        members_.push_back(std::move(member));
    }

    // Short-form import member: two magic words, a version, machine, and the
    // symbol and DLL names.
    void add_import_member(const std::string& symbol, const std::string& dll, std::uint16_t hint) {
        Member member;
        member.name = dll;
        auto& bytes = member.bytes;
        auto u16 = [&bytes](std::uint16_t value) {
            bytes.push_back(static_cast<std::uint8_t>(value));
            bytes.push_back(static_cast<std::uint8_t>(value >> 8U));
        };
        auto u32 = [&bytes](std::uint32_t value) {
            for (unsigned shift = 0; shift < 32; shift += 8)
                bytes.push_back(static_cast<std::uint8_t>(value >> shift));
        };

        u16(0x0000);                       // Sig1: IMAGE_FILE_MACHINE_UNKNOWN
        u16(0xFFFF);                       // Sig2
        u16(0);                            // Version
        u16(0x8664);                       // Machine
        u32(0);                            // Time stamp
        u32(static_cast<std::uint32_t>(symbol.size() + dll.size() + 2)); // Size of data
        u16(hint);                         // Ordinal or hint
        u16(0x0004);                       // Name type 1 (name), import type 0 (code)
        for (const char ch : symbol) bytes.push_back(static_cast<std::uint8_t>(ch));
        bytes.push_back(0);
        for (const char ch : dll) bytes.push_back(static_cast<std::uint8_t>(ch));
        bytes.push_back(0);

        member.symbols = {symbol};
        member.import = true;
        members_.push_back(std::move(member));
    }

    [[nodiscard]] std::vector<std::byte> build() const {
        // Names longer than fifteen characters live in the "//" member.
        std::vector<std::uint8_t> long_names;
        std::vector<std::string> stored_names;
        for (const auto& member : members_) {
            if (member.name.size() <= 15) {
                stored_names.push_back(member.name + "/");
                continue;
            }
            stored_names.push_back("/" + std::to_string(long_names.size()));
            for (const char ch : member.name) long_names.push_back(static_cast<std::uint8_t>(ch));
            long_names.push_back(0);
        }

        // The first linker member holds one big-endian offset per symbol, so
        // its size depends on offsets that depend on its size: compute it in
        // two passes.
        std::size_t symbol_count = 0;
        std::size_t names_size = 0;
        for (const auto& member : members_) {
            symbol_count += member.symbols.size();
            for (const auto& symbol : member.symbols) names_size += symbol.size() + 1;
        }
        const std::size_t index_size = 4 + symbol_count * 4 + names_size;

        std::vector<std::size_t> member_offsets(members_.size());
        std::size_t cursor = 8; // "!<arch>\n"
        cursor += 60 + index_size + (index_size % 2);
        if (!long_names.empty()) cursor += 60 + long_names.size() + (long_names.size() % 2);
        for (std::size_t index = 0; index < members_.size(); ++index) {
            member_offsets[index] = cursor;
            cursor += 60 + members_[index].bytes.size();
            if (members_[index].bytes.size() % 2 != 0) ++cursor;
        }

        std::vector<std::uint8_t> bytes;
        for (const char ch : std::string("!<arch>\n")) bytes.push_back(static_cast<std::uint8_t>(ch));

        std::vector<std::uint8_t> index_member;
        auto be32 = [&index_member](std::uint32_t value) {
            for (int shift = 24; shift >= 0; shift -= 8)
                index_member.push_back(static_cast<std::uint8_t>(value >> shift));
        };
        be32(static_cast<std::uint32_t>(symbol_count));
        for (std::size_t index = 0; index < members_.size(); ++index)
            for (std::size_t entry = 0; entry < members_[index].symbols.size(); ++entry)
                be32(static_cast<std::uint32_t>(member_offsets[index]));
        for (const auto& member : members_)
            for (const auto& symbol : member.symbols) {
                for (const char ch : symbol) index_member.push_back(static_cast<std::uint8_t>(ch));
                index_member.push_back(0);
            }

        append_member(bytes, "/", index_member);
        if (!long_names.empty()) append_member(bytes, "//", long_names);
        for (std::size_t index = 0; index < members_.size(); ++index)
            append_member(bytes, stored_names[index], members_[index].bytes);

        std::vector<std::byte> result(bytes.size());
        for (std::size_t index = 0; index < bytes.size(); ++index)
            result[index] = static_cast<std::byte>(bytes[index]);
        return result;
    }

private:
    struct Member {
        std::string name;
        std::vector<std::uint8_t> bytes;
        std::vector<std::string> symbols;
        bool import{};
    };

    static void append_member(std::vector<std::uint8_t>& bytes, const std::string& name,
                              const std::vector<std::uint8_t>& data) {
        auto field = [&bytes](const std::string& value, std::size_t width) {
            for (std::size_t index = 0; index < width; ++index)
                bytes.push_back(index < value.size() ? static_cast<std::uint8_t>(value[index]) : ' ');
        };
        field(name, 16);
        field("0", 12);          // Date
        field("0", 6);           // User id
        field("0", 6);           // Group id
        field("0", 8);           // Mode
        field(std::to_string(data.size()), 10);
        bytes.push_back('`');
        bytes.push_back('\n');
        bytes.insert(bytes.end(), data.begin(), data.end());
        if (data.size() % 2 != 0) bytes.push_back('\n');
    }

    std::vector<Member> members_;
};

} // namespace oblink::testing
