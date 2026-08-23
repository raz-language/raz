// Copyright 2026 Mario Vinciguerra
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "forge/diagnostics/diagnostic.hpp"
#include "forge/machine/module.hpp"

namespace forge::codegen::aarch64 {

enum class Abi : std::uint8_t { aapcs64, darwin };

enum class RelocationKind : std::uint8_t {
    call26,
    adr_prel_pg_hi21,
    add_abs_lo12_nc,
    tlsie_adr_gottprel_page21,
    tlsie_ld64_gottprel_lo12_nc,
    tlvp_load_page21,
    tlvp_load_pageoff12,
};

struct Relocation {
    std::size_t offset{};
    std::string symbol;
    RelocationKind kind{};
    std::int64_t addend{};
};

struct EncodedFunction {
    std::string name;
    std::vector<std::byte> code;
    std::vector<Relocation> relocations;
    std::uint32_t frame_size{};
    std::uint32_t stack_homed_value_count{}; // compatibility alias for spilled_value_count
    std::uint32_t register_allocated_value_count{};
    std::uint32_t vector_register_allocated_value_count{};
    std::uint32_t spilled_value_count{};
    std::uint32_t vector_spilled_value_count{};
    std::uint32_t spill_bytes{};
    std::uint32_t frame_bytes_saved{};
    std::uint32_t callee_saved_register_count{};
    std::uint32_t immediate_form_count{};
    std::uint32_t elided_dead_constant_count{};
    std::uint32_t neon_vector_operation_count{};
    std::uint32_t abi_register_argument_count{};
    std::uint32_t abi_stack_argument_count{};
};

enum class DataSection : std::uint8_t { read_only, writable, tls };

struct EncodedGlobal {
    std::string name;
    DataSection section{};
    std::size_t data_offset{};
    bool is_internal{};
};

struct EncodedModuleImage {
    std::vector<std::byte> code;
    std::vector<std::byte> read_only_data;
    std::vector<std::byte> writable_data;
    std::vector<std::byte> thread_local_data;
    std::vector<std::pair<std::string, std::size_t>> entries;
    std::vector<EncodedGlobal> globals;
    std::vector<std::string> external_globals;
    std::vector<std::string> external_tls;
    std::vector<Relocation> relocations;
};

struct EncodeResult {
    std::vector<EncodedFunction> functions;
    Diagnostics diagnostics;
    [[nodiscard]] bool ok() const noexcept { return diagnostics.empty(); }
};

struct ImageEncodeResult {
    EncodedModuleImage image;
    std::vector<EncodedFunction> functions;
    Diagnostics diagnostics;
    [[nodiscard]] bool ok() const noexcept { return diagnostics.empty(); }
};

[[nodiscard]] EncodeResult encode(const machine::Module& module, Abi abi = Abi::aapcs64);
[[nodiscard]] ImageEncodeResult encode_image(const machine::Module& module, Abi abi = Abi::aapcs64);
[[nodiscard]] std::string format_hex(const std::vector<std::byte>& code);

} // namespace forge::codegen::aarch64
