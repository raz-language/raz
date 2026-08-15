// Copyright 2026 Mario Vinciguerra
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "compiler/syntax/syntax_tree.hpp"

namespace raz::compiler {
class DiagnosticEngine;
[[nodiscard]] SyntaxTree lower_namespaces(const SyntaxTree& tree, DiagnosticEngine& diagnostics);
}
