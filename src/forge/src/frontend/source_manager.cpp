// Copyright 2026 Mario Vinciguerra
// SPDX-License-Identifier: Apache-2.0

#include "forge/frontend/source_manager.hpp"
#include <algorithm>
#include <stdexcept>

namespace forge::frontend {

SourceId SourceManager::add(std::string name, std::string text) {
    SourceFile file{std::move(name), std::move(text), {0}};
    for (std::size_t i = 0; i < file.text.size(); ++i)
        if (file.text[i] == '\n') file.line_offsets.push_back(i + 1);
    sources_.push_back(std::move(file));
    return static_cast<SourceId>(sources_.size() - 1);
}

const SourceManager::SourceFile& SourceManager::require(SourceId source) const {
    if (source >= sources_.size()) throw std::out_of_range("invalid Forge source id");
    return sources_[source];
}

std::string_view SourceManager::name(SourceId source) const { return require(source).name; }
std::string_view SourceManager::text(SourceId source) const { return require(source).text; }

std::string_view SourceManager::slice(SourceSpan span) const {
    const auto& file = require(span.source);
    if (span.begin > span.end || span.end > file.text.size()) throw std::out_of_range("invalid Forge source span");
    return std::string_view(file.text).substr(span.begin, span.end - span.begin);
}

SourcePosition SourceManager::position(SourceId source, std::size_t offset) const {
    const auto& file = require(source);
    if (offset > file.text.size()) throw std::out_of_range("invalid Forge source offset");
    const auto upper = std::upper_bound(file.line_offsets.begin(), file.line_offsets.end(), offset);
    const auto line_index = static_cast<std::size_t>(std::distance(file.line_offsets.begin(), upper) - 1);
    return {line_index + 1, offset - file.line_offsets[line_index] + 1};
}

std::string_view SourceManager::line_text(SourceId source, std::size_t line) const {
    const auto& file = require(source);
    if (line == 0 || line > file.line_offsets.size()) return {};
    const auto begin = file.line_offsets[line - 1];
    auto end = line < file.line_offsets.size() ? file.line_offsets[line] : file.text.size();
    if (end > begin && file.text[end - 1] == '\n') --end;
    if (end > begin && file.text[end - 1] == '\r') --end;
    return std::string_view(file.text).substr(begin, end - begin);
}

std::optional<SourceId> SourceManager::find(std::string_view requested) const {
    for (std::size_t i = 0; i < sources_.size(); ++i)
        if (sources_[i].name == requested) return static_cast<SourceId>(i);
    return std::nullopt;
}

} // namespace forge::frontend
