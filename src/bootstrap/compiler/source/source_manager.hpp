// Copyright 2026 Mario Vinciguerra
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "compiler/source/source_location.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace raz::compiler {

class SourceManager final {
 public:
  struct SourceFile final {
    SourceFileId id = kInvalidSourceFileId;
    std::filesystem::path path;
    std::filesystem::path diagnostic_path;
    std::int64_t diagnostic_line_delta = 0;
    std::int64_t diagnostic_byte_delta = 0;
    std::string text;
    std::vector<std::uint32_t> line_starts;
  };

  [[nodiscard]] std::optional<SourceFileId> load_file(
      const std::filesystem::path& path, std::string& error);
  [[nodiscard]] SourceFileId add_virtual_file(std::filesystem::path path,
                                               std::string text);
  [[nodiscard]] const SourceFile* get(SourceFileId id) const noexcept;
  [[nodiscard]] std::string_view text(SourceFileId id) const noexcept;
  [[nodiscard]] std::string_view slice(SourceRange range) const noexcept;
  [[nodiscard]] LineColumn line_column(SourceLocation location) const noexcept;
  [[nodiscard]] LineColumn display_line_column(SourceLocation location) const noexcept;
  [[nodiscard]] LspPosition lsp_position(SourceLocation location) const noexcept;
  [[nodiscard]] LspPosition display_lsp_position(SourceLocation location) const noexcept;
  void set_diagnostic_mapping(SourceFileId id, std::filesystem::path display_path,
                              std::int64_t line_delta, std::int64_t byte_delta);
  [[nodiscard]] std::uint32_t display_byte_offset(SourceLocation location) const noexcept;
  [[nodiscard]] std::string display_name(SourceFileId id) const;

 private:
  [[nodiscard]] static std::vector<std::uint32_t> compute_line_starts(
      std::string_view text);

  std::vector<SourceFile> files_;
};

}  // namespace raz::compiler
