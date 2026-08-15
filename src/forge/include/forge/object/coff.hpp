// Copyright 2026 Mario Vinciguerra
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstddef>
#include <vector>

#include "forge/codegen/x86_64/encoder.hpp"
#include "forge/object/elf.hpp"
#include "forge/diagnostics/diagnostic.hpp"
#include "forge/machine/module.hpp"

namespace forge::object {

struct CoffObjectResult {
    std::vector<std::byte> bytes;
    Diagnostics diagnostics;
    ObjectEmissionStats stats;
    [[nodiscard]] bool ok() const noexcept { return diagnostics.empty(); }
};

// Emits a Microsoft COFF AMD64 relocatable object containing .text, .rdata,
// .data, a symbol/string table, and REL32 relocations for functions/globals.
[[nodiscard]] CoffObjectResult emit_coff_x86_64(
    const machine::Module& module,
    codegen::x86_64::Abi abi = codegen::x86_64::Abi::windows);
[[nodiscard]] CoffObjectResult emit_coff_x86_64(
    codegen::x86_64::EncodedModuleImage image);

} // namespace forge::object
