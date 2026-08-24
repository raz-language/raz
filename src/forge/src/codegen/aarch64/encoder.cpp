// Copyright 2026 Mario Vinciguerra
// SPDX-License-Identifier: Apache-2.0

#include "forge/codegen/aarch64/encoder.hpp"
#include "forge/codegen/aarch64/register_allocation.hpp"

#include "forge/target/aarch64_immediate.hpp"

#include "forge/machine/verifier.hpp"

#include <algorithm>
#include <array>
#include <iomanip>
#include <limits>
#include <optional>
#include <sstream>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

namespace forge::codegen::aarch64 {
namespace {

void add_error(Diagnostics& diagnostics, std::string message) {
    diagnostics.push_back({DiagnosticSeverity::error, std::move(message), {}});
}

constexpr std::uint32_t align16(std::uint32_t value) noexcept {
    return (value + 15U) & ~15U;
}

constexpr std::uint32_t align_to(std::uint32_t value, std::uint32_t alignment) noexcept {
    return alignment <= 1U ? value : (value + alignment - 1U) & ~(alignment - 1U);
}

class Buffer {
public:
    void word(std::uint32_t value) {
        for (unsigned shift = 0; shift < 32; shift += 8)
            bytes_.push_back(static_cast<std::byte>(static_cast<std::uint8_t>(value >> shift)));
    }

    void patch_word(std::size_t offset, std::uint32_t value) {
        for (unsigned shift = 0; shift < 32; shift += 8)
            bytes_.at(offset + shift / 8U) = static_cast<std::byte>(static_cast<std::uint8_t>(value >> shift));
    }

