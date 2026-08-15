// Copyright 2026 Mario Vinciguerra
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include <string>
#include "forge/ir/module.hpp"

namespace forge::ir {
[[nodiscard]] std::string build_source_map_json(const Module& module);
}
