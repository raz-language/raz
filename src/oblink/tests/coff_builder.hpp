// Copyright 2026 Mario Vinciguerra
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdint>
#include <string>
#include <vector>

// Minimal COFF object writer used by the oblink tests. It exists so the tests
// can build inputs by hand instead of depending on a code generator.
namespace oblink::testing {

struct BuiltRelocation {
    std::uint32_t virtual_address{};
    std::uint32_t symbol_index{};
    std::uint16_t type{};
};

struct BuiltSection {
    std::string name;
    std::uint32_t characteristics{};
    std::vector<std::uint8_t> data;
    std::vector<BuiltRelocation> relocations;
    std::uint32_t virtual_size{};
};

struct BuiltSymbol {
    std::string name;
    std::uint32_t value{};
    std::int16_t section_number{};
    std::uint16_t type{};
    std::uint8_t storage_class{2};
    // An optional 18-byte auxiliary record following the symbol, used for
    // COMDAT section definitions and weak externals.
    std::vector<std::uint8_t> aux;
};

class ObjectBuilder {
public:
    std::size_t add_section(BuiltSection section) {
        sections_.push_back(std::move(section));
        return sections_.size() - 1;
    }

    std::uint32_t add_symbol(BuiltSymbol symbol) {
        const auto index = next_index_;
        next_index_ += symbol.aux.empty() ? 1U : 2U;
        symbols_.push_back(std::move(symbol));
        return index;
    }

    // A section definition whose auxiliary record carries a COMDAT selection.
    std::uint32_t add_comdat_section_symbol(const std::string& name, std::int16_t section_number,
                                            std::uint8_t selection, std::uint32_t length) {
        BuiltSymbol symbol{name, 0, section_number, 0, 3, {}};
        symbol.aux.assign(18, 0);
        for (std::size_t index = 0; index < 4; ++index)
            symbol.aux[index] = static_cast<std::uint8_t>(length >> (index * 8U));
        symbol.aux[14] = selection;
        return add_symbol(std::move(symbol));
    }

    // A weak external whose auxiliary record names the fallback symbol.
    std::uint32_t add_weak_symbol(const std::string& name, std::uint32_t tag_index) {
        BuiltSymbol symbol{name, 0, 0, 0, 105, {}};
        symbol.aux.assign(18, 0);
        for (std::size_t index = 0; index < 4; ++index)
            symbol.aux[index] = static_cast<std::uint8_t>(tag_index >> (index * 8U));
        symbol.aux[4] = 2; // Search the library for a definition.
        return add_symbol(std::move(symbol));
    }

    [[nodiscard]] std::vector<std::byte> build() const {
        std::vector<std::uint8_t> bytes;
        auto u16 = [&bytes](std::uint16_t value) {
            bytes.push_back(static_cast<std::uint8_t>(value));
            bytes.push_back(static_cast<std::uint8_t>(value >> 8U));
        };
        auto u32 = [&bytes](std::uint32_t value) {
            for (unsigned shift = 0; shift < 32; shift += 8)
                bytes.push_back(static_cast<std::uint8_t>(value >> shift));
        };

        // Strings longer than eight bytes live in the trailing string table.
        std::vector<char> strings;
        auto intern = [&strings](const std::string& value) {
            const auto offset = static_cast<std::uint32_t>(strings.size() + 4U);
            strings.insert(strings.end(), value.begin(), value.end());
            strings.push_back('\0');
            return offset;
        };

        const std::size_t headers = 20U + sections_.size() * 40U;
        std::size_t cursor = headers;
        std::vector<std::uint32_t> data_offsets(sections_.size());
        std::vector<std::uint32_t> relocation_offsets(sections_.size());
        for (std::size_t index = 0; index < sections_.size(); ++index) {
            const auto& section = sections_[index];
            if (!section.data.empty()) {
                data_offsets[index] = static_cast<std::uint32_t>(cursor);
                cursor += section.data.size();
            }
            if (!section.relocations.empty()) {
                relocation_offsets[index] = static_cast<std::uint32_t>(cursor);
                cursor += section.relocations.size() * 10U;
            }
        }
        const auto symbol_table = static_cast<std::uint32_t>(cursor);

        std::size_t record_count = 0;
        for (const auto& symbol : symbols_) record_count += symbol.aux.empty() ? 1U : 2U;

        u16(0x8664);
        u16(static_cast<std::uint16_t>(sections_.size()));
        u32(0);
        u32(symbol_table);
        u32(static_cast<std::uint32_t>(record_count));
        u16(0);
        u16(0);

        for (std::size_t index = 0; index < sections_.size(); ++index) {
            const auto& section = sections_[index];
            for (std::size_t position = 0; position < 8; ++position)
                bytes.push_back(position < section.name.size()
                    ? static_cast<std::uint8_t>(section.name[position]) : 0U);
            u32(section.virtual_size);
            u32(0);
            u32(static_cast<std::uint32_t>(section.data.size()));
            u32(data_offsets[index]);
            u32(relocation_offsets[index]);
            u32(0);
            u16(static_cast<std::uint16_t>(section.relocations.size()));
            u16(0);
            u32(section.characteristics);
        }

        for (std::size_t index = 0; index < sections_.size(); ++index) {
            const auto& section = sections_[index];
            bytes.insert(bytes.end(), section.data.begin(), section.data.end());
            for (const auto& relocation : section.relocations) {
                u32(relocation.virtual_address);
                u32(relocation.symbol_index);
                u16(relocation.type);
            }
        }

        for (const auto& symbol : symbols_) {
            if (symbol.name.size() <= 8) {
                for (std::size_t position = 0; position < 8; ++position)
                    bytes.push_back(position < symbol.name.size()
                        ? static_cast<std::uint8_t>(symbol.name[position]) : 0U);
            } else {
                u32(0);
                u32(intern(symbol.name));
            }
            u32(symbol.value);
            u16(static_cast<std::uint16_t>(symbol.section_number));
            u16(symbol.type);
            bytes.push_back(symbol.storage_class);
            bytes.push_back(symbol.aux.empty() ? 0U : 1U);
            bytes.insert(bytes.end(), symbol.aux.begin(), symbol.aux.end());
        }

        const auto table_size = static_cast<std::uint32_t>(strings.size() + 4U);
        u32(table_size);
        for (const char ch : strings) bytes.push_back(static_cast<std::uint8_t>(ch));

        std::vector<std::byte> result(bytes.size());
        for (std::size_t index = 0; index < bytes.size(); ++index)
            result[index] = static_cast<std::byte>(bytes[index]);
        return result;
    }

private:
    std::vector<BuiltSection> sections_;
    std::vector<BuiltSymbol> symbols_;
    std::uint32_t next_index_{};
};

} // namespace oblink::testing