    [[nodiscard]] std::size_t size() const noexcept { return bytes_.size(); }
    [[nodiscard]] std::vector<std::byte> take() { return std::move(bytes_); }

private:
    std::vector<std::byte> bytes_;
};

constexpr std::uint8_t sp = 31U;
constexpr std::uint8_t fp = 29U;
constexpr std::uint8_t scratch0 = 9U;
constexpr std::uint8_t scratch1 = 10U;
constexpr std::uint8_t scratch2 = 11U;
constexpr std::uint8_t scratch3 = 12U;
constexpr std::uint8_t address_scratch = 15U;
constexpr std::uint8_t indirect_call_scratch = 16U;
constexpr std::uint8_t vector_scalar_scratch = 17U;

void emit_add_immediate(Buffer& out, std::uint8_t destination, std::uint8_t source,
                        std::uint32_t immediate, bool subtract = false) {
    out.word((subtract ? 0xD1000000U : 0x91000000U) |
             ((immediate & 0xFFFU) << 10U) |
             (static_cast<std::uint32_t>(source) << 5U) | destination);
}

void emit_copy_x(Buffer& out, std::uint8_t destination, std::uint8_t source) {
    if (destination == source) return;
    out.word(0xAA0003E0U | (static_cast<std::uint32_t>(source) << 16U) | destination);
}

void emit_copy_w(Buffer& out, std::uint8_t destination, std::uint8_t source) {
    if (destination == source) return;
    out.word(0x2A0003E0U | (static_cast<std::uint32_t>(source) << 16U) | destination);
}

void emit_address(Buffer& out, std::uint8_t destination, std::uint8_t base, std::int64_t displacement) {
    if (base == sp) emit_add_immediate(out, destination, sp, 0U);
    else emit_copy_x(out, destination, base);
    while (displacement != 0) {
        const auto magnitude = static_cast<std::uint64_t>(displacement < 0 ? -displacement : displacement);
        const auto chunk = static_cast<std::uint32_t>(std::min<std::uint64_t>(magnitude, 4095U));
        emit_add_immediate(out, destination, destination, chunk, displacement < 0);
        displacement += displacement < 0 ? static_cast<std::int64_t>(chunk) : -static_cast<std::int64_t>(chunk);
    }
}

void emit_mov_imm64(Buffer& out, std::uint8_t destination, std::uint64_t value) {
    out.word(0xD2800000U | (static_cast<std::uint32_t>(value & 0xFFFFU) << 5U) | destination);
    for (std::uint32_t half = 1; half < 4U; ++half) {
        const auto piece = static_cast<std::uint16_t>((value >> (half * 16U)) & 0xFFFFU);
        if (piece == 0U) continue;
        out.word(0xF2800000U | (half << 21U) | (static_cast<std::uint32_t>(piece) << 5U) | destination);
    }
}

std::uint32_t load_base(unsigned width) {
    switch (width) {
    case 1: return 0x39400000U; // ldrb w
    case 2: return 0x79400000U; // ldrh w
    case 4: return 0xB9400000U; // ldr w
    default: return 0xF9400000U; // ldr x
    }
}

std::uint32_t store_base(unsigned width) {
    switch (width) {
    case 1: return 0x39000000U; // strb w
    case 2: return 0x79000000U; // strh w
    case 4: return 0xB9000000U; // str w
    default: return 0xF9000000U; // str x
    }
}

std::uint32_t unscaled_load_base(unsigned width) {
    switch (width) {
    case 1: return 0x38400000U; // ldurb w
    case 2: return 0x78400000U; // ldurh w
    case 4: return 0xB8400000U; // ldur w
    default: return 0xF8400000U; // ldur x
    }
}

std::uint32_t unscaled_store_base(unsigned width) {
    switch (width) {
    case 1: return 0x38000000U; // sturb w
    case 2: return 0x78000000U; // sturh w
    case 4: return 0xB8000000U; // stur w
    default: return 0xF8000000U; // stur x
    }
}

bool emit_direct_integer_memory(Buffer& out, bool load, std::uint8_t value, std::uint8_t base,
                                std::int64_t displacement, unsigned width) {
    if (displacement >= -256 && displacement <= 255) {
        const auto imm9 = static_cast<std::uint32_t>(displacement) & 0x1FFU;
        out.word((load ? unscaled_load_base(width) : unscaled_store_base(width)) |
                 (imm9 << 12U) | (static_cast<std::uint32_t>(base) << 5U) | value);
        return true;
    }
    if (displacement >= 0 && width != 0U && (displacement % static_cast<std::int64_t>(width)) == 0) {
        const auto scaled = static_cast<std::uint64_t>(displacement) / width;
        if (scaled <= 4095U) {
            out.word((load ? load_base(width) : store_base(width)) |
                     (static_cast<std::uint32_t>(scaled) << 10U) |
                     (static_cast<std::uint32_t>(base) << 5U) | value);
            return true;
        }
    }
    return false;
}

void emit_load_integer(Buffer& out, std::uint8_t destination, std::uint8_t base,
                       std::int64_t displacement, unsigned width) {
    if (emit_direct_integer_memory(out, true, destination, base, displacement, width)) return;
    emit_address(out, address_scratch, base, displacement);
    out.word(load_base(width) | (static_cast<std::uint32_t>(address_scratch) << 5U) | destination);
}

void emit_store_integer(Buffer& out, std::uint8_t source, std::uint8_t base,
                        std::int64_t displacement, unsigned width) {
    if (emit_direct_integer_memory(out, false, source, base, displacement, width)) return;
    emit_address(out, address_scratch, base, displacement);
    out.word(store_base(width) | (static_cast<std::uint32_t>(address_scratch) << 5U) | source);
}

void emit_load_float(Buffer& out, std::uint8_t destination, std::uint8_t base,
                     std::int64_t displacement, bool wide) {
    emit_address(out, address_scratch, base, displacement);
    out.word((wide ? 0xFD400000U : 0xBD400000U) |
             (static_cast<std::uint32_t>(address_scratch) << 5U) | destination);
}

void emit_store_float(Buffer& out, std::uint8_t source, std::uint8_t base,
                      std::int64_t displacement, bool wide) {
    emit_address(out, address_scratch, base, displacement);
    out.word((wide ? 0xFD000000U : 0xBD000000U) |
             (static_cast<std::uint32_t>(address_scratch) << 5U) | source);
}

// AArch64 Advanced SIMD uses the same v0-v31 register file as scalar FP.
// These helpers intentionally start with 128-bit Q-register operations only;
// the shared packed machine pseudos are memory-to-memory, so the backend can
// vectorize them without exposing target-specific vector types in Forge IR.
void emit_load_neon128(Buffer& out, std::uint8_t destination, std::uint8_t base,
                       std::int64_t displacement) {
    if (displacement >= -256 && displacement <= 255) {
        const auto imm9 = static_cast<std::uint32_t>(displacement) & 0x1FFU;
        out.word(0x3CC00000U | (imm9 << 12U) |
                 (static_cast<std::uint32_t>(base) << 5U) | destination); // ldur q
        return;
    }
    if (displacement >= 0 && (displacement & 15) == 0) {
        const auto scaled = static_cast<std::uint64_t>(displacement) >> 4U;
        if (scaled <= 4095U) {
            out.word(0x3DC00000U | (static_cast<std::uint32_t>(scaled) << 10U) |
                     (static_cast<std::uint32_t>(base) << 5U) | destination);
            return;
        }
    }
    emit_address(out, address_scratch, base, displacement);
    out.word(0x3DC00000U | (static_cast<std::uint32_t>(address_scratch) << 5U) | destination);
}

void emit_store_neon128(Buffer& out, std::uint8_t source, std::uint8_t base,
                        std::int64_t displacement) {
    if (displacement >= -256 && displacement <= 255) {
        const auto imm9 = static_cast<std::uint32_t>(displacement) & 0x1FFU;
        out.word(0x3C800000U | (imm9 << 12U) |
                 (static_cast<std::uint32_t>(base) << 5U) | source); // stur q
        return;
    }
    if (displacement >= 0 && (displacement & 15) == 0) {
        const auto scaled = static_cast<std::uint64_t>(displacement) >> 4U;
        if (scaled <= 4095U) {
            out.word(0x3D800000U | (static_cast<std::uint32_t>(scaled) << 10U) |
                     (static_cast<std::uint32_t>(base) << 5U) | source);
            return;
        }
    }
    emit_address(out, address_scratch, base, displacement);
    out.word(0x3D800000U | (static_cast<std::uint32_t>(address_scratch) << 5U) | source);
}

std::uint16_t packed_token(const std::string& program, std::size_t offset) noexcept {
    return static_cast<std::uint16_t>(
        static_cast<unsigned char>(program[offset]) |
        (static_cast<unsigned>(static_cast<unsigned char>(program[offset + 1U])) << 8U));
}

void emit_load_neon64(Buffer& out, std::uint8_t destination, std::uint8_t base,
                      std::int64_t displacement) {
    if (displacement >= -256 && displacement <= 255) {
        const auto imm9 = static_cast<std::uint32_t>(displacement) & 0x1FFU;
        out.word(0xFC400000U | (imm9 << 12U) |
                 (static_cast<std::uint32_t>(base) << 5U) | destination); // ldur d
        return;
    }
    if (displacement >= 0 && (displacement & 7) == 0) {
        const auto scaled = static_cast<std::uint64_t>(displacement) >> 3U;
        if (scaled <= 4095U) {
            out.word(0xFD400000U | (static_cast<std::uint32_t>(scaled) << 10U) |
                     (static_cast<std::uint32_t>(base) << 5U) | destination); // ldr d
            return;
        }
    }
    emit_address(out, address_scratch, base, displacement);
    out.word(0xFD400000U | (static_cast<std::uint32_t>(address_scratch) << 5U) | destination);
}

void emit_store_neon64(Buffer& out, std::uint8_t source, std::uint8_t base,
                       std::int64_t displacement) {
    if (displacement >= -256 && displacement <= 255) {
        const auto imm9 = static_cast<std::uint32_t>(displacement) & 0x1FFU;
        out.word(0xFC000000U | (imm9 << 12U) |
                 (static_cast<std::uint32_t>(base) << 5U) | source); // stur d
        return;
    }
    if (displacement >= 0 && (displacement & 7) == 0) {
        const auto scaled = static_cast<std::uint64_t>(displacement) >> 3U;
        if (scaled <= 4095U) {
            out.word(0xFD000000U | (static_cast<std::uint32_t>(scaled) << 10U) |
                     (static_cast<std::uint32_t>(base) << 5U) | source); // str d
            return;
        }
    }
    emit_address(out, address_scratch, base, displacement);
    out.word(0xFD000000U | (static_cast<std::uint32_t>(address_scratch) << 5U) | source);
}

bool neon_integer_operation_supported(machine::Opcode opcode, bool wide) noexcept {
    if (wide)
        return opcode == machine::Opcode::add_i64 || opcode == machine::Opcode::sub_i64 ||
               opcode == machine::Opcode::and_i64 || opcode == machine::Opcode::or_i64 ||
               opcode == machine::Opcode::xor_i64;
    return opcode == machine::Opcode::add_i32 || opcode == machine::Opcode::sub_i32 ||
           opcode == machine::Opcode::and_i32 || opcode == machine::Opcode::or_i32 ||
           opcode == machine::Opcode::xor_i32;
}

bool neon_float_operation_supported(machine::Opcode opcode, bool wide) noexcept {
    if (wide)
        return opcode == machine::Opcode::add_f64 || opcode == machine::Opcode::sub_f64 ||
               opcode == machine::Opcode::mul_f64 || opcode == machine::Opcode::div_f64;
    return opcode == machine::Opcode::add_f32 || opcode == machine::Opcode::sub_f32 ||
           opcode == machine::Opcode::mul_f32 || opcode == machine::Opcode::div_f32;
}

bool neon_packed_operation_supported(machine::Opcode opcode, bool wide) noexcept {
    return neon_integer_operation_supported(opcode, wide) || neon_float_operation_supported(opcode, wide);
}

void emit_neon_integer_binary(Buffer& out, machine::Opcode opcode, std::uint8_t destination,
                              std::uint8_t left, std::uint8_t right, bool wide) {
    std::uint32_t base = 0U;
    switch (opcode) {
    case machine::Opcode::add_i32: case machine::Opcode::add_i64:
        base = wide ? 0x4EE08400U : 0x4EA08400U; break;
    case machine::Opcode::sub_i32: case machine::Opcode::sub_i64:
        base = wide ? 0x6EE08400U : 0x6EA08400U; break;
    case machine::Opcode::and_i32: case machine::Opcode::and_i64:
        base = 0x4E201C00U; break;
    case machine::Opcode::or_i32: case machine::Opcode::or_i64:
        base = 0x4EA01C00U; break;
    case machine::Opcode::xor_i32: case machine::Opcode::xor_i64:
        base = 0x6E201C00U; break;
    default: return;
    }
    out.word(base | (static_cast<std::uint32_t>(right) << 16U) |
             (static_cast<std::uint32_t>(left) << 5U) | destination);
}

void emit_neon_float_binary(Buffer& out, machine::Opcode opcode, std::uint8_t destination,
                            std::uint8_t left, std::uint8_t right, bool wide) {
    std::uint32_t base = 0U;
    switch (opcode) {
    case machine::Opcode::add_f32: case machine::Opcode::add_f64:
        base = wide ? 0x4E60D400U : 0x4E20D400U; break;
    case machine::Opcode::sub_f32: case machine::Opcode::sub_f64:
        base = wide ? 0x4EE0D400U : 0x4EA0D400U; break;
    case machine::Opcode::mul_f32: case machine::Opcode::mul_f64:
        base = wide ? 0x6E60DC00U : 0x6E20DC00U; break;
    case machine::Opcode::div_f32: case machine::Opcode::div_f64:
        base = wide ? 0x6E60FC00U : 0x6E20FC00U; break;
    default: return;
    }
    out.word(base | (static_cast<std::uint32_t>(right) << 16U) |
             (static_cast<std::uint32_t>(left) << 5U) | destination);
}

void emit_neon_packed_binary(Buffer& out, machine::Opcode opcode, std::uint8_t destination,
                             std::uint8_t left, std::uint8_t right, bool wide) {
    if (neon_float_operation_supported(opcode, wide))
        emit_neon_float_binary(out, opcode, destination, left, right, wide);
    else
        emit_neon_integer_binary(out, opcode, destination, left, right, wide);
}

void emit_neon_broadcast_integer(Buffer& out, std::uint8_t destination, std::uint8_t source, bool wide) {
    // dup vD.2d, xN / dup vD.4s, wN
    out.word((wide ? 0x4E080C00U : 0x4E040C00U) |
             (static_cast<std::uint32_t>(source) << 5U) | destination);
}

void emit_neon_broadcast_float(Buffer& out, std::uint8_t destination, std::uint8_t source, bool wide) {
    // dup vD.2d, vN.d[0] / dup vD.4s, vN.s[0]
    out.word((wide ? 0x4E080400U : 0x4E040400U) |
             (static_cast<std::uint32_t>(source) << 5U) | destination);
}

void emit_fmov_from_integer(Buffer& out, std::uint8_t destination, std::uint8_t source, bool wide) {
    out.word((wide ? 0x9E670000U : 0x1E270000U) |
             (static_cast<std::uint32_t>(source) << 5U) | destination);
}

void emit_fmov_to_integer(Buffer& out, std::uint8_t destination, std::uint8_t source, bool wide) {
    // fmov xD, dN / fmov wD, sN
    out.word((wide ? 0x9E660000U : 0x1E260000U) |
             (static_cast<std::uint32_t>(source) << 5U) | destination);
}

void emit_neon_reduce_add(Buffer& out, std::uint8_t destination, std::uint8_t source, bool wide) {
    // ADDP Dd,Vn.2D is the scalar horizontal add for two 64-bit lanes.
    // ADDV Sd,Vn.4S horizontally reduces all four 32-bit lanes.
    out.word((wide ? 0x5EF1B800U : 0x4EB1B800U) |
             (static_cast<std::uint32_t>(source) << 5U) | destination);
}

void emit_copy_float(Buffer& out, std::uint8_t destination, std::uint8_t source, bool wide) {
    if (destination == source) return;
    out.word((wide ? 0x1E604000U : 0x1E204000U) |
             (static_cast<std::uint32_t>(source) << 5U) | destination);
}


void emit_integer_binary(Buffer& out, machine::Opcode opcode, std::uint8_t destination,
                         std::uint8_t left, std::uint8_t right, bool wide) {
    std::uint32_t base = 0;
    switch (opcode) {
    case machine::Opcode::add_i32:
    case machine::Opcode::add_i64: base = wide ? 0x8B000000U : 0x0B000000U; break;
    case machine::Opcode::sub_i32:
    case machine::Opcode::sub_i64: base = wide ? 0xCB000000U : 0x4B000000U; break;
    case machine::Opcode::mul_i32:
    case machine::Opcode::mul_i64: base = wide ? 0x9B007C00U : 0x1B007C00U; break;
    case machine::Opcode::div_s_i32:
    case machine::Opcode::div_s_i64: base = wide ? 0x9AC00C00U : 0x1AC00C00U; break;
    case machine::Opcode::div_u_i32:
    case machine::Opcode::div_u_i64: base = wide ? 0x9AC00800U : 0x1AC00800U; break;
    case machine::Opcode::and_i32:
    case machine::Opcode::and_i64: base = wide ? 0x8A000000U : 0x0A000000U; break;
    case machine::Opcode::or_i32:
    case machine::Opcode::or_i64: base = wide ? 0xAA000000U : 0x2A000000U; break;
    case machine::Opcode::xor_i32:
    case machine::Opcode::xor_i64: base = wide ? 0xCA000000U : 0x4A000000U; break;
    case machine::Opcode::shl_i32:
    case machine::Opcode::shl_i64: base = wide ? 0x9AC02000U : 0x1AC02000U; break;
    case machine::Opcode::shr_s_i32:
    case machine::Opcode::shr_s_i64: base = wide ? 0x9AC02800U : 0x1AC02800U; break;
    case machine::Opcode::shr_u_i32:
    case machine::Opcode::shr_u_i64: base = wide ? 0x9AC02400U : 0x1AC02400U; break;
    default: break;
    }
    out.word(base | (static_cast<std::uint32_t>(right) << 16U) |
             (static_cast<std::uint32_t>(left) << 5U) | destination);
}

std::uint8_t integer_condition(machine::Opcode opcode) noexcept;
void emit_cset(Buffer& out, std::uint8_t destination, std::uint8_t condition);

bool encode_add_sub_immediate(std::int64_t value, std::uint32_t& imm12, bool& shift12) noexcept {
    if (value < 0) return false;
    const auto immediate = static_cast<std::uint64_t>(value);
    if (immediate <= 4095U) {
        imm12 = static_cast<std::uint32_t>(immediate);
        shift12 = false;
        return true;
    }
    if ((immediate & 4095U) != 0U || (immediate >> 12U) > 4095U) return false;
    imm12 = static_cast<std::uint32_t>(immediate >> 12U);
    shift12 = true;
    return true;
}

bool emit_integer_immediate(Buffer& out, machine::Opcode opcode, std::uint8_t destination,
                            std::uint8_t source, std::int64_t immediate, bool wide) {
    if (opcode == machine::Opcode::add_i32 || opcode == machine::Opcode::add_i64 ||
        opcode == machine::Opcode::sub_i32 || opcode == machine::Opcode::sub_i64) {
        std::uint32_t imm12 = 0U;
        bool shift12 = false;
        if (!encode_add_sub_immediate(immediate, imm12, shift12)) return false;
        const bool subtract = opcode == machine::Opcode::sub_i32 || opcode == machine::Opcode::sub_i64;
        const auto base = wide ? (subtract ? 0xD1000000U : 0x91000000U)
                               : (subtract ? 0x51000000U : 0x11000000U);
        out.word(base | (shift12 ? 0x00400000U : 0U) | (imm12 << 10U) |
                 (static_cast<std::uint32_t>(source) << 5U) | destination);
        return true;
    }

    if (opcode == machine::Opcode::and_i32 || opcode == machine::Opcode::and_i64 ||
        opcode == machine::Opcode::or_i32 || opcode == machine::Opcode::or_i64 ||
        opcode == machine::Opcode::xor_i32 || opcode == machine::Opcode::xor_i64) {
        const auto encoded = forge::target::encode_aarch64_logical_immediate(
            static_cast<std::uint64_t>(immediate), wide ? 64U : 32U);
        if (!encoded) return false;
        std::uint32_t base = wide ? 0x92000000U : 0x12000000U;
        if (opcode == machine::Opcode::or_i32 || opcode == machine::Opcode::or_i64)
            base = wide ? 0xB2000000U : 0x32000000U;
        else if (opcode == machine::Opcode::xor_i32 || opcode == machine::Opcode::xor_i64)
            base = wide ? 0xD2000000U : 0x52000000U;
        out.word(base | (static_cast<std::uint32_t>(encoded->n) << 22U) |
                 (static_cast<std::uint32_t>(encoded->immr) << 16U) |
                 (static_cast<std::uint32_t>(encoded->imms) << 10U) |
                 (static_cast<std::uint32_t>(source) << 5U) | destination);
        return true;
    }

    const auto width = wide ? 64U : 32U;
    if (immediate < 0 || static_cast<std::uint64_t>(immediate) >= width) return false;
    const auto shift = static_cast<std::uint32_t>(immediate);
    const auto source_bits = static_cast<std::uint32_t>(source) << 5U;
    if (opcode == machine::Opcode::shl_i32 || opcode == machine::Opcode::shl_i64) {
        const auto immr = (width - shift) & (width - 1U);
        const auto imms = width - 1U - shift;
        out.word((wide ? 0xD3400000U : 0x53000000U) | (immr << 16U) | (imms << 10U) |
                 source_bits | destination);
        return true;
    }
    if (opcode == machine::Opcode::shr_u_i32 || opcode == machine::Opcode::shr_u_i64) {
        out.word((wide ? 0xD3400000U : 0x53000000U) | (shift << 16U) | ((width - 1U) << 10U) |
                 source_bits | destination);
        return true;
    }
    if (opcode == machine::Opcode::shr_s_i32 || opcode == machine::Opcode::shr_s_i64) {
        out.word((wide ? 0x93400000U : 0x13000000U) | (shift << 16U) | ((width - 1U) << 10U) |
                 source_bits | destination);
        return true;
    }
    return false;
}

bool emit_integer_compare_immediate(Buffer& out, machine::Opcode opcode, std::uint8_t destination,
                                    std::uint8_t source, std::int64_t immediate, bool wide) {
    std::uint32_t imm12 = 0U;
    bool shift12 = false;
    if (!encode_add_sub_immediate(immediate, imm12, shift12)) return false;
    out.word((wide ? 0xF100001FU : 0x7100001FU) | (shift12 ? 0x00400000U : 0U) |
             (imm12 << 10U) | (static_cast<std::uint32_t>(source) << 5U));
    emit_cset(out, destination, integer_condition(opcode));
    return true;
}

std::uint8_t integer_condition(machine::Opcode opcode) noexcept {
    switch (opcode) {
    case machine::Opcode::cmp_eq_i32: case machine::Opcode::cmp_eq_i64: return 0x0U;
    case machine::Opcode::cmp_ne_i32: case machine::Opcode::cmp_ne_i64: return 0x1U;
    case machine::Opcode::cmp_lt_i32: case machine::Opcode::cmp_lt_i64: return 0xBU;
    case machine::Opcode::cmp_le_i32: case machine::Opcode::cmp_le_i64: return 0xDU;
    case machine::Opcode::cmp_gt_i32: case machine::Opcode::cmp_gt_i64: return 0xCU;
    case machine::Opcode::cmp_ge_i32: case machine::Opcode::cmp_ge_i64: return 0xAU;
    case machine::Opcode::cmp_ult_i32: case machine::Opcode::cmp_ult_i64: return 0x3U;
    case machine::Opcode::cmp_ule_i32: case machine::Opcode::cmp_ule_i64: return 0x9U;
    case machine::Opcode::cmp_ugt_i32: case machine::Opcode::cmp_ugt_i64: return 0x8U;
    case machine::Opcode::cmp_uge_i32: case machine::Opcode::cmp_uge_i64: return 0x2U;
    default: return 0xEU;
    }
}

std::uint8_t floating_condition(machine::Opcode opcode) noexcept {
    switch (opcode) {
    case machine::Opcode::cmp_eq_f32: case machine::Opcode::cmp_eq_f64: return 0x0U;
    case machine::Opcode::cmp_ne_f32: case machine::Opcode::cmp_ne_f64: return 0x1U;
    // MI and LS are the ordered AArch64 floating-point forms for < and <=;
    // unordered comparisons do not satisfy either condition.
    case machine::Opcode::cmp_lt_f32: case machine::Opcode::cmp_lt_f64: return 0x4U;
    case machine::Opcode::cmp_le_f32: case machine::Opcode::cmp_le_f64: return 0x9U;
    case machine::Opcode::cmp_gt_f32: case machine::Opcode::cmp_gt_f64: return 0xCU;
    case machine::Opcode::cmp_ge_f32: case machine::Opcode::cmp_ge_f64: return 0xAU;
    default: return 0xEU;
    }
}

void emit_cset(Buffer& out, std::uint8_t destination, std::uint8_t condition) {
    out.word(0x9A9F07E0U | (static_cast<std::uint32_t>(condition ^ 1U) << 12U) | destination);
}

void emit_integer_compare(Buffer& out, machine::Opcode opcode, std::uint8_t destination,
                          std::uint8_t left, std::uint8_t right, bool wide) {
    out.word((wide ? 0xEB00001FU : 0x6B00001FU) |
             (static_cast<std::uint32_t>(right) << 16U) |
             (static_cast<std::uint32_t>(left) << 5U));
    emit_cset(out, destination, integer_condition(opcode));
}

void emit_float_binary(Buffer& out, machine::Opcode opcode, std::uint8_t destination,
                       std::uint8_t left, std::uint8_t right, bool wide) {
    std::uint32_t base = 0;
    switch (opcode) {
    case machine::Opcode::add_f32: case machine::Opcode::add_f64: base = wide ? 0x1E602800U : 0x1E202800U; break;
    case machine::Opcode::sub_f32: case machine::Opcode::sub_f64: base = wide ? 0x1E603800U : 0x1E203800U; break;
    case machine::Opcode::mul_f32: case machine::Opcode::mul_f64: base = wide ? 0x1E600800U : 0x1E200800U; break;
    case machine::Opcode::div_f32: case machine::Opcode::div_f64: base = wide ? 0x1E601800U : 0x1E201800U; break;
    default: break;
    }
    out.word(base | (static_cast<std::uint32_t>(right) << 16U) |
             (static_cast<std::uint32_t>(left) << 5U) | destination);
}

void emit_float_compare(Buffer& out, machine::Opcode opcode, std::uint8_t destination,
                        std::uint8_t left, std::uint8_t right, bool wide) {
    out.word((wide ? 0x1E602000U : 0x1E202000U) |
             (static_cast<std::uint32_t>(right) << 16U) |
             (static_cast<std::uint32_t>(left) << 5U));
    emit_cset(out, destination, floating_condition(opcode));
}

void emit_sign_extend(Buffer& out, std::uint8_t destination, std::uint8_t source, unsigned source_bits) {
    const auto imms = source_bits == 0U ? 0U : std::min(source_bits, 64U) - 1U;
    out.word(0x93400000U | (static_cast<std::uint32_t>(imms) << 10U) |
             (static_cast<std::uint32_t>(source) << 5U) | destination);
}

void emit_zero_extend(Buffer& out, std::uint8_t destination, std::uint8_t source, unsigned source_bits) {
    if (source_bits >= 64U) { emit_copy_x(out, destination, source); return; }
    if (source_bits == 32U) { emit_copy_w(out, destination, source); return; }
    const auto mask = source_bits == 0U ? 0U : ((std::uint64_t{1} << source_bits) - 1U);
    emit_mov_imm64(out, scratch2, mask);
    out.word(0x8A000000U | (static_cast<std::uint32_t>(scratch2) << 16U) |
             (static_cast<std::uint32_t>(source) << 5U) | destination);
}

struct ArgumentLocation {
    enum class Kind : std::uint8_t { integer_register, floating_register, stack, indirect_result } kind{};
    std::uint8_t index{};
    std::uint32_t stack_offset{};
};

std::vector<ArgumentLocation> function_argument_locations(const machine::Function& function, Abi abi) {
    std::vector<ArgumentLocation> result(function.argument_count);
    std::uint8_t integer_index = 0;
    std::uint8_t floating_index = 0;
    std::uint32_t stack_offset = 0;
    const auto slot_width = [&](std::size_t index) -> std::uint32_t {
        if (index < function.argument_classes.size() &&
            function.argument_classes[index] == machine::RegisterClass::vector) return 16U;
        return index < function.argument_widths.size() && function.argument_widths[index] != 0U
            ? function.argument_widths[index] : 8U;
    };
    for (std::size_t index = 0; index < function.argument_count;) {
        if (function.indirect_result_parameter && index == 0U) {
            result[index] = {ArgumentLocation::Kind::indirect_result, 8U, 0U};
            ++index;
            continue;
        }
        auto group = index < function.argument_group_sizes.size() && function.argument_group_sizes[index] != 0U
            ? static_cast<std::size_t>(function.argument_group_sizes[index]) : std::size_t{1};
        group = std::min(group, static_cast<std::size_t>(function.argument_count) - index);
        bool all_simd = true;
        bool vector = false;
        std::uint32_t group_bytes = 0U;
        for (std::size_t piece = 0; piece < group; ++piece) {
            const auto slot = index + piece;
            const auto register_class = slot < function.argument_classes.size()
                ? function.argument_classes[slot] : machine::RegisterClass::integer;
            all_simd = all_simd && (register_class == machine::RegisterClass::floating ||
                                    register_class == machine::RegisterClass::vector);
            vector = vector || register_class == machine::RegisterClass::vector;
            group_bytes += slot_width(slot);
        }
        const auto natural_alignment = index < function.argument_group_alignments.size() &&
            function.argument_group_alignments[index] != 0U
            ? static_cast<std::uint32_t>(function.argument_group_alignments[index]) : 8U;
        if (!all_simd && abi == Abi::aapcs64 && natural_alignment >= 16U && integer_index < 8U)
            integer_index = static_cast<std::uint8_t>((integer_index + 1U) & ~1U);
        const bool registers_fit = all_simd
            ? static_cast<std::size_t>(floating_index) + group <= 8U
            : static_cast<std::size_t>(integer_index) + group <= 8U;
        if (registers_fit) {
            for (std::size_t piece = 0; piece < group; ++piece) {
                result[index + piece] = all_simd
                    ? ArgumentLocation{ArgumentLocation::Kind::floating_register, floating_index++, 0U}
                    : ArgumentLocation{ArgumentLocation::Kind::integer_register, integer_index++, 0U};
            }
        } else {
            // AAPCS64 C.3/C.13: once an HFA/HVA or small composite cannot fit,
            // exhaust that register bank and place the whole argument on the
            // stack. The pieces remain contiguous in their in-memory layout.
            if (all_simd) floating_index = 8U;
            else integer_index = 8U;
            const auto stack_alignment = abi == Abi::darwin ? natural_alignment
                : std::max<std::uint32_t>(8U, natural_alignment);
            stack_offset = align_to(stack_offset, stack_alignment);
            std::uint32_t piece_offset = 0U;
            for (std::size_t piece = 0; piece < group; ++piece) {
                result[index + piece] = {ArgumentLocation::Kind::stack, 0U, stack_offset + piece_offset};
                piece_offset += slot_width(index + piece);
            }
            stack_offset += abi == Abi::darwin ? std::max(group_bytes, 1U)
                                                : align_to(std::max(group_bytes, 1U), 8U);
        }
        (void)vector;
        index += group;
    }
    return result;
}

bool is_call(machine::Opcode opcode) noexcept {
    switch (opcode) {
    case machine::Opcode::call_i32: case machine::Opcode::call_i64:
    case machine::Opcode::call_f32: case machine::Opcode::call_f64:
    case machine::Opcode::call_void: case machine::Opcode::call_aggregate:
    case machine::Opcode::call_indirect_i32: case machine::Opcode::call_indirect_i64:
    case machine::Opcode::call_indirect_f32: case machine::Opcode::call_indirect_f64:
    case machine::Opcode::call_indirect_void: return true;
    default: return false;
    }
}

bool is_indirect_call(machine::Opcode opcode) noexcept {
    return opcode == machine::Opcode::call_indirect_i32 || opcode == machine::Opcode::call_indirect_i64 ||
           opcode == machine::Opcode::call_indirect_f32 || opcode == machine::Opcode::call_indirect_f64 ||
           opcode == machine::Opcode::call_indirect_void;
}

std::size_t call_argument_begin(const machine::Instruction& instruction) noexcept {
    std::size_t begin = is_indirect_call(instruction.opcode) ? 1U : 0U;
    if (instruction.indirect_result || instruction.opcode == machine::Opcode::call_aggregate) ++begin;
    return begin;
}

std::uint32_t call_stack_bytes(const machine::Function& function, const machine::Instruction& instruction, Abi abi) {
    std::uint8_t integer_index = 0;
    std::uint8_t simd_index = 0;
    std::uint32_t stack = 0;
    const auto argument_begin = call_argument_begin(instruction);
    const auto value_width = [&](std::size_t input_index) -> std::uint32_t {
        if (input_index < instruction.argument_widths.size() && instruction.argument_widths[input_index] != 0U)
            return instruction.argument_widths[input_index];
        const auto reg = instruction.inputs[input_index];
        if (reg < function.register_classes.size() &&
            function.register_classes[reg] == machine::RegisterClass::vector) return 16U;
        return reg < function.register_widths.size() && function.register_widths[reg] != 0U
            ? function.register_widths[reg] : 8U;
    };
    for (std::size_t index = argument_begin; index < instruction.inputs.size();) {
        auto group = index < instruction.argument_group_sizes.size() && instruction.argument_group_sizes[index] != 0U
            ? static_cast<std::size_t>(instruction.argument_group_sizes[index]) : std::size_t{1};
        group = std::min(group, instruction.inputs.size() - index);
        bool all_simd = true;
        std::uint32_t group_bytes = 0U;
        for (std::size_t piece = 0; piece < group; ++piece) {
            const auto reg = instruction.inputs[index + piece];
            const auto register_class = reg < function.register_classes.size()
                ? function.register_classes[reg] : machine::RegisterClass::integer;
            all_simd = all_simd && (register_class == machine::RegisterClass::floating ||
                                    register_class == machine::RegisterClass::vector);
            group_bytes += value_width(index + piece);
        }
        const auto natural_alignment = index < instruction.argument_group_alignments.size() &&
            instruction.argument_group_alignments[index] != 0U
            ? static_cast<std::uint32_t>(instruction.argument_group_alignments[index]) : 8U;
        if (!all_simd && abi == Abi::aapcs64 && natural_alignment >= 16U && integer_index < 8U)
            integer_index = static_cast<std::uint8_t>((integer_index + 1U) & ~1U);
        const bool darwin_variadic_tail = abi == Abi::darwin && instruction.variadic_call &&
            index - argument_begin >= instruction.variadic_named_input_count;
        const bool registers_fit = !darwin_variadic_tail && (all_simd
            ? static_cast<std::size_t>(simd_index) + group <= 8U
            : static_cast<std::size_t>(integer_index) + group <= 8U);
        if (registers_fit) {
            if (all_simd) simd_index = static_cast<std::uint8_t>(simd_index + group);
            else integer_index = static_cast<std::uint8_t>(integer_index + group);
        } else {
            // Generic AAPCS64 exhausts a register bank when an aggregate/HFA
            // cannot fit. Darwin's anonymous variadic tail is different: the
            // stack placement is a platform rule, not register exhaustion.
            if (!darwin_variadic_tail) {
                if (all_simd) simd_index = 8U;
                else integer_index = 8U;
            }
            const auto stack_alignment = darwin_variadic_tail
                ? std::max<std::uint32_t>(8U, natural_alignment)
                : abi == Abi::darwin ? natural_alignment : std::max<std::uint32_t>(8U, natural_alignment);
            stack = align_to(stack, stack_alignment);
            stack += abi == Abi::darwin && !darwin_variadic_tail
                ? std::max(group_bytes, 1U)
                : align_to(std::max(group_bytes, 1U), 8U);
        }
        index += group;
    }
    return align16(stack);
}

struct BranchFixup {
    std::size_t offset{};
    std::string target;
};

bool patch_branch26(Buffer& out, std::size_t offset, std::size_t target) {
    if ((offset & 3U) != 0U || (target & 3U) != 0U) return false;
    const auto delta = static_cast<std::int64_t>(target) - static_cast<std::int64_t>(offset);
    if ((delta & 3) != 0) return false;
    const auto words = delta / 4;
    if (words < -(std::int64_t{1} << 25) || words >= (std::int64_t{1} << 25)) return false;
    out.patch_word(offset, 0x14000000U | (static_cast<std::uint32_t>(words) & 0x03FFFFFFU));
    return true;
}

bool patch_cbz19(Buffer& out, std::size_t offset, std::size_t target, std::uint8_t reg) {
    const auto delta = static_cast<std::int64_t>(target) - static_cast<std::int64_t>(offset);
    if ((delta & 3) != 0) return false;
    const auto words = delta / 4;
    if (words < -(std::int64_t{1} << 18) || words >= (std::int64_t{1} << 18)) return false;
    out.patch_word(offset, 0x34000000U | ((static_cast<std::uint32_t>(words) & 0x7FFFFU) << 5U) | reg);
    return true;
}

bool unsupported_vector_opcode(machine::Opcode opcode) noexcept {
    // Wider logical vectors still have no single-register representation, but
    // stack transfers are decomposed into deterministic Q-register chunks by
    // the encoder. Keep this gate for genuinely unsupported vector opcodes.
    (void)opcode;
    return false;
}

EncodedFunction encode_function(const machine::Function& function, Abi abi, Diagnostics& diagnostics) {
    EncodedFunction encoded;
    encoded.name = function.name;
    if (function.target_feature == "sse2" || function.target_feature == "avx2" || function.target_feature == "avx512") {
        add_error(diagnostics, "AArch64 cannot encode x86 target feature on @" + function.name);
        return encoded;
    }

    std::uint32_t max_outgoing = 0;
    std::uint32_t max_edge_arguments = 0;
    for (const auto& block : function.blocks) {
        max_edge_arguments = std::max(max_edge_arguments, static_cast<std::uint32_t>(block.parameters.size()));
        for (const auto& instruction : block.instructions) {
            if (is_call(instruction.opcode)) max_outgoing = std::max(max_outgoing, call_stack_bytes(function, instruction, abi));
            for (const auto& successor : instruction.successors)
                max_edge_arguments = std::max(max_edge_arguments, static_cast<std::uint32_t>(successor.arguments.size()));
            if (unsupported_vector_opcode(instruction.opcode)) {
                add_error(diagnostics, "AArch64 NEON lowering is not yet available for machine opcode " +
                    std::string(machine::opcode_name(instruction.opcode)) + " in @" + function.name);
                return encoded;
            }
        }
    }

    const auto allocation = allocate_registers(function);
    if (!allocation.ok()) {
        diagnostics.insert(diagnostics.end(), allocation.diagnostics.begin(), allocation.diagnostics.end());
        return encoded;
    }

    const auto local_size = align16(function.local_stack_size);
    const auto saved_integer_bytes = static_cast<std::uint32_t>(allocation.used_integer_callee_saved.size()) * 8U;
    const auto saved_floating_bytes = static_cast<std::uint32_t>(allocation.used_floating_callee_saved.size()) * 8U;
    const auto saved_bytes = saved_integer_bytes + saved_floating_bytes;
    const auto spill_bytes = allocation.spill_bytes;
    // Reserve a full Q-sized staging cell per CFG edge argument. Scalar values
    // use the low bytes; vector values need all sixteen bytes so parallel-copy
    // cycles remain correct for both register files.
    const auto edge_bytes = max_edge_arguments * 16U;
    const auto frame_size = align16(local_size + saved_bytes + spill_bytes + edge_bytes + max_outgoing);
    encoded.frame_size = frame_size;
    encoded.stack_homed_value_count = allocation.spilled_value_count;
    encoded.register_allocated_value_count = allocation.physical_value_count;
    encoded.vector_register_allocated_value_count = allocation.vector_register_value_count;
    encoded.spilled_value_count = allocation.spilled_value_count;
    encoded.vector_spilled_value_count = allocation.vector_spilled_value_count;
    encoded.spill_bytes = allocation.spill_bytes;
    encoded.frame_bytes_saved = allocation.frame_bytes_saved;
    encoded.abi_outgoing_stack_bytes = max_outgoing;
    encoded.callee_saved_register_count = static_cast<std::uint32_t>(
        allocation.used_integer_callee_saved.size() + allocation.used_floating_callee_saved.size());

    const auto saved_integer_offset = [&](std::size_t index) -> std::int64_t {
        return -static_cast<std::int64_t>(local_size + (index + 1U) * 8U);
    };
    const auto saved_floating_offset = [&](std::size_t index) -> std::int64_t {
        return -static_cast<std::int64_t>(local_size + saved_integer_bytes + (index + 1U) * 8U);
    };
    const auto spill_offset = [&](machine::VirtualRegister reg) -> std::int64_t {
        const auto& location = allocation.location(reg);
        return -static_cast<std::int64_t>(local_size + saved_bytes + location.spill_offset + location.spill_size);
    };
    const auto edge_offset = [&](std::size_t index) -> std::int64_t {
        return -static_cast<std::int64_t>(local_size + saved_bytes + spill_bytes + (index + 1U) * 16U);
    };
    const auto register_width = [&](machine::VirtualRegister reg) -> unsigned {
        if (reg >= function.register_widths.size()) return 8U;
        const auto value = function.register_widths[reg];
        if (value == 1U || value == 2U || value == 4U || value == 8U) return value;
        return value <= 32U ? 4U : 8U;
    };
    const auto floating_value = [&](machine::VirtualRegister reg) -> bool {
        return reg < function.register_classes.size() &&
            function.register_classes[reg] == machine::RegisterClass::floating;
    };
    const auto vector_value = [&](machine::VirtualRegister reg) -> bool {
        return reg < function.register_classes.size() &&
            function.register_classes[reg] == machine::RegisterClass::vector;
    };

    Buffer out;
    // Stable frame record plus fixed save/spill areas. The AArch64 allocator
    // uses only AAPCS64 callee-saved banks, keeping caller-saved x0-x18/v0-v7
    // available for calls and encoder scratch without live-value shuffling.
    out.word(0xA9BF7BFDU); // stp x29, x30, [sp, #-16]!
    out.word(0x910003FDU); // mov x29, sp
    std::uint32_t remaining_frame = frame_size;
    while (remaining_frame != 0U) {
        const auto chunk = std::min<std::uint32_t>(remaining_frame, 4095U);
        emit_add_immediate(out, sp, sp, chunk, true);
        remaining_frame -= chunk;
    }
    for (std::size_t index = 0; index < allocation.used_integer_callee_saved.size(); ++index)
        emit_store_integer(out, allocation.used_integer_callee_saved[index], fp, saved_integer_offset(index), 8U);
    for (std::size_t index = 0; index < allocation.used_floating_callee_saved.size(); ++index)
        emit_store_float(out, allocation.used_floating_callee_saved[index], fp, saved_floating_offset(index), true);

    const auto store_integer_result = [&](machine::VirtualRegister reg, std::uint8_t source = scratch0) {
        const auto& location = allocation.location(reg);
        if (location.kind == AllocationKind::integer_register) {
            if (register_width(reg) <= 4U) emit_copy_w(out, location.physical, source);
            else emit_copy_x(out, location.physical, source);
        } else {
            emit_store_integer(out, source, fp, spill_offset(reg), register_width(reg));
        }
    };
    const auto load_integer_value = [&](machine::VirtualRegister reg, std::uint8_t destination = scratch0) {
        const auto& location = allocation.location(reg);
        if (location.kind == AllocationKind::integer_register) {
            if (register_width(reg) <= 4U) emit_copy_w(out, destination, location.physical);
            else emit_copy_x(out, destination, location.physical);
        } else {
            emit_load_integer(out, destination, fp, spill_offset(reg), register_width(reg));
        }
    };
    const auto store_float_result = [&](machine::VirtualRegister reg, std::uint8_t source = 0U) {
        const auto& location = allocation.location(reg);
        const bool wide = register_width(reg) == 8U;
        if (location.kind == AllocationKind::floating_register)
            emit_copy_float(out, location.physical, source, wide);
        else
            emit_store_float(out, source, fp, spill_offset(reg), wide);
    };
    const auto load_float_value = [&](machine::VirtualRegister reg, std::uint8_t destination = 0U) {
        const auto& location = allocation.location(reg);
        const bool wide = register_width(reg) == 8U;
        if (location.kind == AllocationKind::floating_register)
            emit_copy_float(out, destination, location.physical, wide);
        else
            emit_load_float(out, destination, fp, spill_offset(reg), wide);
    };
    const auto integer_source_register = [&](machine::VirtualRegister reg, std::uint8_t scratch) -> std::uint8_t {
        const auto& location = allocation.location(reg);
        if (location.kind == AllocationKind::integer_register) return location.physical;
        emit_load_integer(out, scratch, fp, spill_offset(reg), register_width(reg));
        return scratch;
    };
    const auto integer_result_register = [&](machine::VirtualRegister reg, std::uint8_t scratch) -> std::uint8_t {
        const auto& location = allocation.location(reg);
        return location.kind == AllocationKind::integer_register ? location.physical : scratch;
    };
    const auto floating_source_register = [&](machine::VirtualRegister reg, std::uint8_t scratch) -> std::uint8_t {
        const auto& location = allocation.location(reg);
        if (location.kind == AllocationKind::floating_register) return location.physical;
        emit_load_float(out, scratch, fp, spill_offset(reg), register_width(reg) == 8U);
        return scratch;
    };
    const auto floating_result_register = [&](machine::VirtualRegister reg, std::uint8_t scratch) -> std::uint8_t {
        const auto& location = allocation.location(reg);
        return location.kind == AllocationKind::floating_register ? location.physical : scratch;
    };
    const auto store_vector_result = [&](machine::VirtualRegister reg, std::uint8_t source = 0U) {
        const auto& location = allocation.location(reg);
        if (location.kind == AllocationKind::vector_register) {
            if (location.physical != source)
                emit_neon_integer_binary(out, machine::Opcode::or_i32, location.physical, source, source, false);
        } else {
            emit_store_neon128(out, source, fp, spill_offset(reg));
        }
    };
    const auto load_vector_value = [&](machine::VirtualRegister reg, std::uint8_t destination = 0U) {
        const auto& location = allocation.location(reg);
        if (location.kind == AllocationKind::vector_register) {
            if (location.physical != destination)
                emit_neon_integer_binary(out, machine::Opcode::or_i32, destination, location.physical, location.physical, false);
        } else {
            emit_load_neon128(out, destination, fp, spill_offset(reg));
        }
    };
    const auto emit_epilogue = [&]() {
        for (std::size_t index = allocation.used_floating_callee_saved.size(); index-- > 0U;)
            emit_load_float(out, allocation.used_floating_callee_saved[index], fp, saved_floating_offset(index), true);
        for (std::size_t index = allocation.used_integer_callee_saved.size(); index-- > 0U;)
            emit_load_integer(out, allocation.used_integer_callee_saved[index], fp, saved_integer_offset(index), 8U);
        emit_add_immediate(out, sp, fp, 0U);
        out.word(0xA8C17BFDU); // ldp x29, x30, [sp], #16
        out.word(0xD65F03C0U); // ret
    };

    const auto argument_locations = function_argument_locations(function, abi);
    std::unordered_map<std::string, const machine::Block*> block_lookup;
    for (const auto& block : function.blocks) block_lookup.emplace(block.name, &block);
    std::unordered_map<std::string, std::size_t> labels;
    std::vector<BranchFixup> branch_fixups;

    const auto emit_edge_copies = [&](const machine::Successor& successor) {
        const auto target = block_lookup.find(successor.block);
        if (target == block_lookup.end()) {
            add_error(diagnostics, "unknown AArch64 machine successor " + successor.block + " in @" + function.name);
            return false;
        }
        const auto& parameters = target->second->parameters;
        if (parameters.size() != successor.arguments.size()) {
            add_error(diagnostics, "AArch64 successor argument mismatch in @" + function.name);
            return false;
        }
        // Stage every source before writing any destination. This preserves true
        // parallel-copy semantics even for backedge cycles such as (a,b)->(b,a).
        for (std::size_t index = 0; index < successor.arguments.size(); ++index) {
            const auto source = successor.arguments[index];
            if (vector_value(source)) {
                load_vector_value(source, 0U);
                emit_store_neon128(out, 0U, fp, edge_offset(index));
            } else if (floating_value(source)) {
                load_float_value(source, 0U);
                emit_store_float(out, 0U, fp, edge_offset(index), register_width(source) == 8U);
            } else {
                load_integer_value(source, scratch0);
                emit_store_integer(out, scratch0, fp, edge_offset(index), register_width(source));
            }
        }
        for (std::size_t index = 0; index < parameters.size(); ++index) {
            const auto destination = parameters[index];
            if (vector_value(destination)) {
                emit_load_neon128(out, 0U, fp, edge_offset(index));
                store_vector_result(destination, 0U);
            } else if (floating_value(destination)) {
                emit_load_float(out, 0U, fp, edge_offset(index), register_width(destination) == 8U);
                store_float_result(destination, 0U);
            } else {
                emit_load_integer(out, scratch0, fp, edge_offset(index), register_width(destination));
                store_integer_result(destination, scratch0);
            }
        }
        return true;
    };

    const auto emit_call = [&](const machine::Instruction& instruction) -> bool {
        const bool indirect = is_indirect_call(instruction.opcode);
        std::size_t cursor = indirect ? 1U : 0U;
        if (indirect) load_integer_value(instruction.inputs.front(), indirect_call_scratch);

        machine::VirtualRegister aggregate_destination = 0;
        if (instruction.indirect_result) {
            if (cursor >= instruction.inputs.size()) {
                add_error(diagnostics, "malformed AAPCS64 indirect-result call in @" + function.name);
                return false;
            }
            load_integer_value(instruction.inputs[cursor++], 8U); // AAPCS64 sret register x8.
        } else if (instruction.opcode == machine::Opcode::call_aggregate) {
            if (cursor >= instruction.inputs.size()) {
                add_error(diagnostics, "malformed AAPCS64 aggregate call in @" + function.name);
                return false;
            }
            aggregate_destination = instruction.inputs[cursor++];
        }

        std::uint8_t integer_index = 0;
        std::uint8_t simd_index = 0;
        std::uint32_t stack_offset = 0;
        const auto argument_begin = cursor;
        const auto argument_width = [&](std::size_t input_index) -> std::uint32_t {
            if (input_index < instruction.argument_widths.size() && instruction.argument_widths[input_index] != 0U)
                return instruction.argument_widths[input_index];
            const auto reg = instruction.inputs[input_index];
            return vector_value(reg) ? 16U : register_width(reg);
        };
        while (cursor < instruction.inputs.size()) {
            auto group = cursor < instruction.argument_group_sizes.size() &&
                instruction.argument_group_sizes[cursor] != 0U
                ? static_cast<std::size_t>(instruction.argument_group_sizes[cursor]) : std::size_t{1};
            group = std::min(group, instruction.inputs.size() - cursor);
            bool all_simd = true;
            std::uint32_t group_bytes = 0U;
            for (std::size_t piece = 0; piece < group; ++piece) {
                const auto reg = instruction.inputs[cursor + piece];
                all_simd = all_simd && (floating_value(reg) || vector_value(reg));
                group_bytes += argument_width(cursor + piece);
            }
            const auto natural_alignment = cursor < instruction.argument_group_alignments.size() &&
                instruction.argument_group_alignments[cursor] != 0U
                ? static_cast<std::uint32_t>(instruction.argument_group_alignments[cursor]) : 8U;
            if (!all_simd && abi == Abi::aapcs64 && natural_alignment >= 16U && integer_index < 8U)
                integer_index = static_cast<std::uint8_t>((integer_index + 1U) & ~1U);
            const bool darwin_variadic_tail = abi == Abi::darwin && instruction.variadic_call &&
                cursor - argument_begin >= instruction.variadic_named_input_count;
            const bool registers_fit = !darwin_variadic_tail && (all_simd
                ? static_cast<std::size_t>(simd_index) + group <= 8U
                : static_cast<std::size_t>(integer_index) + group <= 8U);
            if (registers_fit) {
                for (std::size_t piece = 0; piece < group; ++piece) {
                    const auto reg = instruction.inputs[cursor + piece];
                    if (all_simd) {
                        if (vector_value(reg)) load_vector_value(reg, simd_index);
                        else load_float_value(reg, simd_index);
                        ++simd_index;
                    } else {
                        load_integer_value(reg, integer_index++);
                    }
                    ++encoded.abi_register_argument_count;
                }
            } else {
                if (!darwin_variadic_tail) {
                    if (all_simd) simd_index = 8U;
                    else integer_index = 8U;
                }
                const auto stack_alignment = darwin_variadic_tail
                    ? std::max<std::uint32_t>(8U, natural_alignment)
                    : abi == Abi::darwin ? natural_alignment : std::max<std::uint32_t>(8U, natural_alignment);
                stack_offset = align_to(stack_offset, stack_alignment);
                std::uint32_t piece_offset = 0U;
                for (std::size_t piece = 0; piece < group; ++piece) {
                    const auto reg = instruction.inputs[cursor + piece];
                    const auto width = argument_width(cursor + piece);
                    if (vector_value(reg)) {
                        load_vector_value(reg, 0U);
                        emit_store_neon128(out, 0U, sp, stack_offset + piece_offset);
                        piece_offset += width;
                    } else if (floating_value(reg)) {
                        load_float_value(reg, 0U);
                        emit_store_float(out, 0U, sp, stack_offset + piece_offset, width == 8U);
                        piece_offset += width;
                    } else {
                        load_integer_value(reg, scratch0);
                        emit_store_integer(out, scratch0, sp, stack_offset + piece_offset, width);
                        piece_offset += width;
                    }
                    ++encoded.abi_stack_argument_count;
                }
                stack_offset += abi == Abi::darwin && !darwin_variadic_tail
                    ? std::max(group_bytes, 1U)
                    : align_to(std::max(group_bytes, 1U), 8U);
            }
            cursor += group;
        }

        if (indirect) {
            out.word(0xD63F0000U | (static_cast<std::uint32_t>(indirect_call_scratch) << 5U)); // blr x16
        } else {
            const auto call_offset = out.size();
            out.word(0x94000000U); // bl symbol
            encoded.relocations.push_back({call_offset, instruction.symbol, RelocationKind::call26, 0});
        }

        switch (instruction.opcode) {
        case machine::Opcode::call_i32: case machine::Opcode::call_indirect_i32:
            emit_copy_w(out, scratch0, 0U); store_integer_result(instruction.result, scratch0); break;
        case machine::Opcode::call_i64: case machine::Opcode::call_indirect_i64:
            emit_copy_x(out, scratch0, 0U); store_integer_result(instruction.result, scratch0); break;
        case machine::Opcode::call_f32: case machine::Opcode::call_indirect_f32:
            store_float_result(instruction.result, 0U); break;
        case machine::Opcode::call_f64: case machine::Opcode::call_indirect_f64:
            store_float_result(instruction.result, 0U); break;
        case machine::Opcode::call_aggregate: {
            load_integer_value(aggregate_destination, scratch3);
            const auto count = instruction.argument_index & 0xFFU;
            std::uint32_t integer_piece = 0;
            std::uint32_t floating_piece = 0;
            std::uint32_t destination_offset = 0;
            for (std::uint32_t piece = 0; piece < count; ++piece) {
                const bool floating = (instruction.argument_index & (1U << (8U + piece))) != 0U;
                const bool narrow = (instruction.argument_index & (1U << (16U + piece))) != 0U;
                const auto width = narrow ? 4U : 8U;
                if (floating) {
                    emit_store_float(out, static_cast<std::uint8_t>(floating_piece++), scratch3,
                                     destination_offset, !narrow);
                } else {
                    emit_store_integer(out, static_cast<std::uint8_t>(integer_piece++), scratch3,
                                       destination_offset, width);
                }
                destination_offset += width;
            }
            break;
        }
        default: break;
        }
        return true;
    };

    for (const auto& block : function.blocks) {
        labels.emplace(block.name, out.size());
        for (const auto& instruction : block.instructions) {
            using O = machine::Opcode;
            if ((instruction.opcode == O::load_immediate || instruction.opcode == O::load_immediate_i64) &&
                instruction.result < allocation.intervals.size() &&
                allocation.intervals[instruction.result].use_count == 0U) {
                ++encoded.elided_dead_constant_count;
                continue;
            }
            switch (instruction.opcode) {
            case O::load_argument: case O::load_argument_i64:
            case O::load_argument_f32: case O::load_argument_f64: {
                if (instruction.argument_index >= argument_locations.size()) {
                    add_error(diagnostics, "AArch64 argument index out of range in @" + function.name);
                    return encoded;
                }
                const auto& location = argument_locations[instruction.argument_index];
                const bool floating = instruction.opcode == O::load_argument_f32 || instruction.opcode == O::load_argument_f64;
                const bool wide_float = instruction.opcode == O::load_argument_f64;
                const auto abi_width = instruction.argument_index < function.argument_widths.size() &&
                    function.argument_widths[instruction.argument_index] != 0U
                    ? static_cast<unsigned>(function.argument_widths[instruction.argument_index])
                    : register_width(instruction.result);
                if (location.kind == ArgumentLocation::Kind::indirect_result) {
                    emit_copy_x(out, scratch0, 8U);
                    store_integer_result(instruction.result, scratch0);
                } else if (location.kind == ArgumentLocation::Kind::integer_register) {
                    if (abi_width <= 4U) emit_copy_w(out, scratch0, location.index);
                    else emit_copy_x(out, scratch0, location.index);
                    if (abi_width == 1U || abi_width == 2U)
                        emit_zero_extend(out, scratch0, scratch0, abi_width * 8U);
                    store_integer_result(instruction.result, scratch0);
                } else if (location.kind == ArgumentLocation::Kind::floating_register) {
                    store_float_result(instruction.result, location.index);
                } else if (floating) {
                    emit_load_float(out, 0U, fp, 16 + static_cast<std::int64_t>(location.stack_offset), wide_float);
                    store_float_result(instruction.result, 0U);
                } else {
                    emit_load_integer(out, scratch0, fp, 16 + static_cast<std::int64_t>(location.stack_offset), abi_width);
                    store_integer_result(instruction.result, scratch0);
                }
                break;
            }
            case O::load_immediate: case O::load_immediate_i64:
            {
                const auto destination = integer_result_register(instruction.result, scratch0);
                emit_mov_imm64(out, destination, static_cast<std::uint64_t>(instruction.immediate));
                store_integer_result(instruction.result, destination);
                break;
            }
            case O::load_immediate_f32: case O::load_immediate_f64: {
                const bool wide = instruction.opcode == O::load_immediate_f64;
                emit_mov_imm64(out, scratch0, static_cast<std::uint64_t>(instruction.immediate));
                const auto destination = floating_result_register(instruction.result, 0U);
                emit_fmov_from_integer(out, destination, scratch0, wide);
                store_float_result(instruction.result, destination);
                break;
            }
            case O::load_function_address: case O::load_global_address: {
                const auto destination = integer_result_register(instruction.result, scratch0);
                const auto adrp = out.size();
                out.word(0x90000000U | destination);
                const auto add = out.size();
                out.word(0x91000000U | (static_cast<std::uint32_t>(destination) << 5U) | destination);
                encoded.relocations.push_back({adrp, instruction.symbol, RelocationKind::adr_prel_pg_hi21, 0});
                encoded.relocations.push_back({add, instruction.symbol, RelocationKind::add_abs_lo12_nc, 0});
                store_integer_result(instruction.result, destination);
                break;
            }
            case O::load_tls_address: {
                if (abi == Abi::darwin) {
                    const auto adrp = out.size();
                    out.word(0x90000000U); // adrp x0, symbol@TLVPPAGE
                    const auto ldr = out.size();
                    out.word(0xF9400000U); // ldr x0, [x0, symbol@TLVPPAGEOFF]
                    out.word(0xF9400010U); // ldr x16, [x0]
                    out.word(0xD63F0200U); // blr x16 (__tlv_bootstrap descriptor entry)
                    encoded.relocations.push_back({adrp, instruction.symbol, RelocationKind::tlvp_load_page21, 0});
                    encoded.relocations.push_back({ldr, instruction.symbol, RelocationKind::tlvp_load_pageoff12, 0});
                    store_integer_result(instruction.result, 0U);
                    break;
                }
                const auto destination = integer_result_register(instruction.result, scratch0);
                const auto adrp = out.size();
                out.word(0x90000000U | destination);
                const auto ldr = out.size();
                out.word(0xF9400000U | (static_cast<std::uint32_t>(destination) << 5U) | destination);
                out.word(0xD53BD040U | scratch1); // mrs x10, TPIDR_EL0
                out.word(0x8B000000U | (static_cast<std::uint32_t>(destination) << 16U) |
                         (static_cast<std::uint32_t>(scratch1) << 5U) | destination);
                encoded.relocations.push_back({adrp, instruction.symbol, RelocationKind::tlsie_adr_gottprel_page21, 0});
                encoded.relocations.push_back({ldr, instruction.symbol, RelocationKind::tlsie_ld64_gottprel_lo12_nc, 0});
                store_integer_result(instruction.result, destination);
                break;
            }
            case O::load_stack_address: {
                const auto destination = integer_result_register(instruction.result, scratch0);
                emit_address(out, destination, fp, instruction.immediate);
                store_integer_result(instruction.result, destination);
                break;
            }
            case O::ptr_offset: {
                const auto source = integer_source_register(instruction.inputs.at(0), scratch0);
                const auto destination = integer_result_register(instruction.result,
                    source == scratch0 ? scratch0 : scratch1);
                emit_address(out, destination, source, instruction.immediate);
                store_integer_result(instruction.result, destination);
                break;
            }
            case O::copy: {
                const auto source = integer_source_register(instruction.inputs.at(0), scratch0);
                store_integer_result(instruction.result, source);
                break;
            }
            case O::copy_f32: case O::copy_f64: {
                const auto source = floating_source_register(instruction.inputs.at(0), 0U);
                store_float_result(instruction.result, source);
                break;
            }
            case O::add_i32: case O::add_i64: case O::sub_i32: case O::sub_i64:
            case O::mul_i32: case O::mul_i64: case O::div_s_i32: case O::div_s_i64:
            case O::div_u_i32: case O::div_u_i64: case O::and_i32: case O::and_i64:
            case O::or_i32: case O::or_i64: case O::xor_i32: case O::xor_i64:
            case O::shl_i32: case O::shl_i64: case O::shr_s_i32: case O::shr_s_i64:
            case O::shr_u_i32: case O::shr_u_i64: {
                const bool wide = instruction.opcode == O::add_i64 || instruction.opcode == O::sub_i64 ||
                    instruction.opcode == O::mul_i64 || instruction.opcode == O::div_s_i64 ||
                    instruction.opcode == O::div_u_i64 || instruction.opcode == O::and_i64 ||
                    instruction.opcode == O::or_i64 || instruction.opcode == O::xor_i64 ||
                    instruction.opcode == O::shl_i64 || instruction.opcode == O::shr_s_i64 ||
                    instruction.opcode == O::shr_u_i64;
                const auto left = integer_source_register(instruction.inputs.at(0), scratch0);
                const auto destination = integer_result_register(instruction.result,
                    left == scratch0 ? scratch0 : scratch2);
                if (instruction.symbol == "$imm") {
                    if (!emit_integer_immediate(out, instruction.opcode, destination, left,
                                                instruction.immediate, wide)) {
                        add_error(diagnostics, "illegal AArch64 immediate selected in @" + function.name);
                        return encoded;
                    }
                    ++encoded.immediate_form_count;
                } else {
                    const auto right = integer_source_register(instruction.inputs.at(1), scratch1);
                    emit_integer_binary(out, instruction.opcode, destination, left, right, wide);
                }
                store_integer_result(instruction.result, destination);
                break;
            }
            case O::rem_s_i32: case O::rem_s_i64: case O::rem_u_i32: case O::rem_u_i64: {
                const bool wide = instruction.opcode == O::rem_s_i64 || instruction.opcode == O::rem_u_i64;
                const bool signed_division = instruction.opcode == O::rem_s_i32 || instruction.opcode == O::rem_s_i64;
                const auto left = integer_source_register(instruction.inputs.at(0), scratch0);
                const auto right = integer_source_register(instruction.inputs.at(1), scratch1);
                const auto destination = integer_result_register(instruction.result, scratch3);
                emit_integer_binary(out, signed_division ? (wide ? O::div_s_i64 : O::div_s_i32)
                                                         : (wide ? O::div_u_i64 : O::div_u_i32),
                                    scratch2, left, right, wide);
                out.word((wide ? 0x9B008000U : 0x1B008000U) |
                         (static_cast<std::uint32_t>(right) << 16U) |
                         (static_cast<std::uint32_t>(left) << 10U) |
                         (static_cast<std::uint32_t>(scratch2) << 5U) | destination);
                store_integer_result(instruction.result, destination);
                break;
            }
            case O::neg_i32: case O::neg_i64: {
                const bool wide = instruction.opcode == O::neg_i64;
                const auto source = integer_source_register(instruction.inputs.at(0), scratch0);
                const auto destination = integer_result_register(instruction.result, scratch1);
                out.word((wide ? 0xCB0003E0U : 0x4B0003E0U) |
                         (static_cast<std::uint32_t>(source) << 16U) | destination);
                store_integer_result(instruction.result, destination);
                break;
            }
            case O::not_i32: case O::not_i64: {
                const bool wide = instruction.opcode == O::not_i64;
                const auto source = integer_source_register(instruction.inputs.at(0), scratch0);
                const auto destination = integer_result_register(instruction.result, scratch1);
                out.word((wide ? 0xAA2003E0U : 0x2A2003E0U) |
                         (static_cast<std::uint32_t>(source) << 16U) | destination);
                store_integer_result(instruction.result, destination);
                break;
            }
            case O::select_i32: case O::select_i64: {
                const bool wide = instruction.opcode == O::select_i64;
                load_integer_value(instruction.inputs.at(0), scratch0);
                out.word((wide ? 0xF100001FU : 0x7100001FU) |
                         (static_cast<std::uint32_t>(scratch0) << 5U)); // cmp reg, #0
                load_integer_value(instruction.inputs.at(1), scratch0);
                load_integer_value(instruction.inputs.at(2), scratch1);
                out.word((wide ? 0x9A800000U : 0x1A800000U) |
                         (static_cast<std::uint32_t>(scratch1) << 16U) | (1U << 12U) |
                         (static_cast<std::uint32_t>(scratch0) << 5U) | scratch0);
                store_integer_result(instruction.result, scratch0);
                break;
            }
            case O::cmp_eq_i32: case O::cmp_ne_i32: case O::cmp_lt_i32: case O::cmp_le_i32:
            case O::cmp_gt_i32: case O::cmp_ge_i32: case O::cmp_ult_i32: case O::cmp_ule_i32:
            case O::cmp_ugt_i32: case O::cmp_uge_i32: case O::cmp_eq_i64: case O::cmp_ne_i64:
            case O::cmp_lt_i64: case O::cmp_le_i64: case O::cmp_gt_i64: case O::cmp_ge_i64:
            case O::cmp_ult_i64: case O::cmp_ule_i64: case O::cmp_ugt_i64: case O::cmp_uge_i64: {
                const bool wide = instruction.opcode >= O::cmp_eq_i64;
                const auto left = integer_source_register(instruction.inputs.at(0), scratch0);
                const auto destination = integer_result_register(instruction.result, scratch2);
                if (instruction.symbol == "$cmpimm") {
                    if (!emit_integer_compare_immediate(out, instruction.opcode, destination, left,
                                                        instruction.immediate, wide)) {
                        add_error(diagnostics, "illegal AArch64 compare immediate selected in @" + function.name);
                        return encoded;
                    }
                    ++encoded.immediate_form_count;
                } else {
                    const auto right = integer_source_register(instruction.inputs.at(1), scratch1);
                    emit_integer_compare(out, instruction.opcode, destination, left, right, wide);
                }
                store_integer_result(instruction.result, destination);
                break;
            }
            case O::add_f32: case O::add_f64: case O::sub_f32: case O::sub_f64:
            case O::mul_f32: case O::mul_f64: case O::div_f32: case O::div_f64: {
                const bool wide = instruction.opcode == O::add_f64 || instruction.opcode == O::sub_f64 ||
                    instruction.opcode == O::mul_f64 || instruction.opcode == O::div_f64;
                const auto left = floating_source_register(instruction.inputs.at(0), 0U);
                const auto right = floating_source_register(instruction.inputs.at(1), 1U);
                const auto destination = floating_result_register(instruction.result, 2U);
                emit_float_binary(out, instruction.opcode, destination, left, right, wide);
                store_float_result(instruction.result, destination);
                break;
            }
            case O::neg_f32: case O::neg_f64: {
                const bool wide = instruction.opcode == O::neg_f64;
                const auto source = floating_source_register(instruction.inputs.at(0), 0U);
                const auto destination = floating_result_register(instruction.result, 1U);
                out.word((wide ? 0x1E614000U : 0x1E214000U) |
                         (static_cast<std::uint32_t>(source) << 5U) | destination);
                store_float_result(instruction.result, destination);
                break;
            }
            case O::cmp_eq_f32: case O::cmp_ne_f32: case O::cmp_lt_f32: case O::cmp_le_f32:
            case O::cmp_gt_f32: case O::cmp_ge_f32: case O::cmp_eq_f64: case O::cmp_ne_f64:
            case O::cmp_lt_f64: case O::cmp_le_f64: case O::cmp_gt_f64: case O::cmp_ge_f64: {
                const bool wide = instruction.opcode >= O::cmp_eq_f64 && instruction.opcode <= O::cmp_ge_f64;
                const auto left = floating_source_register(instruction.inputs.at(0), 0U);
                const auto right = floating_source_register(instruction.inputs.at(1), 1U);
                const auto destination = integer_result_register(instruction.result, scratch0);
                emit_float_compare(out, instruction.opcode, destination, left, right, wide);
                store_integer_result(instruction.result, destination);
                break;
            }
            case O::zero_extend: case O::sign_extend: case O::truncate: {
                const auto source_bits = static_cast<unsigned>((instruction.immediate >> 8U) & 0xFFU);
                const auto result_bits = static_cast<unsigned>(instruction.immediate & 0xFFU);
                load_integer_value(instruction.inputs.at(0), scratch0);
                if (instruction.opcode == O::sign_extend) emit_sign_extend(out, scratch0, scratch0, source_bits);
                else emit_zero_extend(out, scratch0, scratch0, instruction.opcode == O::truncate ? result_bits : source_bits);
                store_integer_result(instruction.result, scratch0);
                break;
            }
            case O::int_to_float_signed: case O::int_to_float_unsigned: {
                const auto source_bits = static_cast<unsigned>((instruction.immediate >> 8U) & 0xFFU);
                const auto result_bits = static_cast<unsigned>(instruction.immediate & 0xFFU);
                load_integer_value(instruction.inputs.at(0), scratch0);
                if (source_bits < 32U) {
                    if (instruction.opcode == O::int_to_float_signed) emit_sign_extend(out, scratch0, scratch0, source_bits);
                    else emit_zero_extend(out, scratch0, scratch0, source_bits);
                }
                const bool source_wide = source_bits > 32U;
                const bool result_wide = result_bits > 32U;
                std::uint32_t base = instruction.opcode == O::int_to_float_signed ? 0x1E220000U : 0x1E230000U;
                if (source_wide) base |= 0x80000000U;
                if (result_wide) base |= 0x00400000U;
                out.word(base | (static_cast<std::uint32_t>(scratch0) << 5U));
                store_float_result(instruction.result, 0U);
                break;
            }
            case O::float_to_int_signed: case O::float_to_int_unsigned: {
                const auto source_bits = static_cast<unsigned>((instruction.immediate >> 8U) & 0xFFU);
                const auto result_bits = static_cast<unsigned>(instruction.immediate & 0xFFU);
                const bool source_wide = source_bits > 32U;
                const bool result_wide = result_bits > 32U;
                load_float_value(instruction.inputs.at(0), 0U);
                std::uint32_t base = instruction.opcode == O::float_to_int_signed ? 0x1E380000U : 0x1E390000U;
                if (result_wide) base |= 0x80000000U;
                if (source_wide) base |= 0x00400000U;
                out.word(base | scratch0);
                if (result_bits < 32U) emit_zero_extend(out, scratch0, scratch0, result_bits);
                store_integer_result(instruction.result, scratch0);
                break;
            }
            case O::float_extend: case O::float_truncate: {
                const bool extend = instruction.opcode == O::float_extend;
                load_float_value(instruction.inputs.at(0), extend ? 1U : 1U);
                out.word((extend ? 0x1E22C000U : 0x1E624000U) | (1U << 5U));
                store_float_result(instruction.result, 0U);
                break;
            }
            case O::load_stack_i8: case O::load_stack_i16: case O::load_stack_i32: case O::load_stack_i64: {
                const unsigned width = instruction.opcode == O::load_stack_i8 ? 1U : instruction.opcode == O::load_stack_i16 ? 2U : instruction.opcode == O::load_stack_i32 ? 4U : 8U;
                emit_load_integer(out, scratch0, fp, instruction.immediate, width);
                store_integer_result(instruction.result, scratch0);
                break;
            }
            case O::load_stack_f32: case O::load_stack_f64: {
                const bool wide = instruction.opcode == O::load_stack_f64;
                emit_load_float(out, 0U, fp, instruction.immediate, wide);
                store_float_result(instruction.result, 0U);
                break;
            }
            case O::store_stack_i8: case O::store_stack_i16: case O::store_stack_i32: case O::store_stack_i64: {
                const unsigned width = instruction.opcode == O::store_stack_i8 ? 1U : instruction.opcode == O::store_stack_i16 ? 2U : instruction.opcode == O::store_stack_i32 ? 4U : 8U;
                if (instruction.symbol == "$storeimm") emit_mov_imm64(out, scratch0, instruction.argument_index);
                else load_integer_value(instruction.inputs.at(0), scratch0);
                emit_store_integer(out, scratch0, fp, instruction.immediate, width);
                break;
            }
            case O::store_stack_f32: case O::store_stack_f64: {
                const bool wide = instruction.opcode == O::store_stack_f64;
                load_float_value(instruction.inputs.at(0), 0U);
                emit_store_float(out, 0U, fp, instruction.immediate, wide);
                break;
            }
            case O::load_stack_v128: {
                if (instruction.inputs.size() != 0U || !vector_value(instruction.result) ||
                    instruction.result >= function.register_widths.size() || function.register_widths[instruction.result] > 16U) {
                    add_error(diagnostics, "malformed AArch64 128-bit vector stack load in @" + function.name);
                    return encoded;
                }
                emit_load_neon128(out, 0U, fp, instruction.immediate);
                store_vector_result(instruction.result, 0U);
                break;
            }
            case O::store_stack_v128: {
                if (instruction.inputs.size() != 1U || !vector_value(instruction.inputs[0]) ||
                    instruction.inputs[0] >= function.register_widths.size() || function.register_widths[instruction.inputs[0]] > 16U) {
                    add_error(diagnostics, "malformed AArch64 128-bit vector stack store in @" + function.name);
                    return encoded;
                }
                load_vector_value(instruction.inputs[0], 0U);
                emit_store_neon128(out, 0U, fp, instruction.immediate);
                break;
            }
            case O::load_stack_v256: case O::load_stack_v512: {
                const auto width = instruction.opcode == O::load_stack_v256 ? 32U : 64U;
                if (!instruction.inputs.empty() || !vector_value(instruction.result) ||
                    instruction.result >= function.register_widths.size() ||
                    function.register_widths[instruction.result] != width ||
                    allocation.location(instruction.result).kind != AllocationKind::stack_slot) {
                    add_error(diagnostics, "malformed AArch64 wide-vector stack load in @" + function.name);
                    return encoded;
                }
                const auto home = spill_offset(instruction.result);
                for (unsigned chunk = 0; chunk < width; chunk += 16U) {
                    emit_load_neon128(out, 0U, fp, instruction.immediate + static_cast<std::int64_t>(chunk));
                    emit_store_neon128(out, 0U, fp, home + static_cast<std::int64_t>(chunk));
                }
                break;
            }
            case O::store_stack_v256: case O::store_stack_v512: {
                const auto width = instruction.opcode == O::store_stack_v256 ? 32U : 64U;
                if (instruction.inputs.size() != 1U || !vector_value(instruction.inputs[0]) ||
                    instruction.inputs[0] >= function.register_widths.size() ||
                    function.register_widths[instruction.inputs[0]] != width ||
                    allocation.location(instruction.inputs[0]).kind != AllocationKind::stack_slot) {
                    add_error(diagnostics, "malformed AArch64 wide-vector stack store in @" + function.name);
                    return encoded;
                }
                const auto home = spill_offset(instruction.inputs[0]);
                for (unsigned chunk = 0; chunk < width; chunk += 16U) {
                    emit_load_neon128(out, 0U, fp, home + static_cast<std::int64_t>(chunk));
                    emit_store_neon128(out, 0U, fp, instruction.immediate + static_cast<std::int64_t>(chunk));
                }
                break;
            }
            case O::load_ptr_i8: case O::load_ptr_i16: case O::load_ptr_i32: case O::load_ptr_i64: {
                const unsigned width = instruction.opcode == O::load_ptr_i8 ? 1U : instruction.opcode == O::load_ptr_i16 ? 2U : instruction.opcode == O::load_ptr_i32 ? 4U : 8U;
                load_integer_value(instruction.inputs.at(0), scratch1);
                emit_load_integer(out, scratch0, scratch1, instruction.immediate, width);
                store_integer_result(instruction.result, scratch0);
                break;
            }
            case O::load_ptr_f32: case O::load_ptr_f64: {
                const bool wide = instruction.opcode == O::load_ptr_f64;
                load_integer_value(instruction.inputs.at(0), scratch1);
                emit_load_float(out, 0U, scratch1, instruction.immediate, wide);
                store_float_result(instruction.result, 0U);
                break;
            }
            case O::store_ptr_i8: case O::store_ptr_i16: case O::store_ptr_i32: case O::store_ptr_i64: {
                const unsigned width = instruction.opcode == O::store_ptr_i8 ? 1U : instruction.opcode == O::store_ptr_i16 ? 2U : instruction.opcode == O::store_ptr_i32 ? 4U : 8U;
                if (instruction.symbol == "$storeimm") {
                    load_integer_value(instruction.inputs.at(0), scratch1);
                    emit_mov_imm64(out, scratch0, instruction.argument_index);
                } else {
                    load_integer_value(instruction.inputs.at(0), scratch0);
                    load_integer_value(instruction.inputs.at(1), scratch1);
                }
                emit_store_integer(out, scratch0, scratch1, instruction.immediate, width);
                break;
            }
            case O::store_ptr_f32: case O::store_ptr_f64: {
                const bool wide = instruction.opcode == O::store_ptr_f64;
                load_float_value(instruction.inputs.at(0), 0U);
                load_integer_value(instruction.inputs.at(1), scratch1);
                emit_store_float(out, 0U, scratch1, instruction.immediate, wide);
                break;
            }
            case O::add_i64_contiguous_inplace:
            case O::binary_i32_contiguous_inplace: case O::binary_i64_contiguous_inplace:
            case O::binary_i32_contiguous_map: case O::binary_i64_contiguous_map: {
                const bool legacy_add = instruction.opcode == O::add_i64_contiguous_inplace;
                const bool wide = legacy_add || instruction.opcode == O::binary_i64_contiguous_inplace ||
                                  instruction.opcode == O::binary_i64_contiguous_map;
                const bool inplace = legacy_add || instruction.opcode == O::binary_i32_contiguous_inplace ||
                                     instruction.opcode == O::binary_i64_contiguous_inplace;
                const auto lane_bytes = wide ? 8U : 4U;
                const auto operation = legacy_add ? O::add_i64 : static_cast<O>(instruction.argument_index);
                const bool floating = neon_float_operation_supported(operation, wide);
                if (instruction.inputs.size() != (inplace ? 2U : 3U) || instruction.immediate < 2 ||
                    instruction.immediate > 16 ||
                    !neon_packed_operation_supported(operation, wide)) {
                    add_error(diagnostics, "malformed AArch64 NEON scalar-map pack in @" + function.name);
                    return encoded;
                }
                const auto source = integer_source_register(instruction.inputs[0], scratch0);
                const auto destination = inplace ? source : integer_source_register(instruction.inputs[1], scratch2);
                const auto scalar_index = inplace ? 1U : 2U;
                const auto scalar = floating
                    ? floating_source_register(instruction.inputs[scalar_index], 1U)
                    : integer_source_register(instruction.inputs[scalar_index], scratch1);
                if (floating) emit_neon_broadcast_float(out, 1U, scalar, wide);
                else emit_neon_broadcast_integer(out, 1U, scalar, wide);
                const auto total_bytes = static_cast<std::int64_t>(instruction.immediate) * lane_bytes;
                std::int64_t offset = 0;
                for (; offset + 16 <= total_bytes; offset += 16) {
                    emit_load_neon128(out, 0U, source, offset);
                    emit_neon_packed_binary(out, operation, 0U, 0U, 1U, wide);
                    emit_store_neon128(out, 0U, destination, offset);
                    ++encoded.neon_vector_operation_count;
                }
                if (offset + 8 <= total_bytes) {
                    emit_load_neon64(out, 0U, source, offset);
                    emit_neon_packed_binary(out, operation, 0U, 0U, 1U, wide);
                    emit_store_neon64(out, 0U, destination, offset);
                    ++encoded.neon_vector_operation_count;
                    offset += 8;
                }
                for (; offset < total_bytes; offset += lane_bytes) {
                    if (floating) {
                        emit_load_float(out, 0U, source, offset, wide);
                        emit_float_binary(out, operation, 0U, 0U, 1U, wide);
                        emit_store_float(out, 0U, destination, offset, wide);
                    } else {
                        emit_load_integer(out, scratch3, source, offset, lane_bytes);
                        emit_integer_binary(out, operation, scratch3, scratch3, scalar, wide);
                        emit_store_integer(out, scratch3, destination, offset, lane_bytes);
                    }
                }
                break;
            }
            case O::binary_i32_contiguous_map2: case O::binary_i64_contiguous_map2: {
                const bool wide = instruction.opcode == O::binary_i64_contiguous_map2;
                const auto lane_bytes = wide ? 8U : 4U;
                const auto operation = static_cast<O>(instruction.argument_index);
                const bool floating = neon_float_operation_supported(operation, wide);
                if (instruction.inputs.size() != 3U || instruction.immediate < 2 || instruction.immediate > 16 ||
                    !neon_packed_operation_supported(operation, wide)) {
                    add_error(diagnostics, "malformed AArch64 NEON vector-map pack in @" + function.name);
                    return encoded;
                }
                const auto source_a = integer_source_register(instruction.inputs[0], scratch0);
                const auto source_b = integer_source_register(instruction.inputs[1], scratch1);
                const auto destination = integer_source_register(instruction.inputs[2], scratch2);
                const auto total_bytes = static_cast<std::int64_t>(instruction.immediate) * lane_bytes;
                std::int64_t offset = 0;
                for (; offset + 16 <= total_bytes; offset += 16) {
                    emit_load_neon128(out, 0U, source_a, offset);
                    emit_load_neon128(out, 1U, source_b, offset);
                    emit_neon_packed_binary(out, operation, 0U, 0U, 1U, wide);
                    emit_store_neon128(out, 0U, destination, offset);
                    ++encoded.neon_vector_operation_count;
                }
                if (offset + 8 <= total_bytes) {
                    emit_load_neon64(out, 0U, source_a, offset);
                    emit_load_neon64(out, 1U, source_b, offset);
                    emit_neon_packed_binary(out, operation, 0U, 0U, 1U, wide);
                    emit_store_neon64(out, 0U, destination, offset);
                    ++encoded.neon_vector_operation_count;
                    offset += 8;
                }
                for (; offset < total_bytes; offset += lane_bytes) {
                    if (floating) {
                        emit_load_float(out, 0U, source_a, offset, wide);
                        emit_load_float(out, 1U, source_b, offset, wide);
                        emit_float_binary(out, operation, 0U, 0U, 1U, wide);
                        emit_store_float(out, 0U, destination, offset, wide);
                    } else {
                        emit_load_integer(out, scratch3, source_a, offset, lane_bytes);
                        emit_load_integer(out, vector_scalar_scratch, source_b, offset, lane_bytes);
                        emit_integer_binary(out, operation, scratch3, scratch3, vector_scalar_scratch, wide);
                        emit_store_integer(out, scratch3, destination, offset, lane_bytes);
                    }
                }
                break;
            }
            case O::binary_i32_contiguous_map3: case O::binary_i64_contiguous_map3: {
                const bool wide = instruction.opcode == O::binary_i64_contiguous_map3;
                const auto lane_bytes = wide ? 8U : 4U;
                const auto first = static_cast<O>(instruction.argument_index & 0xffffU);
                const auto second = static_cast<O>((instruction.argument_index >> 16U) & 0xffffU);
                const bool floating = neon_float_operation_supported(first, wide);
                if (instruction.inputs.size() != 4U || instruction.immediate < 2 || instruction.immediate > 16 ||
                    !neon_packed_operation_supported(first, wide) || !neon_packed_operation_supported(second, wide) ||
                    floating != neon_float_operation_supported(second, wide)) {
                    add_error(diagnostics, "malformed AArch64 NEON chained-map pack in @" + function.name);
                    return encoded;
                }
                const auto source_a = integer_source_register(instruction.inputs[0], scratch0);
                const auto source_b = integer_source_register(instruction.inputs[1], scratch1);
                const auto source_c = integer_source_register(instruction.inputs[2], scratch2);
                const auto destination = integer_source_register(instruction.inputs[3], scratch3);
                const auto total_bytes = static_cast<std::int64_t>(instruction.immediate) * lane_bytes;
                std::int64_t offset = 0;
                for (; offset + 16 <= total_bytes; offset += 16) {
                    emit_load_neon128(out, 0U, source_a, offset);
                    emit_load_neon128(out, 1U, source_b, offset);
                    emit_neon_packed_binary(out, first, 0U, 0U, 1U, wide);
                    emit_load_neon128(out, 1U, source_c, offset);
                    emit_neon_packed_binary(out, second, 0U, 0U, 1U, wide);
                    emit_store_neon128(out, 0U, destination, offset);
                    encoded.neon_vector_operation_count += 2U;
                }
                // i64 packs are always a multiple of 16 bytes. The only tail
                // shape here is two i32 lanes (8 bytes), handled scalarly.
                if (offset + 8 <= total_bytes) {
                    emit_load_neon64(out, 0U, source_a, offset);
                    emit_load_neon64(out, 1U, source_b, offset);
                    emit_neon_packed_binary(out, first, 0U, 0U, 1U, wide);
                    emit_load_neon64(out, 1U, source_c, offset);
                    emit_neon_packed_binary(out, second, 0U, 0U, 1U, wide);
                    emit_store_neon64(out, 0U, destination, offset);
                    encoded.neon_vector_operation_count += 2U;
                    offset += 8;
                }
                for (; offset < total_bytes; offset += lane_bytes) {
                    if (floating) {
                        emit_load_float(out, 0U, source_a, offset, wide);
                        emit_load_float(out, 1U, source_b, offset, wide);
                        emit_float_binary(out, first, 0U, 0U, 1U, wide);
                        emit_load_float(out, 1U, source_c, offset, wide);
                        emit_float_binary(out, second, 0U, 0U, 1U, wide);
                        emit_store_float(out, 0U, destination, offset, wide);
                    } else {
                        emit_load_integer(out, vector_scalar_scratch, source_a, offset, lane_bytes);
                        emit_load_integer(out, indirect_call_scratch, source_b, offset, lane_bytes);
                        emit_integer_binary(out, first, vector_scalar_scratch, vector_scalar_scratch, indirect_call_scratch, wide);
                        emit_load_integer(out, indirect_call_scratch, source_c, offset, lane_bytes);
                        emit_integer_binary(out, second, vector_scalar_scratch, vector_scalar_scratch, indirect_call_scratch, wide);
                        emit_store_integer(out, vector_scalar_scratch, destination, offset, lane_bytes);
                    }
                }
                break;
            }
            case O::reduce_add_i32_contiguous: case O::reduce_add_i64_contiguous: {
                const bool wide = instruction.opcode == O::reduce_add_i64_contiguous;
                const auto lane_bytes = wide ? 8U : 4U;
                const auto minimum_lanes = wide ? 2 : 4;
                const auto maximum_lanes = wide ? 16 : 32;
                if (instruction.inputs.size() != 1U || instruction.immediate < minimum_lanes ||
                    instruction.immediate > maximum_lanes ||
                    (instruction.immediate & (instruction.immediate - 1)) != 0) {
                    add_error(diagnostics, "malformed AArch64 NEON contiguous reduction in @" + function.name);
                    return encoded;
                }
                const auto source = integer_source_register(instruction.inputs[0], scratch0);
                const auto total_bytes = static_cast<std::int64_t>(instruction.immediate) * lane_bytes;
                emit_load_neon128(out, 0U, source, 0);
                for (std::int64_t offset = 16; offset < total_bytes; offset += 16) {
                    emit_load_neon128(out, 1U, source, offset);
                    emit_neon_integer_binary(out, wide ? O::add_i64 : O::add_i32, 0U, 0U, 1U, wide);
                    ++encoded.neon_vector_operation_count;
                }
                emit_neon_reduce_add(out, 0U, 0U, wide);
                ++encoded.neon_vector_operation_count;
                emit_fmov_to_integer(out, scratch0, 0U, wide);
                store_integer_result(instruction.result, scratch0);
                break;
            }
            case O::binary_i32_contiguous_chain: case O::binary_i64_contiguous_chain: {
                const bool wide = instruction.opcode == O::binary_i64_contiguous_chain;
                const auto lane_bytes = wide ? 8U : 4U;
                if (instruction.symbol.size() < 6U || (instruction.symbol.size() & 1U) != 0U ||
                    instruction.inputs.size() != instruction.symbol.size() / 2U + 2U ||
                    instruction.immediate < 2 || instruction.immediate > 16) {
                    add_error(diagnostics, "malformed AArch64 NEON integer chain in @" + function.name);
                    return encoded;
                }
                std::vector<O> operations;
                operations.reserve(instruction.symbol.size() / 2U);
                std::optional<bool> floating;
                for (std::size_t offset = 0; offset + 1U < instruction.symbol.size(); offset += 2U) {
                    const auto operation = static_cast<O>(packed_token(instruction.symbol, offset));
                    if (!neon_packed_operation_supported(operation, wide)) {
                        add_error(diagnostics, "unsupported AArch64 NEON chain operation in @" + function.name);
                        return encoded;
                    }
                    const auto operation_floating = neon_float_operation_supported(operation, wide);
                    if (floating && *floating != operation_floating) {
                        add_error(diagnostics, "mixed integer/floating AArch64 NEON chain in @" + function.name);
                        return encoded;
                    }
                    floating = operation_floating;
                    operations.push_back(operation);
                }
                const auto source_count = operations.size() + 1U;
                const auto total_bytes = static_cast<std::int64_t>(instruction.immediate) * lane_bytes;
                std::int64_t offset = 0;
                for (; offset + 16 <= total_bytes; offset += 16) {
                    auto pointer = integer_source_register(instruction.inputs[0], scratch0);
                    emit_load_neon128(out, 0U, pointer, offset);
                    for (std::size_t operation_index = 0; operation_index < operations.size(); ++operation_index) {
                        pointer = integer_source_register(instruction.inputs[operation_index + 1U], scratch0);
                        emit_load_neon128(out, 1U, pointer, offset);
                        emit_neon_packed_binary(out, operations[operation_index], 0U, 0U, 1U, wide);
                        ++encoded.neon_vector_operation_count;
                    }
                    const auto destination = integer_source_register(instruction.inputs[source_count], scratch0);
                    emit_store_neon128(out, 0U, destination, offset);
                }
                if (offset + 8 <= total_bytes) {
                    auto pointer = integer_source_register(instruction.inputs[0], scratch0);
                    emit_load_neon64(out, 0U, pointer, offset);
                    for (std::size_t operation_index = 0; operation_index < operations.size(); ++operation_index) {
                        pointer = integer_source_register(instruction.inputs[operation_index + 1U], scratch0);
                        emit_load_neon64(out, 1U, pointer, offset);
                        emit_neon_packed_binary(out, operations[operation_index], 0U, 0U, 1U, wide);
                        ++encoded.neon_vector_operation_count;
                    }
                    const auto destination = integer_source_register(instruction.inputs[source_count], scratch0);
                    emit_store_neon64(out, 0U, destination, offset);
                    offset += 8;
                }
                for (; offset < total_bytes; offset += lane_bytes) {
                    auto pointer = integer_source_register(instruction.inputs[0], scratch0);
                    if (floating.value_or(false)) {
                        emit_load_float(out, 0U, pointer, offset, wide);
                        for (std::size_t operation_index = 0; operation_index < operations.size(); ++operation_index) {
                            pointer = integer_source_register(instruction.inputs[operation_index + 1U], scratch0);
                            emit_load_float(out, 1U, pointer, offset, wide);
                            emit_float_binary(out, operations[operation_index], 0U, 0U, 1U, wide);
                        }
                        const auto destination = integer_source_register(instruction.inputs[source_count], scratch0);
                        emit_store_float(out, 0U, destination, offset, wide);
                    } else {
                        emit_load_integer(out, scratch3, pointer, offset, lane_bytes);
                        for (std::size_t operation_index = 0; operation_index < operations.size(); ++operation_index) {
                            pointer = integer_source_register(instruction.inputs[operation_index + 1U], scratch0);
                            emit_load_integer(out, vector_scalar_scratch, pointer, offset, lane_bytes);
                            emit_integer_binary(out, operations[operation_index], scratch3, scratch3,
                                                vector_scalar_scratch, wide);
                        }
                        const auto destination = integer_source_register(instruction.inputs[source_count], scratch0);
                        emit_store_integer(out, scratch3, destination, offset, lane_bytes);
                    }
                }
                break;
            }
            case O::binary_i32_contiguous_dag: case O::binary_i64_contiguous_dag: {
                const bool wide = instruction.opcode == O::binary_i64_contiguous_dag;
                const auto lane_bytes = wide ? 8U : 4U;
                if (instruction.symbol.size() < 10U || (instruction.symbol.size() & 1U) != 0U ||
                    instruction.immediate < (wide ? 2 : 4) || instruction.immediate > 16) {
                    add_error(diagnostics, "malformed AArch64 NEON integer DAG in @" + function.name);
                    return encoded;
                }
                std::vector<std::uint16_t> program;
                program.reserve(instruction.symbol.size() / 2U);
                std::size_t max_source = 0U;
                bool saw_source = false;
                std::optional<bool> floating;
                for (std::size_t metadata_offset = 0; metadata_offset + 1U < instruction.symbol.size(); metadata_offset += 2U) {
                    const auto token = packed_token(instruction.symbol, metadata_offset);
                    program.push_back(token);
                    if ((token & 0x8000U) != 0U) {
                        saw_source = true;
                        max_source = std::max(max_source, static_cast<std::size_t>(token & 0x7fffU));
                    } else {
                        const auto operation = static_cast<O>(token);
                        if (!neon_packed_operation_supported(operation, wide)) {
                            add_error(diagnostics, "unsupported AArch64 NEON DAG operation in @" + function.name);
                            return encoded;
                        }
                        const auto operation_floating = neon_float_operation_supported(operation, wide);
                        if (floating && *floating != operation_floating) {
                            add_error(diagnostics, "mixed integer/floating AArch64 NEON DAG in @" + function.name);
                            return encoded;
                        }
                        floating = operation_floating;
                    }
                }
                const auto source_count = saw_source ? max_source + 1U : 0U;
                if (source_count == 0U || instruction.inputs.size() != source_count + 1U) {
                    add_error(diagnostics, "malformed AArch64 NEON integer DAG source list in @" + function.name);
                    return encoded;
                }
                const auto total_bytes = static_cast<std::int64_t>(instruction.immediate) * lane_bytes;
                if ((total_bytes & 7) != 0) {
                    add_error(diagnostics, "AArch64 NEON DAG tail must cover at least 64 bits in @" + function.name);
                    return encoded;
                }
                const std::array<std::uint8_t, 8> vector_stack_registers{0U, 1U, 2U, 3U, 4U, 5U, 6U, 7U};
                for (std::int64_t offset = 0; offset < total_bytes;) {
                    const auto chunk_bytes = std::min<std::int64_t>(16, total_bytes - offset);
                    std::vector<std::uint8_t> stack;
                    stack.reserve(vector_stack_registers.size());
                    for (const auto token : program) {
                        if ((token & 0x8000U) != 0U) {
                            const auto source_index = static_cast<std::size_t>(token & 0x7fffU);
                            if (source_index >= source_count || stack.size() >= vector_stack_registers.size()) {
                                add_error(diagnostics, "AArch64 NEON integer DAG stack/source overflow in @" + function.name);
                                return encoded;
                            }
                            const auto target = vector_stack_registers[stack.size()];
                            const auto pointer = integer_source_register(instruction.inputs[source_index], scratch0);
                            if (chunk_bytes == 16) emit_load_neon128(out, target, pointer, offset);
                            else emit_load_neon64(out, target, pointer, offset);
                            stack.push_back(target);
                            continue;
                        }
                        if (stack.size() < 2U) {
                            add_error(diagnostics, "AArch64 NEON integer DAG stack underflow in @" + function.name);
                            return encoded;
                        }
                        const auto operation = static_cast<O>(token);
                        const auto right = stack.back();
                        stack.pop_back();
                        const auto left = stack.back();
                        emit_neon_packed_binary(out, operation, left, left, right, wide);
                        ++encoded.neon_vector_operation_count;
                    }
                    if (stack.size() != 1U) {
                        add_error(diagnostics, "AArch64 NEON integer DAG stack imbalance in @" + function.name);
                        return encoded;
                    }
                    const auto destination = integer_source_register(instruction.inputs[source_count], scratch0);
                    if (chunk_bytes == 16) emit_store_neon128(out, stack.front(), destination, offset);
                    else emit_store_neon64(out, stack.front(), destination, offset);
                    offset += chunk_bytes;
                }
                break;
            }
            case O::binary_i32_contiguous_dag_reuse: case O::binary_i64_contiguous_dag_reuse: {
                const bool wide = instruction.opcode == O::binary_i64_contiguous_dag_reuse;
                const auto lane_bytes = wide ? 8U : 4U;
                if (instruction.symbol.size() < 18U || (instruction.symbol.size() % 6U) != 0U ||
                    instruction.immediate < (wide ? 2 : 4) || instruction.immediate > 16) {
                    add_error(diagnostics, "malformed AArch64 reusable NEON integer DAG in @" + function.name);
                    return encoded;
                }
                struct DagNode { std::uint16_t tag{}, lhs{}, rhs{}; };
                std::vector<DagNode> nodes;
                nodes.reserve(instruction.symbol.size() / 6U);
                std::size_t max_source = 0U;
                bool saw_source = false;
                std::optional<bool> floating;
                for (std::size_t at = 0; at < instruction.symbol.size(); at += 6U) {
                    DagNode node{packed_token(instruction.symbol, at), packed_token(instruction.symbol, at + 2U),
                                 packed_token(instruction.symbol, at + 4U)};
                    if ((node.tag & 0x8000U) != 0U) {
                        saw_source = true;
                        max_source = std::max(max_source, static_cast<std::size_t>(node.tag & 0x7fffU));
                    } else {
                        const auto operation = static_cast<O>(node.tag);
                        if (node.lhs >= nodes.size() || node.rhs >= nodes.size() ||
                            !neon_packed_operation_supported(operation, wide)) {
                            add_error(diagnostics, "invalid AArch64 reusable NEON DAG node in @" + function.name);
                            return encoded;
                        }
                        const auto operation_floating = neon_float_operation_supported(operation, wide);
                        if (floating && *floating != operation_floating) {
                            add_error(diagnostics, "mixed integer/floating AArch64 reusable NEON DAG in @" + function.name);
                            return encoded;
                        }
                        floating = operation_floating;
                    }
                    nodes.push_back(node);
                }
                const auto source_count = saw_source ? max_source + 1U : 0U;
                if (source_count == 0U || instruction.inputs.size() != source_count + 1U) {
                    add_error(diagnostics, "malformed AArch64 reusable NEON DAG source list in @" + function.name);
                    return encoded;
                }
                const auto total_bytes = static_cast<std::int64_t>(instruction.immediate) * lane_bytes;
                if ((total_bytes & 7) != 0) {
                    add_error(diagnostics, "AArch64 reusable NEON DAG tail must cover at least 64 bits in @" + function.name);
                    return encoded;
                }

                std::vector<std::uint32_t> base_uses(nodes.size(), 0U);
                for (const auto& node : nodes) {
                    if ((node.tag & 0x8000U) != 0U) continue;
                    ++base_uses[node.lhs];
                    ++base_uses[node.rhs];
                }

                // v0-v7 are encoder scratch registers. v24-v31 are caller-saved
                // and intentionally outside the allocator's v16-v23 vector bank,
                // giving reusable DAGs sixteen Q temporaries without clobbering a
                // live machine value.
                constexpr std::array<std::uint8_t, 16> dag_pool{
                    0U, 1U, 2U, 3U, 4U, 5U, 6U, 7U,
                    24U, 25U, 26U, 27U, 28U, 29U, 30U, 31U};
                for (std::int64_t offset = 0; offset < total_bytes;) {
                    const auto chunk_bytes = std::min<std::int64_t>(16, total_bytes - offset);
                    std::array<bool, dag_pool.size()> busy{};
                    std::vector<int> node_register(nodes.size(), -1);
                    auto remaining_uses = base_uses;
                    const auto allocate_q = [&]() -> int {
                        for (std::size_t index = 0; index < dag_pool.size(); ++index) {
                            if (busy[index]) continue;
                            busy[index] = true;
                            return static_cast<int>(dag_pool[index]);
                        }
                        return -1;
                    };
                    const auto release_q = [&](int reg) {
                        for (std::size_t index = 0; index < dag_pool.size(); ++index) {
                            if (dag_pool[index] == reg) {
                                busy[index] = false;
                                return;
                            }
                        }
                    };

                    for (std::size_t id = 0; id < nodes.size(); ++id) {
                        const auto& node = nodes[id];
                        if ((node.tag & 0x8000U) != 0U) {
                            const auto source_index = static_cast<std::size_t>(node.tag & 0x7fffU);
                            if (source_index >= source_count) {
                                add_error(diagnostics, "AArch64 reusable NEON DAG source overflow in @" + function.name);
                                return encoded;
                            }
                            const auto reg = allocate_q();
                            if (reg < 0) {
                                add_error(diagnostics, "AArch64 reusable NEON DAG exceeded Q-register budget in @" + function.name);
                                return encoded;
                            }
                            const auto pointer = integer_source_register(instruction.inputs[source_index], scratch0);
                            if (chunk_bytes == 16) emit_load_neon128(out, static_cast<std::uint8_t>(reg), pointer, offset);
                            else emit_load_neon64(out, static_cast<std::uint8_t>(reg), pointer, offset);
                            node_register[id] = reg;
                            continue;
                        }

                        const auto lhs = node.lhs;
                        const auto rhs = node.rhs;
                        const auto lhs_reg = node_register[lhs];
                        const auto rhs_reg = node_register[rhs];
                        if (lhs_reg < 0 || rhs_reg < 0) {
                            add_error(diagnostics, "AArch64 reusable NEON DAG references an unavailable value in @" + function.name);
                            return encoded;
                        }

                        const auto lhs_consumptions = lhs == rhs ? 2U : 1U;
                        int destination = -1;
                        if (remaining_uses[lhs] == lhs_consumptions) destination = lhs_reg;
                        else if (lhs != rhs && remaining_uses[rhs] == 1U) destination = rhs_reg;
                        else destination = allocate_q();
                        if (destination < 0) {
                            add_error(diagnostics, "AArch64 reusable NEON DAG exceeded Q-register budget in @" + function.name);
                            return encoded;
                        }

                        emit_neon_packed_binary(out, static_cast<O>(node.tag), static_cast<std::uint8_t>(destination),
                                               static_cast<std::uint8_t>(lhs_reg),
                                               static_cast<std::uint8_t>(rhs_reg), wide);
                        ++encoded.neon_vector_operation_count;

                        if (remaining_uses[lhs] != 0U) --remaining_uses[lhs];
                        if (remaining_uses[rhs] != 0U) --remaining_uses[rhs];
                        if (remaining_uses[lhs] == 0U && lhs_reg != destination) release_q(lhs_reg);
                        if (lhs != rhs && remaining_uses[rhs] == 0U && rhs_reg != destination) release_q(rhs_reg);
                        node_register[id] = destination;
                    }

                    const auto root = node_register.back();
                    if (root < 0) {
                        add_error(diagnostics, "AArch64 reusable NEON DAG has no result in @" + function.name);
                        return encoded;
                    }
                    const auto destination = integer_source_register(instruction.inputs[source_count], scratch0);
                    if (chunk_bytes == 16) emit_store_neon128(out, static_cast<std::uint8_t>(root), destination, offset);
                    else emit_store_neon64(out, static_cast<std::uint8_t>(root), destination, offset);
                    offset += chunk_bytes;
                }
                break;
            }
            case O::call_i32: case O::call_i64: case O::call_f32: case O::call_f64:
            case O::call_void: case O::call_aggregate: case O::call_indirect_i32:
            case O::call_indirect_i64: case O::call_indirect_f32: case O::call_indirect_f64:
            case O::call_indirect_void:
                if (!emit_call(instruction)) return encoded;
                break;
            case O::jump: {
                if (instruction.successors.size() != 1U || !emit_edge_copies(instruction.successors[0])) return encoded;
                const auto offset = out.size();
                out.word(0x14000000U);
                branch_fixups.push_back({offset, instruction.successors[0].block});
                break;
            }
            case O::branch_i1: {
                if (instruction.successors.size() != 2U || instruction.inputs.size() != 1U) {
                    add_error(diagnostics, "malformed AArch64 branch in @" + function.name);
                    return encoded;
                }
                load_integer_value(instruction.inputs[0], scratch0);
                const auto cbz = out.size();
                out.word(0x34000000U | scratch0);
                if (!emit_edge_copies(instruction.successors[0])) return encoded;
                const auto true_branch = out.size();
                out.word(0x14000000U);
                branch_fixups.push_back({true_branch, instruction.successors[0].block});
                const auto false_stub = out.size();
                if (!patch_cbz19(out, cbz, false_stub, scratch0)) {
                    add_error(diagnostics, "AArch64 conditional branch stub is out of range in @" + function.name);
                    return encoded;
                }
                if (!emit_edge_copies(instruction.successors[1])) return encoded;
                const auto false_branch = out.size();
                out.word(0x14000000U);
                branch_fixups.push_back({false_branch, instruction.successors[1].block});
                break;
            }
            case O::return_i32:
                load_integer_value(instruction.inputs.at(0), scratch0);
                emit_copy_w(out, 0U, scratch0);
                emit_epilogue();
                break;
            case O::return_i64:
                load_integer_value(instruction.inputs.at(0), scratch0);
                emit_copy_x(out, 0U, scratch0);
                emit_epilogue();
                break;
            case O::return_f32: case O::return_f64:
                load_float_value(instruction.inputs.at(0), 0U);
                emit_epilogue();
                break;
            case O::return_void:
                emit_epilogue();
                break;
            case O::return_aggregate: {
                if (instruction.inputs.size() != 1U) {
                    add_error(diagnostics, "malformed AArch64 aggregate return in @" + function.name);
                    return encoded;
                }
                load_integer_value(instruction.inputs[0], scratch3);
                const auto count = instruction.argument_index & 0xFFU;
                std::uint32_t integer_piece = 0;
                std::uint32_t floating_piece = 0;
                std::uint32_t source_offset = 0;
                for (std::uint32_t piece = 0; piece < count; ++piece) {
                    const bool floating = (instruction.argument_index & (1U << (8U + piece))) != 0U;
                    const bool narrow = (instruction.argument_index & (1U << (16U + piece))) != 0U;
                    const auto width = narrow ? 4U : 8U;
                    if (floating) {
                        emit_load_float(out, static_cast<std::uint8_t>(floating_piece++), scratch3,
                                        source_offset, !narrow);
                    } else {
                        emit_load_integer(out, static_cast<std::uint8_t>(integer_piece++), scratch3,
                                          source_offset, width);
                    }
                    source_offset += width;
                }
                emit_epilogue();
                break;
            }
            default:
                add_error(diagnostics, "unsupported AArch64 machine opcode " +
                    std::string(machine::opcode_name(instruction.opcode)) + " in @" + function.name);
                return encoded;
            }
        }
    }

    for (const auto& fixup : branch_fixups) {
        const auto target = labels.find(fixup.target);
        if (target == labels.end() || !patch_branch26(out, fixup.offset, target->second)) {
            add_error(diagnostics, "AArch64 branch target is unresolved or out of range in @" + function.name);
            return encoded;
        }
    }

    encoded.code = out.take();
    return encoded;
}

} // namespace

EncodeResult encode(const machine::Module& module, Abi abi) {
    EncodeResult result;
    if (abi != Abi::aapcs64 && abi != Abi::darwin) {
        add_error(result.diagnostics, "unsupported AArch64 ABI");
        return result;
    }
    result.diagnostics = machine::verify_module(module);
    if (!result.diagnostics.empty()) return result;
    for (const auto& function : module.functions) {
        auto encoded = encode_function(function, abi, result.diagnostics);
        if (!result.diagnostics.empty()) return result;
        result.functions.push_back(std::move(encoded));
    }
    return result;
}

ImageEncodeResult encode_image(const machine::Module& module, Abi abi) {
    ImageEncodeResult result;
    auto functions = encode(module, abi);
    result.diagnostics = std::move(functions.diagnostics);
    if (!result.diagnostics.empty()) return result;
    result.functions = functions.functions;

    std::unordered_set<std::string> defined_globals;
    for (const auto& global : module.globals) {
        if (global.is_external) {
            (global.is_thread_local ? result.image.external_tls : result.image.external_globals).push_back(global.name);
            continue;
        }
        const auto section = global.is_thread_local ? DataSection::tls :
            global.is_constant ? DataSection::read_only : DataSection::writable;
        auto& data = global.is_thread_local ? result.image.thread_local_data :
            global.is_constant ? result.image.read_only_data : result.image.writable_data;
        const auto alignment = std::max<std::size_t>(1U, global.alignment);
        while ((data.size() % alignment) != 0U) data.push_back(std::byte{0});
        const auto offset = data.size();
        if (!defined_globals.insert(global.name).second) {
            add_error(result.diagnostics, "duplicate AArch64 global @" + global.name);
            return result;
        }
        result.image.globals.push_back({global.name, section, offset, global.is_internal});
        for (const auto byte : global.initializer) data.push_back(static_cast<std::byte>(byte));
    }

    for (const auto& function : result.functions) {
        const auto base = result.image.code.size();
        result.image.entries.emplace_back(function.name, base);
        result.image.code.insert(result.image.code.end(), function.code.begin(), function.code.end());
        for (auto relocation : function.relocations) {
            relocation.offset += base;
            result.image.relocations.push_back(std::move(relocation));
        }
    }
    std::sort(result.image.relocations.begin(), result.image.relocations.end(),
              [](const auto& left, const auto& right) { return left.offset < right.offset; });
    return result;
}

std::optional<std::uintptr_t> checked_address_offset(std::uintptr_t base, std::size_t offset) noexcept {
    if (offset > std::numeric_limits<std::uintptr_t>::max() - base) return std::nullopt;
    return base + static_cast<std::uintptr_t>(offset);
}

std::optional<std::uintptr_t> checked_address_addend(std::uintptr_t base, std::int64_t addend) noexcept {
    if (addend >= 0) {
        const auto amount = static_cast<std::uint64_t>(addend);
        if (amount > std::numeric_limits<std::uintptr_t>::max() - base) return std::nullopt;
        return base + static_cast<std::uintptr_t>(amount);
    }
    // Avoid negating INT64_MIN in signed arithmetic.
    const auto amount = static_cast<std::uint64_t>(-(addend + 1)) + 1U;
    if (amount > base) return std::nullopt;
    return base - static_cast<std::uintptr_t>(amount);
}

Diagnostics materialize_jit_external_globals(
    EncodedModuleImage& image,
    const ExternalResolver& global_resolver) {
    Diagnostics diagnostics;
    if (image.external_globals.empty()) return diagnostics;
    if (!global_resolver) {
        add_error(diagnostics, "AArch64 JIT module has unresolved external globals and no resolver");
        return diagnostics;
    }
    if (!image.jit_external_global_slots.empty()) {
        add_error(diagnostics, "AArch64 JIT external-global table was materialized more than once");
        return diagnostics;
    }

    // Resolve every address before mutating the image. A failed host lookup must
    // not leave a half-materialized pointer table behind if the caller wants to
    // report/retry the load with a different resolver.
    std::vector<std::pair<std::string, std::uintptr_t>> resolved;
    resolved.reserve(image.external_globals.size());
    for (const auto& symbol : image.external_globals) {
        const auto address = global_resolver(symbol);
        if (!address) {
            add_error(diagnostics, "unresolved AArch64 JIT external global @" + symbol);
            continue;
        }
        resolved.emplace_back(symbol, *address);
    }
    if (!diagnostics.empty()) return diagnostics;

    image.jit_external_global_slots.reserve(resolved.size());
    for (const auto& [symbol, address] : resolved) {
        while ((image.read_only_data.size() & 7U) != 0U) image.read_only_data.push_back(std::byte{0});
        const auto offset = image.read_only_data.size();
        const auto bits = static_cast<std::uint64_t>(address);
        for (unsigned shift = 0U; shift < 64U; shift += 8U)
            image.read_only_data.push_back(static_cast<std::byte>(static_cast<std::uint8_t>(bits >> shift)));
        image.jit_external_global_slots.push_back({symbol, offset});
    }
    return diagnostics;
}

Diagnostics materialize_jit_tls(
    EncodedModuleImage& image,
    const JitTlsDescriptorResolver& descriptor_resolver,
    std::uintptr_t helper_address) {
    Diagnostics diagnostics;
    const auto is_tls_page = [](RelocationKind kind) noexcept {
        return kind == RelocationKind::tlsie_adr_gottprel_page21 ||
               kind == RelocationKind::tlvp_load_page21;
    };
    const auto is_tls_low = [](RelocationKind kind) noexcept {
        return kind == RelocationKind::tlsie_ld64_gottprel_lo12_nc ||
               kind == RelocationKind::tlvp_load_pageoff12;
    };
    const auto matching_low = [](RelocationKind page) noexcept {
        return page == RelocationKind::tlsie_adr_gottprel_page21
            ? RelocationKind::tlsie_ld64_gottprel_lo12_nc
            : RelocationKind::tlvp_load_pageoff12;
    };

    if (!image.jit_tls_thunks.empty()) {
        add_error(diagnostics, "AArch64 JIT TLS thunks were materialized more than once");
        return diagnostics;
    }
    bool has_tls_relocations = false;
    for (const auto& relocation : image.relocations)
        has_tls_relocations = has_tls_relocations || is_tls_page(relocation.kind) || is_tls_low(relocation.kind);
    if (!has_tls_relocations) return diagnostics;
    if (!descriptor_resolver) {
        add_error(diagnostics, "AArch64 JIT TLS references have no descriptor resolver");
        return diagnostics;
    }
    if (helper_address == 0U) {
        add_error(diagnostics, "AArch64 JIT TLS runtime helper address is null");
        return diagnostics;
    }

    struct Site {
        std::size_t page_index{};
        std::size_t low_index{};
        std::size_t offset{};
        std::string symbol;
        bool darwin{};
        std::uint8_t destination{};
    };
    std::vector<Site> sites;
    std::unordered_set<std::size_t> consumed_low;
    std::unordered_map<std::string, std::uintptr_t> descriptors;
    std::vector<std::string> descriptor_order;

    const auto read_word = [&](std::size_t offset) -> std::uint32_t {
        std::uint32_t value = 0U;
        for (unsigned index = 0U; index < 4U; ++index)
            value |= static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(image.code[offset + index])) << (index * 8U);
        return value;
    };

