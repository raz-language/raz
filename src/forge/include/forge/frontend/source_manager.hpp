// Copyright 2026 Mario Vinciguerra
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace forge::frontend {

using SourceId = std::uint32_t;

struct SourceSpan {
    SourceId source{};
    std::size_t begin{};
    std::size_t end{};
};

struct SourcePosition {
    std::size_t line{1};
    std::size_t column{1};
};

class SourceManager {
public:
    [[nodiscard]] SourceId add(std::string name, std::string text);
    [[nodiscard]] std::string_view name(SourceId source) const;
    [[nodiscard]] std::string_view text(SourceId source) const;
    [[nodiscard]] std::string_view slice(SourceSpan span) const;
    [[nodiscard]] SourcePosition position(SourceId source, std::size_t offset) const;
    [[nodiscard]] std::string_view line_text(SourceId source, std::size_t line) const;
    [[nodiscard]] std::optional<SourceId> find(std::string_view name) const;
    [[nodiscard]] std::size_t size() const noexcept { return sources_.size(); }

private:
    struct SourceFile {
        std::string name;
        std::string text;
        std::vector<std::size_t> line_offsets;
    };
    [[nodiscard]] const SourceFile& require(SourceId source) const;
    std::vector<SourceFile> sources_;
};

} // namespace forge::frontend
