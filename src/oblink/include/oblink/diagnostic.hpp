// Copyright 2026 Mario Vinciguerra
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <string>
#include <utility>
#include <vector>

namespace oblink {

enum class DiagnosticLevel { warning, error };

struct Diagnostic {
  DiagnosticLevel level{DiagnosticLevel::error};
  std::string message;
};

using Diagnostics = std::vector<Diagnostic>;

inline void add_error(Diagnostics& diagnostics, std::string message) {
  diagnostics.push_back({DiagnosticLevel::error, std::move(message)});
}

} // namespace oblink
