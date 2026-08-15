// Copyright 2026 Mario Vinciguerra
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdint>

namespace raz::compiler {

using SourceFileId = std::uint32_t;
inline constexpr SourceFileId kInvalidSourceFileId = 0;

struct SourceLocation final {
  SourceFileId file = kInvalidSourceFileId;
  std::uint32_t offset = 0;

  [[nodiscard]] constexpr bool valid() const noexcept {
    return file != kInvalidSourceFileId;
  }

  friend constexpr bool operator==(SourceLocation, SourceLocation) = default;
};

struct SourceRange final {
  SourceLocation begin{};
  SourceLocation end{};

  [[nodiscard]] constexpr bool valid() const noexcept {
    return begin.valid() && end.valid() && begin.file == end.file &&
           begin.offset <= end.offset;
  }

  [[nodiscard]] constexpr std::uint32_t size() const noexcept {
    return valid() ? end.offset - begin.offset : 0;
  }
};

struct LineColumn final {
  std::uint32_t line = 1;
  std::uint32_t column = 1;
};

// LSP positions are zero-based and count UTF-16 code units, not UTF-8 bytes.
struct LspPosition final {
  std::uint32_t line = 0;
  std::uint32_t character = 0;
};

}  // namespace raz::compiler
