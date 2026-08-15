// Copyright 2026 Mario Vinciguerra
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "compiler/ir/mir/mir.hpp"

#include <string>

namespace raz::compiler {

class DiagnosticEngine;

class ForgeLowering final {
 public:
  explicit ForgeLowering(DiagnosticEngine& diagnostics);
  [[nodiscard]] std::string lower_and_print(const MirModule& module);

 private:
  DiagnosticEngine& diagnostics_;
};

}  // namespace raz::compiler
