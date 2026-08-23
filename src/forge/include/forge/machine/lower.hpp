// Copyright 2026 Mario Vinciguerra
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdint>
#include <optional>
#include "forge/diagnostics/diagnostic.hpp"
#include "forge/ir/module.hpp"
#include "forge/machine/module.hpp"

namespace forge::machine {

enum class TargetArchitecture : std::uint8_t { host, x86_64, aarch64 };

struct LowerOptions {
    TargetArchitecture architecture{TargetArchitecture::host};
};

struct LowerResult {
    std::optional<Module> module;
    Diagnostics diagnostics;
    [[nodiscard]] bool ok() const noexcept { return module.has_value() && diagnostics.empty(); }
};

// Initial backend slice: straight-line i32 functions with i32 parameters,
// constants, copies, add/sub/mul, and one return operation.
[[nodiscard]] LowerResult lower_module(const ir::Module& module, LowerOptions options = {});

} // namespace forge::machine