    for (std::size_t page_index = 0U; page_index < image.relocations.size(); ++page_index) {
        const auto& page = image.relocations[page_index];
        if (!is_tls_page(page.kind)) continue;
        if ((page.offset & 3U) != 0U || page.offset + 16U > image.code.size()) {
            add_error(diagnostics, "malformed AArch64 JIT TLS sequence for @" + page.symbol);
            continue;
        }
        std::optional<std::size_t> low_index;
        for (std::size_t candidate = 0U; candidate < image.relocations.size(); ++candidate) {
            const auto& low = image.relocations[candidate];
            if (low.kind == matching_low(page.kind) && low.symbol == page.symbol && low.offset == page.offset + 4U) {
                if (low_index) {
                    add_error(diagnostics, "duplicate AArch64 JIT TLS low relocation for @" + page.symbol);
                    break;
                }
                low_index = candidate;
            }
        }
        if (!low_index) {
            add_error(diagnostics, "incomplete AArch64 JIT TLS relocation pair for @" + page.symbol);
            continue;
        }
        consumed_low.insert(*low_index);
        const bool darwin = page.kind == RelocationKind::tlvp_load_page21;
        const auto destination = darwin ? std::uint8_t{0U}
            : static_cast<std::uint8_t>(read_word(page.offset) & 0x1FU);
        sites.push_back({page_index, *low_index, page.offset, page.symbol, darwin, destination});
        if (!descriptors.contains(page.symbol)) {
            const auto descriptor = descriptor_resolver(page.symbol);
            if (!descriptor || *descriptor == 0U)
                add_error(diagnostics, "unresolved AArch64 JIT TLS descriptor @" + page.symbol);
            else {
                descriptors.emplace(page.symbol, *descriptor);
                descriptor_order.push_back(page.symbol);
            }
        }
    }
    for (std::size_t index = 0U; index < image.relocations.size(); ++index) {
        if (is_tls_low(image.relocations[index].kind) && !consumed_low.contains(index))
            add_error(diagnostics, "orphan AArch64 JIT TLS low relocation for @" + image.relocations[index].symbol);
    }
    if (!diagnostics.empty()) return diagnostics;

