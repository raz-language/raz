// Copyright 2026 Mario Vinciguerra
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstddef>
#include <vector>

#include "forge/codegen/x86_64/encoder.hpp"
#include "forge/diagnostics/diagnostic.hpp"
#include "forge/machine/module.hpp"

namespace forge::object {

struct ObjectEmissionStats {
    std::size_t section_count{};
    std::size_t symbol_count{};
    std::size_t relocation_count{};
    std::size_t external_symbol_count{};
};

struct ElfObjectResult {
    std::vector<std::byte> bytes;
    Diagnostics diagnostics;
    ObjectEmissionStats stats;
    [[nodiscard]] bool ok() const noexcept { return diagnostics.empty(); }
};

// Emits an ELF64 little-endian x86-64 relocatable object. The object contains
// .text, .rodata, .data, a symbol table, and RELA relocations for unresolved
// function/global addresses and internal data references.
[[nodiscard]] ElfObjectResult emit_elf64_x86_64(
    const machine::Module& module,
    codegen::x86_64::Abi abi = codegen::x86_64::Abi::system_v);
[[nodiscard]] ElfObjectResult emit_elf64_x86_64(
    codegen::x86_64::EncodedModuleImage image);

} // namespace forge::object
