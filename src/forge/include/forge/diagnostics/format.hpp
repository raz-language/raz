// Copyright 2026 Mario Vinciguerra
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include <string>
#include <string_view>
#include "forge/diagnostics/diagnostic.hpp"

namespace forge::diagnostics {

struct RenderOptions {
    std::string_view file_name;
    std::string_view source;
    bool show_source{true};
};

[[nodiscard]] std::string_view severity_name(DiagnosticSeverity severity) noexcept;
[[nodiscard]] std::string render(const Diagnostic& diagnostic, RenderOptions options = {});
[[nodiscard]] std::string render_all(const Diagnostics& diagnostics, RenderOptions options = {});

} // namespace forge::diagnostics