    auto code = image.code;
    auto relocations = image.relocations;
    std::vector<JitTlsThunk> thunks;
    thunks.reserve(descriptors.size());
    std::unordered_map<std::string, std::size_t> veneer_offsets;
    veneer_offsets.reserve(descriptors.size());

    const auto append_word = [&](std::uint32_t word) {
        for (unsigned index = 0U; index < 4U; ++index)
            code.push_back(static_cast<std::byte>(static_cast<std::uint8_t>(word >> (index * 8U))));
    };
    const auto append_absolute = [&](std::uint8_t reg, std::uintptr_t address) {
        const auto bits = static_cast<std::uint64_t>(address);
        const auto half = [&](unsigned shift) -> std::uint32_t {
            return static_cast<std::uint32_t>((bits >> shift) & 0xFFFFU);
        };
        append_word(0xD2800000U | (half(0U) << 5U) | reg);
        append_word(0xF2A00000U | (half(16U) << 5U) | reg);
        append_word(0xF2C00000U | (half(32U) << 5U) | reg);
        append_word(0xF2E00000U | (half(48U) << 5U) | reg);
    };
    for (const auto& symbol : descriptor_order) {
        const auto descriptor = descriptors.at(symbol);
        const auto offset = code.size();
        veneer_offsets.emplace(symbol, offset);
        append_absolute(0U, descriptor);      // x0 = opaque TLS descriptor
        append_absolute(16U, helper_address); // x16 = runtime helper
        append_word(0xD61F0200U);             // br x16; helper returns to original BL caller
        thunks.push_back({symbol, offset, descriptor});
    }

