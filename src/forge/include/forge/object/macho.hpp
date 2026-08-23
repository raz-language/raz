// Copyright 2026 Mario Vinciguerra
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstddef>
#include <vector>

#include "forge/codegen/aarch64/encoder.hpp"
#include "forge/diagnostics/diagnostic.hpp"
#include "forge/machine/module.hpp"

namespace forge::object {

struct MachOObjectResult {
    std::vector<std::byte> bytes;
    Diagnostics diagnostics;
    [[nodiscard]] bool ok() const noexcept { return diagnostics.empty(); }
};

// Emit a relocatable Mach-O arm64 object suitable for Darwin/macOS linkers.
// Symbol names use Darwin's leading-underscore convention. Thread-local
// globals are represented with __thread_data + __thread_vars TLV descriptors.
[[nodiscard]] MachOObjectResult emit_macho64_aarch64(const machine::Module& module);
[[nodiscard]] MachOObjectResult emit_macho64_aarch64(codegen::aarch64::EncodedModuleImage image);

} // namespace forge::object
