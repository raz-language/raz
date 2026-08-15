// Copyright 2026 Mario Vinciguerra
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstddef>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

#include "forge/diagnostics/diagnostic.hpp"

namespace forge::object {

struct ArchiveMember {
    std::string name;
    std::vector<std::byte> bytes;
};

struct ArchiveStats {
    std::size_t member_count{};
    std::size_t symbol_count{};
    std::size_t long_name_count{};
};

struct ArchiveResult {
    std::vector<std::byte> bytes;
    Diagnostics diagnostics;
    ArchiveStats stats;
    [[nodiscard]] bool ok() const noexcept { return diagnostics.empty(); }
};

[[nodiscard]] ArchiveResult emit_static_archive(std::span<const ArchiveMember> members);
[[nodiscard]] ArchiveResult emit_static_archive_from_files(
    std::span<const std::filesystem::path> object_paths);

} // namespace forge::object