    const auto write_word = [&](std::size_t offset, std::uint32_t word) {
        for (unsigned index = 0U; index < 4U; ++index)
            code[offset + index] = static_cast<std::byte>(static_cast<std::uint8_t>(word >> (index * 8U)));
    };
    for (const auto& site : sites) {
        const auto found = veneer_offsets.find(site.symbol);
        if (found == veneer_offsets.end()) {
            add_error(diagnostics, "missing AArch64 JIT TLS veneer @" + site.symbol);
            continue;
        }
        const auto delta = static_cast<std::int64_t>(found->second) - static_cast<std::int64_t>(site.offset);
        if ((delta & 3) != 0 || delta < -(std::int64_t{1} << 27) || delta >= (std::int64_t{1} << 27)) {
            add_error(diagnostics, "AArch64 JIT TLS veneer is outside BL range @" + site.symbol);
            continue;
        }
        const auto words = delta / 4;
        write_word(site.offset, 0x94000000U | (static_cast<std::uint32_t>(words) & 0x03FFFFFFU));
        write_word(site.offset + 4U, site.destination == 0U
            ? 0xD503201FU
            : 0xAA0003E0U | static_cast<std::uint32_t>(site.destination)); // mov xD,x0
        write_word(site.offset + 8U, 0xD503201FU);
        write_word(site.offset + 12U, 0xD503201FU);
    }
    if (!diagnostics.empty()) return diagnostics;

