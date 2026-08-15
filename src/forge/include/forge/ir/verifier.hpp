// Copyright 2026 Mario Vinciguerra
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include "forge/diagnostics/diagnostic.hpp"
#include "forge/ir/module.hpp"

namespace forge::ir {
[[nodiscard]] Diagnostics verify_module(const Module& module);
}
