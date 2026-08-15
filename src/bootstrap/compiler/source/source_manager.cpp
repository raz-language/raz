// Copyright 2026 Mario Vinciguerra
// SPDX-License-Identifier: Apache-2.0

#include "compiler/source/source_manager.hpp"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <limits>

namespace raz::compiler {

std::optional<SourceFileId> SourceManager::load_file(
    const std::filesystem::path& path, std::string& error) {
  std::ifstream stream(path, std::ios::binary);
  if (!stream) {
    error = "unable to open source file: " + path.string();
    return std::nullopt;
  }

  std::string text((std::istreambuf_iterator<char>(stream)),
                   std::istreambuf_iterator<char>());
  if (!stream.good() && !stream.eof()) {
    error = "unable to read source file: " + path.string();
    return std::nullopt;
  }

  if (text.size() > std::numeric_limits<std::uint32_t>::max()) {
    error = "source file exceeds Raz's 4 GiB per-file limit: " + path.string();
    return std::nullopt;
  }

  return add_virtual_file(path, std::move(text));
}

SourceFileId SourceManager::add_virtual_file(std::filesystem::path path,
                                              std::string text) {
  const auto id = static_cast<SourceFileId>(files_.size() + 1);
  SourceFile file;
  file.id = id;
  file.path = std::move(path);
  file.text = std::move(text);
  files_.push_back(std::move(file));
  files_.back().line_starts = compute_line_starts(files_.back().text);
  return id;
}

const SourceManager::SourceFile* SourceManager::get(SourceFileId id) const noexcept {
  if (id == kInvalidSourceFileId || id > files_.size()) {
    return nullptr;
  }
  return &files_[id - 1];
}

std::string_view SourceManager::text(SourceFileId id) const noexcept {
  const auto* file = get(id);
  return file == nullptr ? std::string_view{} : std::string_view(file->text);
}

std::string_view SourceManager::slice(SourceRange range) const noexcept {
  if (!range.valid()) {
    return {};
  }
  const auto contents = text(range.begin.file);
  if (range.end.offset > contents.size()) {
    return {};
  }
  return contents.substr(range.begin.offset, range.size());
}

LineColumn SourceManager::line_column(SourceLocation location) const noexcept {
  const auto* file = get(location.file);
  if (file == nullptr || location.offset > file->text.size()) {
    return {};
  }
  const auto upper = std::upper_bound(file->line_starts.begin(),
                                      file->line_starts.end(), location.offset);
  const auto index = upper == file->line_starts.begin()
                         ? std::size_t{0}
                         : static_cast<std::size_t>(upper - file->line_starts.begin() - 1);
  return LineColumn{static_cast<std::uint32_t>(index + 1),
                    location.offset - file->line_starts[index] + 1};
}

LineColumn SourceManager::display_line_column(SourceLocation location) const noexcept {
  auto result = line_column(location);
  const auto* file = get(location.file);
  if (file == nullptr) return result;
  const auto mapped = static_cast<std::int64_t>(result.line) + file->diagnostic_line_delta;
  result.line = static_cast<std::uint32_t>(std::max<std::int64_t>(1, mapped));
  return result;
}

LspPosition SourceManager::lsp_position(SourceLocation location) const noexcept {
  const auto* file = get(location.file);
  if (file == nullptr || location.offset > file->text.size()) return {};
  const auto upper = std::upper_bound(file->line_starts.begin(), file->line_starts.end(), location.offset);
  const auto line_index = upper == file->line_starts.begin()
      ? std::size_t{0}
      : static_cast<std::size_t>(upper - file->line_starts.begin() - 1);
  const auto line_start = static_cast<std::size_t>(file->line_starts[line_index]);
  const auto end = static_cast<std::size_t>(location.offset);
  std::uint32_t utf16 = 0;
  for (std::size_t i = line_start; i < end;) {
    const auto lead = static_cast<unsigned char>(file->text[i]);
    std::uint32_t codepoint = lead;
    std::size_t width = 1;
    if ((lead & 0xE0U) == 0xC0U && i + 1 < end) {
      codepoint = ((lead & 0x1FU) << 6U) | (static_cast<unsigned char>(file->text[i + 1]) & 0x3FU);
      width = 2;
    } else if ((lead & 0xF0U) == 0xE0U && i + 2 < end) {
      codepoint = ((lead & 0x0FU) << 12U) | ((static_cast<unsigned char>(file->text[i + 1]) & 0x3FU) << 6U) |
                  (static_cast<unsigned char>(file->text[i + 2]) & 0x3FU);
      width = 3;
    } else if ((lead & 0xF8U) == 0xF0U && i + 3 < end) {
      codepoint = ((lead & 0x07U) << 18U) | ((static_cast<unsigned char>(file->text[i + 1]) & 0x3FU) << 12U) |
                  ((static_cast<unsigned char>(file->text[i + 2]) & 0x3FU) << 6U) |
                  (static_cast<unsigned char>(file->text[i + 3]) & 0x3FU);
      width = 4;
    }
    utf16 += codepoint > 0xFFFFU ? 2U : 1U;
    i += width;
  }
  return LspPosition{static_cast<std::uint32_t>(line_index), utf16};
}

LspPosition SourceManager::display_lsp_position(SourceLocation location) const noexcept {
  auto result = lsp_position(location);
  const auto* file = get(location.file);
  if (file == nullptr) return result;
  const auto mapped = static_cast<std::int64_t>(result.line) + file->diagnostic_line_delta;
  result.line = static_cast<std::uint32_t>(std::max<std::int64_t>(0, mapped));
  return result;
}

void SourceManager::set_diagnostic_mapping(SourceFileId id, std::filesystem::path display_path,
                                           std::int64_t line_delta, std::int64_t byte_delta) {
  if (id == kInvalidSourceFileId || id > files_.size()) return;
  auto& file = files_[id - 1];
  file.diagnostic_path = std::move(display_path);
  file.diagnostic_line_delta = line_delta;
  file.diagnostic_byte_delta = byte_delta;
}

std::uint32_t SourceManager::display_byte_offset(SourceLocation location) const noexcept {
  const auto* file = get(location.file);
  if (file == nullptr) return location.offset;
  const auto mapped = static_cast<std::int64_t>(location.offset) + file->diagnostic_byte_delta;
  return static_cast<std::uint32_t>(std::max<std::int64_t>(0, mapped));
}

std::string SourceManager::display_name(SourceFileId id) const {
  const auto* file = get(id);
  if (file == nullptr) return "<unknown>";
  return (file->diagnostic_path.empty() ? file->path : file->diagnostic_path).string();
}

std::vector<std::uint32_t> SourceManager::compute_line_starts(
    std::string_view text) {
  std::vector<std::uint32_t> starts{0};
  for (std::uint32_t i = 0; i < text.size(); ++i) {
    if (text[i] == '\n') {
      starts.push_back(i + 1);
    }
  }
  return starts;
}

}  // namespace raz::compiler
