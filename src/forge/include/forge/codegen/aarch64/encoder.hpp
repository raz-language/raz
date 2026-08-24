// Copyright 2026 Mario Vinciguerra
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
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
    std::uint32_t abi_outgoing_stack_bytes{};
};

enum class DataSection : std::uint8_t { read_only, writable, tls };

struct EncodedGlobal {
    std::string name;
    DataSection section{};
    std::size_t data_offset{};
    bool is_internal{};
};

struct JitExternalGlobalSlot {
    std::string symbol;
    std::size_t data_offset{};
};

// JIT-only TLS thunk metadata. The descriptor address is an opaque runtime
// pointer supplied by forge::jit; generated code passes it to the runtime
// helper every time tls.address executes so the result is resolved for the
// current host thread rather than frozen to the thread that loaded the JIT.
struct JitTlsThunk {
    std::string symbol;
    std::size_t code_offset{};
    std::uintptr_t descriptor_address{};
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
    // AArch64 JIT-only indirection table. Each slot stores the absolute host
    // address of an external global in read_only_data so generated code can
    // address the nearby slot with ADRP and load the unrestricted 64-bit
    // target with LDR. Object emission leaves this empty.
    std::vector<JitExternalGlobalSlot> jit_external_global_slots;
    std::vector<JitTlsThunk> jit_tls_thunks;
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

using ExternalResolver = std::function<std::optional<std::uintptr_t>(std::string_view)>;

[[nodiscard]] EncodeResult encode(const machine::Module& module, Abi abi = Abi::aapcs64);
[[nodiscard]] ImageEncodeResult encode_image(const machine::Module& module, Abi abi = Abi::aapcs64);
// Materialize absolute host addresses for external globals into a local JIT
// pointer table. This makes external-global references independent of ADRP
// range while keeping ordinary object-file global addressing unchanged.
[[nodiscard]] Diagnostics materialize_jit_external_globals(
    EncodedModuleImage& image,
    const ExternalResolver& global_resolver);

// Resolve an encoded AArch64 image for direct in-process execution. Internal
// functions and globals are resolved from the supplied image bases; external
// functions/globals are supplied by the host resolvers. TLS references must
// first be rewritten with materialize_jit_tls() so each access resolves against
// the calling host thread.

// Rewrite native object-file TLS sequences into JIT-local thunks. Each thunk
// materializes an opaque per-symbol descriptor in x0 and tail-branches to the
// supplied runtime helper. The original tls.address site becomes BL thunk plus
// a result move/NOP padding, preserving the fixed four-instruction footprint
// used by both Linux initial-exec and Darwin TLV lowering. This operation is
// transactional: malformed pairs or missing descriptors leave the image
// unchanged.
using JitTlsDescriptorResolver = std::function<std::optional<std::uintptr_t>(std::string_view)>;
[[nodiscard]] Diagnostics materialize_jit_tls(
    EncodedModuleImage& image,
    const JitTlsDescriptorResolver& descriptor_resolver,
    std::uintptr_t helper_address);

[[nodiscard]] Diagnostics resolve_jit_relocations(
    EncodedModuleImage& image,
    std::uintptr_t code_address,
    std::uintptr_t read_only_data_address,
    std::uintptr_t writable_data_address,
    const ExternalResolver& resolver = {},
    const ExternalResolver& global_resolver = {});
[[nodiscard]] std::string format_hex(const std::vector<std::byte>& code);

} // namespace forge::codegen::aarch64
