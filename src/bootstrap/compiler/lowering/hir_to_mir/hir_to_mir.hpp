// Copyright 2026 Mario Vinciguerra
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "compiler/ir/hir/hir.hpp"
#include "compiler/ir/mir/mir.hpp"

namespace raz::compiler {

class DiagnosticEngine;

class HirToMirLowering final {
 public:
  explicit HirToMirLowering(DiagnosticEngine& diagnostics);
  [[nodiscard]] MirModule lower(const HirModule& hir);

 private:
  DiagnosticEngine& diagnostics_;
};

}  // namespace raz::compiler
