// Copyright 2026 Mario Vinciguerra
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

#include "oblink/diagnostic.hpp"

namespace oblink {

// A member of a static library. Members are kept as raw bytes and only parsed
// as objects when a symbol they define is actually needed.
struct ArchiveMember {
    std::string name;
    std::size_t offset{};
    std::span<const std::byte> bytes;
};

// Short-form import members describe a symbol supplied by a DLL rather than an
// object file. They appear in import libraries in place of stub objects.
struct ArchiveImport {
    std::string symbol;
    std::string dll;
    std::uint16_t hint{};
    // 0 = code, 1 = data, 2 = const.
    std::uint16_t type{};
    // 0 = ordinal, 1 = name, 2 = name without prefix, 3 = undecorated name.
    std::uint16_t name_type{};
};

struct Archive {
    std::string name;
    std::vector<ArchiveMember> members;
    std::vector<ArchiveImport> imports;
    // Symbol name to member index, taken from the archive's own symbol index.
    std::unordered_map<std::string, std::size_t> symbol_index;
    // Symbol name to entry in `imports`.
    std::unordered_map<std::string, std::size_t> import_index;
};

struct ArchiveReadResult {
    Archive archive;
    Diagnostics diagnostics;
    [[nodiscard]] bool ok() const noexcept { return !has_error(diagnostics); }
};

// Parses a Microsoft static or import library. The returned members reference
// `bytes`, which must outlive the archive.
[[nodiscard]] ArchiveReadResult read_archive(std::span<const std::byte> bytes, std::string name);

} // namespace oblink