    std::vector<Relocation> kept;
    kept.reserve(relocations.size() - sites.size() * 2U);
    for (const auto& relocation : relocations) {
        if (is_tls_page(relocation.kind) || is_tls_low(relocation.kind)) continue;
        kept.push_back(relocation);
    }
    image.code = std::move(code);
    image.relocations = std::move(kept);
    image.jit_tls_thunks = std::move(thunks);
    return diagnostics;
}

Diagnostics resolve_jit_relocations(
    EncodedModuleImage& image,
    std::uintptr_t code_address,
    std::uintptr_t read_only_data_address,
    std::uintptr_t writable_data_address,
    const ExternalResolver& resolver,
    const ExternalResolver& global_resolver) {
    Diagnostics diagnostics;
    if (code_address == 0U) {
        add_error(diagnostics, "AArch64 JIT code base is null");
        return diagnostics;
    }
    std::unordered_map<std::string, std::uintptr_t> defined;
    defined.reserve(image.entries.size() + image.globals.size());
    for (const auto& [name, offset] : image.entries) {
        if (offset > image.code.size()) {
            add_error(diagnostics, "AArch64 JIT function entry is outside the code image @" + name);
            continue;
        }
        const auto address = checked_address_offset(code_address, offset);
        if (!address) {
            add_error(diagnostics, "AArch64 JIT function address overflows the process address space @" + name);
            continue;
        }
        defined.emplace(name, *address);
    }
    for (const auto& global : image.globals) {
        std::uintptr_t base = 0U;
        std::size_t size = 0U;
        switch (global.section) {
        case DataSection::read_only:
            base = read_only_data_address;
            size = image.read_only_data.size();
            break;
        case DataSection::writable:
            base = writable_data_address;
            size = image.writable_data.size();
            break;
        case DataSection::tls:
            // JIT TLS accesses are rewritten to runtime thunks before generic
            // relocation resolution, so no process-static address exists here.
            continue;
        }
        if (global.data_offset > size) {
            add_error(diagnostics, "AArch64 JIT global offset is outside its data image @" + global.name);
            continue;
        }
        const auto address = checked_address_offset(base, global.data_offset);
        if (!address) {
            add_error(diagnostics, "AArch64 JIT global address overflows the process address space @" + global.name);
            continue;
        }
        defined.emplace(global.name, *address);
    }
    if (!diagnostics.empty()) return diagnostics;

    std::unordered_set<std::string> external_globals(
        image.external_globals.begin(), image.external_globals.end());
    std::unordered_map<std::string, std::uintptr_t> jit_external_global_slots;
    jit_external_global_slots.reserve(image.jit_external_global_slots.size());
    for (const auto& slot : image.jit_external_global_slots) {
        if (slot.data_offset + 8U > image.read_only_data.size()) {
            add_error(diagnostics, "AArch64 JIT external-global slot is outside read-only data @" + slot.symbol);
            continue;
        }
        if ((slot.data_offset & 7U) != 0U) {
            add_error(diagnostics, "AArch64 JIT external-global slot is misaligned @" + slot.symbol);
            continue;
        }
        const auto address = checked_address_offset(read_only_data_address, slot.data_offset);
        if (!address) {
            add_error(diagnostics, "AArch64 JIT external-global slot address overflows the process address space @" + slot.symbol);
            continue;
        }
        jit_external_global_slots.emplace(slot.symbol, *address);
    }
    if (!diagnostics.empty()) return diagnostics;

    const auto read_word = [&](std::size_t offset) -> std::uint32_t {
        std::uint32_t value = 0U;
        for (unsigned index = 0; index < 4U; ++index)
            value |= static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(image.code[offset + index])) << (index * 8U);
        return value;
    };
    const auto write_word = [&](std::size_t offset, std::uint32_t value) {
        for (unsigned index = 0; index < 4U; ++index)
            image.code[offset + index] = static_cast<std::byte>(static_cast<std::uint8_t>(value >> (index * 8U)));
    };
    const auto external_address = [&](const Relocation& relocation) -> std::optional<std::uintptr_t> {
        if (external_globals.contains(relocation.symbol)) {
            if (!global_resolver) return std::nullopt;
            return global_resolver(relocation.symbol);
        }
        if (!resolver) return std::nullopt;
        return resolver(relocation.symbol);
    };

    for (const auto& relocation : image.relocations) {
        if ((relocation.offset & 3U) != 0U || relocation.offset + 4U > image.code.size()) {
            add_error(diagnostics, "malformed AArch64 JIT relocation for @" + relocation.symbol);
            continue;
        }
        if (relocation.kind == RelocationKind::tlsie_adr_gottprel_page21 ||
            relocation.kind == RelocationKind::tlsie_ld64_gottprel_lo12_nc ||
            relocation.kind == RelocationKind::tlvp_load_page21 ||
            relocation.kind == RelocationKind::tlvp_load_pageoff12) {
            add_error(diagnostics, "AArch64 JIT TLS relocation was not materialized for @" + relocation.symbol);
            continue;
        }

        std::optional<std::uintptr_t> target;
        const auto jit_slot = jit_external_global_slots.find(relocation.symbol);
        if (jit_slot != jit_external_global_slots.end()) target = jit_slot->second;
        else if (const auto found = defined.find(relocation.symbol); found != defined.end()) target = found->second;
        else target = external_address(relocation);
        if (!target) {
            add_error(diagnostics, "unresolved AArch64 JIT symbol @" + relocation.symbol);
            continue;
        }
        const auto target_value = checked_address_addend(*target, relocation.addend);
        if (!target_value) {
            add_error(diagnostics, "AArch64 JIT relocation addend overflows the process address space @" + relocation.symbol);
            continue;
        }
        const auto pc_value = checked_address_offset(code_address, relocation.offset);
        if (!pc_value) {
            add_error(diagnostics, "AArch64 JIT relocation PC overflows the process address space @" + relocation.symbol);
            continue;
        }
        const auto pc = *pc_value;
        auto word = read_word(relocation.offset);

        switch (relocation.kind) {
        case RelocationKind::call26: {
            const auto delta = static_cast<std::int64_t>(*target_value) - static_cast<std::int64_t>(pc);
            if ((delta & 3) != 0 || delta < -(std::int64_t{1} << 27) || delta >= (std::int64_t{1} << 27)) {
                add_error(diagnostics, "AArch64 JIT call target is outside BL range @" + relocation.symbol);
                break;
            }
            const auto words = delta / 4;
            word = (word & 0xFC000000U) | (static_cast<std::uint32_t>(words) & 0x03FFFFFFU);
            write_word(relocation.offset, word);
            break;
        }
        case RelocationKind::adr_prel_pg_hi21: {
            const auto target_page = static_cast<std::int64_t>(*target_value & ~std::uintptr_t{0xFFFU});
            const auto pc_page = static_cast<std::int64_t>(pc & ~std::uintptr_t{0xFFFU});
            const auto page_delta = (target_page - pc_page) / 4096;
            if (page_delta < -(std::int64_t{1} << 20) || page_delta >= (std::int64_t{1} << 20)) {
                add_error(diagnostics, "AArch64 JIT address target is outside ADRP range @" + relocation.symbol);
                break;
            }
            const auto immediate = static_cast<std::uint64_t>(page_delta) & 0x1FFFFFU;
            const auto immlo = static_cast<std::uint32_t>(immediate & 0x3U);
            const auto immhi = static_cast<std::uint32_t>((immediate >> 2U) & 0x7FFFFU);
            word &= ~((0x3U << 29U) | (0x7FFFFU << 5U));
            word |= (immlo << 29U) | (immhi << 5U);
            write_word(relocation.offset, word);
            break;
        }
        case RelocationKind::add_abs_lo12_nc:
            if (jit_slot != jit_external_global_slots.end()) {
                const auto low12 = static_cast<std::uint32_t>(*target_value & 0xFFFU);
                if ((low12 & 7U) != 0U) {
                    add_error(diagnostics, "AArch64 JIT external-global slot is not 8-byte aligned @" + relocation.symbol);
                    break;
                }
                const auto rd = word & 0x1FU;
                const auto rn = (word >> 5U) & 0x1FU;
                // The encoder's global-address sequence is ADRP rd; ADD rd,rd,#lo12.
                // Replace ADD with LDR Xrd,[Xrn,#slot_lo12] so the nearby table
                // yields the unrestricted absolute host-global address.
                word = 0xF9400000U | ((low12 / 8U) << 10U) | (rn << 5U) | rd;
            } else {
                word &= ~(0xFFFU << 10U);
                word |= static_cast<std::uint32_t>(*target_value & 0xFFFU) << 10U;
            }
            write_word(relocation.offset, word);
            break;
        case RelocationKind::tlsie_adr_gottprel_page21:
        case RelocationKind::tlsie_ld64_gottprel_lo12_nc:
        case RelocationKind::tlvp_load_page21:
        case RelocationKind::tlvp_load_pageoff12:
            break;
        }
    }
    return diagnostics;
}

std::string format_hex(const std::vector<std::byte>& code) {
    std::ostringstream out;
    out << std::hex << std::setfill('0');
    for (std::size_t index = 0; index < code.size(); ++index) {
        if (index != 0U) out << ' ';
        out << std::setw(2) << static_cast<unsigned>(std::to_integer<std::uint8_t>(code[index]));
    }
    return out.str();
}

} // namespace forge::codegen::aarch64
