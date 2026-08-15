// Copyright 2026 Mario Vinciguerra
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include <string>
#include "forge/ir/module.hpp"

namespace forge::ir {
[[nodiscard]] std::string print_module(const Module& module);
}
