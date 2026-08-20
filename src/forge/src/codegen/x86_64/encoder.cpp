// Copyright 2026 Mario Vinciguerra
// SPDX-License-Identifier: Apache-2.0

#include <algorithm>
#include "forge/codegen/x86_64/encoder.hpp"

#include "forge/machine/register_allocation.hpp"
#include "forge/machine/verifier.hpp"

#include <array>
#include <atomic>
#include <cstring>
#include <iomanip>
#include <limits>
#include <sstream>
#include <thread>
#include <unordered_map>
#include <unordered_set>

namespace forge::codegen::x86_64 {
namespace {
class Buffer {
public:
    void byte(std::uint8_t value) { bytes_.push_back(static_cast<std::byte>(value)); }
    void i32(std::int32_t value) {
        const auto bits = static_cast<std::uint32_t>(value);
        for (unsigned shift = 0; shift < 32; shift += 8) byte(static_cast<std::uint8_t>(bits >> shift));
    }
    void i64(std::int64_t value) {
        const auto bits = static_cast<std::uint64_t>(value);
        for (unsigned shift = 0; shift < 64; shift += 8) byte(static_cast<std::uint8_t>(bits >> shift));
    }
    [[nodiscard]] std::size_t size() const noexcept { return bytes_.size(); }
    void patch_i32(std::size_t offset, std::int32_t value) {
        const auto bits = static_cast<std::uint32_t>(value);
        for (unsigned shift = 0; shift < 32; shift += 8)
            bytes_.at(offset + shift / 8U) = static_cast<std::byte>(static_cast<std::uint8_t>(bits >> shift));
    }
    void record_spill_load() noexcept { ++spill_load_count_; }
    void record_spill_store() noexcept { ++spill_store_count_; }
    [[nodiscard]] std::uint32_t spill_load_count() const noexcept { return spill_load_count_; }
    [[nodiscard]] std::uint32_t spill_store_count() const noexcept { return spill_store_count_; }
    [[nodiscard]] const std::vector<std::byte>& bytes() const noexcept { return bytes_; }
    void replace(std::vector<std::byte> bytes) { bytes_ = std::move(bytes); }
    [[nodiscard]] std::vector<std::byte> take() { return std::move(bytes_); }
private:
    std::vector<std::byte> bytes_;
    std::uint32_t spill_load_count_{};
    std::uint32_t spill_store_count_{};
};

enum class Register : std::uint8_t {
    eax = 0, ecx = 1, edx = 2, ebx = 3, esp = 4, ebp = 5, esi = 6, edi = 7,
    r8d = 8, r9d = 9, r10d = 10, r11d = 11, r12d = 12, r13d = 13, r14d = 14, r15d = 15
};

void emit_modrm(Buffer& out, std::uint8_t mod, std::uint8_t reg, std::uint8_t rm);

enum class XmmRegister : std::uint8_t { xmm0 = 0, xmm1 = 1, xmm2 = 2, xmm3 = 3, xmm4 = 4, xmm5 = 5, xmm6 = 6, xmm7 = 7 };

Register integer_register(machine::PhysicalRegister reg) {
    switch (reg) {
    case machine::PhysicalRegister::r8d: return Register::r8d;
    case machine::PhysicalRegister::r9d: return Register::r9d;
    case machine::PhysicalRegister::ebx: return Register::ebx;
    case machine::PhysicalRegister::r10d: return Register::r10d;
    case machine::PhysicalRegister::r11d: return Register::r11d;
    case machine::PhysicalRegister::r12d: return Register::r12d;
    case machine::PhysicalRegister::r13d: return Register::r13d;
    case machine::PhysicalRegister::r14d: return Register::r14d;
    case machine::PhysicalRegister::r15d: return Register::r15d;
    }
    return Register::r10d;
}

XmmRegister floating_register(machine::FloatingPhysicalRegister reg) {
    switch (reg) {
    case machine::FloatingPhysicalRegister::xmm2: return XmmRegister::xmm2;
    case machine::FloatingPhysicalRegister::xmm3: return XmmRegister::xmm3;
    case machine::FloatingPhysicalRegister::xmm4: return XmmRegister::xmm4;
    case machine::FloatingPhysicalRegister::xmm5: return XmmRegister::xmm5;
    }
    return XmmRegister::xmm2;
}

void emit_sse_move(Buffer& out, XmmRegister destination, XmmRegister source, bool wide) {
    if (destination == source) return;
    out.byte(wide ? 0xF2 : 0xF3); out.byte(0x0F); out.byte(0x10);
    emit_modrm(out, 3, static_cast<std::uint8_t>(destination), static_cast<std::uint8_t>(source));
}

void emit_sse_stack_load(Buffer& out, XmmRegister destination, std::int32_t displacement, bool wide) {
    out.byte(wide ? 0xF2 : 0xF3); out.byte(0x0F); out.byte(0x10);
    emit_modrm(out, 2, static_cast<std::uint8_t>(destination), static_cast<std::uint8_t>(Register::ebp));
    out.i32(displacement);
}

void emit_sse_stack_store(Buffer& out, XmmRegister source, std::int32_t displacement, bool wide) {
    out.byte(wide ? 0xF2 : 0xF3); out.byte(0x0F); out.byte(0x11);
    emit_modrm(out, 2, static_cast<std::uint8_t>(source), static_cast<std::uint8_t>(Register::ebp));
    out.i32(displacement);
}

// Packed spill and reload. These use the unaligned forms (MOVDQU / VMOVDQU) so
// a spill slot only has to be non-overlapping, not aligned to its own width.
// 128-bit uses the SSE2 encoding; 256-bit uses two-byte VEX; 512-bit uses EVEX.
void emit_vector_stack_move(Buffer& out, XmmRegister reg, std::int32_t displacement,
                            std::uint16_t bits, bool store) {
    const std::uint8_t opcode = store ? 0x7F : 0x6F;
    if (bits >= 512U) {
        // EVEX.512.F3.0F.W0 6F/7F — 4-byte prefix, zeroing-unmasked.
        out.byte(0x62);
        out.byte(0xF1);              // R/X/B set, mm = 0F map
        out.byte(0x7E);              // W0, vvvv unused, pp = F3
        out.byte(0x48);              // L'L = 10 (512-bit), no mask
    } else if (bits >= 256U) {
        out.byte(0xC5);              // two-byte VEX
        out.byte(0xFA);              // R set, vvvv unused, L = 1 (256-bit), pp = F3
    } else {
        out.byte(0xF3); out.byte(0x0F);
    }
    out.byte(opcode);
    emit_modrm(out, 2, static_cast<std::uint8_t>(reg), static_cast<std::uint8_t>(Register::ebp));
    out.i32(displacement);
}

void emit_ptr_modrm(Buffer& out, std::uint8_t reg, Register pointer, std::int32_t displacement) {
    emit_modrm(out, displacement == 0 ? 0 : 2, reg, static_cast<std::uint8_t>(pointer));
    if (displacement != 0) out.i32(displacement);
}

void emit_xmm128_ptr_store(Buffer& out, Register pointer, XmmRegister source, std::int32_t displacement = 0) {
    const auto src = static_cast<std::uint8_t>(source);
    const auto ptr = static_cast<std::uint8_t>(pointer);
    out.byte(0xF3);
    if (src >= 8U || ptr >= 8U) out.byte(static_cast<std::uint8_t>(0x40U | (src >= 8U ? 0x04U : 0U) | (ptr >= 8U ? 0x01U : 0U)));
    out.byte(0x0F); out.byte(0x7F);
    emit_ptr_modrm(out, src, pointer, displacement);
}
void emit_xmm128_ptr_load(Buffer& out, XmmRegister destination, Register pointer, std::int32_t displacement = 0) {
    const auto dst = static_cast<std::uint8_t>(destination);
    const auto ptr = static_cast<std::uint8_t>(pointer);
    out.byte(0xF3);
    if (dst >= 8U || ptr >= 8U) out.byte(static_cast<std::uint8_t>(0x40U | (dst >= 8U ? 0x04U : 0U) | (ptr >= 8U ? 0x01U : 0U)));
    out.byte(0x0F); out.byte(0x6F);
    emit_ptr_modrm(out, dst, pointer, displacement);
}
void emit_xmm64_ptr_store(Buffer& out, Register pointer, XmmRegister source, std::int32_t displacement = 0) {
    const auto src = static_cast<std::uint8_t>(source);
    const auto ptr = static_cast<std::uint8_t>(pointer);
    out.byte(0x66);
    if (src >= 8U || ptr >= 8U) out.byte(static_cast<std::uint8_t>(0x40U | (src >= 8U ? 0x04U : 0U) | (ptr >= 8U ? 0x01U : 0U)));
    out.byte(0x0F); out.byte(0xD6);
    emit_ptr_modrm(out, src, pointer, displacement);
}
void emit_xmm64_ptr_load(Buffer& out, XmmRegister destination, Register pointer, std::int32_t displacement = 0) {
    const auto dst = static_cast<std::uint8_t>(destination);
    const auto ptr = static_cast<std::uint8_t>(pointer);
    out.byte(0xF3);
    if (dst >= 8U || ptr >= 8U) out.byte(static_cast<std::uint8_t>(0x40U | (dst >= 8U ? 0x04U : 0U) | (ptr >= 8U ? 0x01U : 0U)));
    out.byte(0x0F); out.byte(0x7E);
    emit_ptr_modrm(out, dst, pointer, displacement);
}
void emit_vex3(Buffer& out, std::uint8_t map, std::uint8_t pp, bool l256,
               std::uint8_t reg, std::uint8_t vvvv, std::uint8_t rm, bool memory_rm = false) {
    const bool r = reg >= 8U;
    const bool b = rm >= 8U;
    out.byte(0xC4);
    out.byte(static_cast<std::uint8_t>((r ? 0U : 0x80U) | 0x40U | (b ? 0U : 0x20U) | (map & 0x1FU)));
    out.byte(static_cast<std::uint8_t>(((~vvvv) & 0x0FU) << 3U | (l256 ? 0x04U : 0U) | (pp & 0x03U)));
    (void)memory_rm;
}
void emit_vex_mov_gpr_to_xmm(Buffer& out, XmmRegister destination, Register source, bool wide) {
    const auto dst = static_cast<std::uint8_t>(destination);
    const auto src = static_cast<std::uint8_t>(source);
    const bool r = dst >= 8U;
    const bool b = src >= 8U;
    out.byte(0xC4);
    out.byte(static_cast<std::uint8_t>((r ? 0U : 0x80U) | 0x40U | (b ? 0U : 0x20U) | 0x01U));
    // VMOVD/VMOVQ xmm, r32/r64: VEX.128.66.0F.W0/W1 6E /r,
    // with the unused vvvv field encoded as 1111b.
    out.byte(static_cast<std::uint8_t>((wide ? 0x80U : 0U) | 0x78U | 0x01U));
    out.byte(0x6E);
    emit_modrm(out, 3, dst, src);
}
void emit_ymm256_ptr_load(Buffer& out, XmmRegister destination, Register pointer, std::int32_t displacement = 0) {
    const auto dst = static_cast<std::uint8_t>(destination);
    const auto ptr = static_cast<std::uint8_t>(pointer);
    emit_vex3(out, 1U, 2U, true, dst, 0U, ptr, true); // VMOVDQU ymm, m256
    out.byte(0x6F);
    emit_ptr_modrm(out, dst, pointer, displacement);
}
void emit_ymm256_ptr_store(Buffer& out, Register pointer, XmmRegister source, std::int32_t displacement = 0) {
    const auto src = static_cast<std::uint8_t>(source);
    const auto ptr = static_cast<std::uint8_t>(pointer);
    emit_vex3(out, 1U, 2U, true, src, 0U, ptr, true); // VMOVDQU m256, ymm
    out.byte(0x7F);
    emit_ptr_modrm(out, src, pointer, displacement);
}
void emit_avx2_integer_binary(Buffer& out, XmmRegister destination, XmmRegister lhs, XmmRegister rhs, std::uint8_t opcode) {
    const auto dst = static_cast<std::uint8_t>(destination);
    const auto left = static_cast<std::uint8_t>(lhs);
    const auto right = static_cast<std::uint8_t>(rhs);
    emit_vex3(out, 1U, 1U, true, dst, left, right); // VPADDD/Q, VPSUBD/Q, VPAND/OR/XOR
    out.byte(opcode);
    emit_modrm(out, 3, dst, right);
}
void emit_avx2_broadcast(Buffer& out, XmmRegister destination, XmmRegister source, bool wide) {
    const auto dst = static_cast<std::uint8_t>(destination);
    const auto src = static_cast<std::uint8_t>(source);
    emit_vex3(out, 2U, 1U, true, dst, 0U, src); // VPBROADCASTD/Q ymm, xmm
    out.byte(wide ? 0x59 : 0x58);
    emit_modrm(out, 3, dst, src);
}
enum class MaskRegister : std::uint8_t { k0 = 0, k1 = 1, k2 = 2, k3 = 3, k4 = 4, k5 = 5, k6 = 6, k7 = 7 };

void emit_evex(Buffer& out, std::uint8_t map, std::uint8_t pp, bool wide64,
               std::uint16_t vector_bits, std::uint8_t reg, std::uint8_t vvvv,
               std::uint8_t rm, MaskRegister mask = MaskRegister::k0, bool zeroing = false) {
    const bool r = (reg & 0x08U) != 0U;
    const bool b = (rm & 0x08U) != 0U;
    // Forge's current packed scratch allocator uses vector registers 0-7, so
    // EVEX.R'/V' remain in their non-extended state. The helper still handles
    // high GPR memory bases through EVEX.B.
    out.byte(0x62);
    out.byte(static_cast<std::uint8_t>(0xC0U | (b ? 0U : 0x20U) | (r ? 0U : 0x10U) | (map & 0x07U)));
    out.byte(static_cast<std::uint8_t>((wide64 ? 0x80U : 0U) | (((~vvvv) & 0x0FU) << 3U) | 0x04U | (pp & 0x03U)));
    std::uint8_t length = 0U;
    if (vector_bits >= 512U) length = 0x40U;       // EVEX.L'L = 10b
    else if (vector_bits >= 256U) length = 0x20U;  // EVEX.L'L = 01b
    out.byte(static_cast<std::uint8_t>((zeroing ? 0x80U : 0U) | length | 0x08U | static_cast<std::uint8_t>(mask)));
}

void emit_zmm512_ptr_load(Buffer& out, XmmRegister destination, Register pointer, std::int32_t displacement,
                          bool wide, MaskRegister mask = MaskRegister::k0, bool zeroing = false) {
    const auto dst = static_cast<std::uint8_t>(destination);
    const auto ptr = static_cast<std::uint8_t>(pointer);
    emit_evex(out, 1U, 2U, wide, 512U, dst, 0U, ptr, mask, zeroing); // VMOVDQU32/64 zmm, m512
    out.byte(0x6F);
    emit_ptr_modrm(out, dst, pointer, displacement);
}

void emit_zmm512_ptr_store(Buffer& out, Register pointer, XmmRegister source, std::int32_t displacement,
                           bool wide, MaskRegister mask = MaskRegister::k0) {
    const auto src = static_cast<std::uint8_t>(source);
    const auto ptr = static_cast<std::uint8_t>(pointer);
    emit_evex(out, 1U, 2U, wide, 512U, src, 0U, ptr, mask, false); // VMOVDQU32/64 m512, zmm
    out.byte(0x7F);
    emit_ptr_modrm(out, src, pointer, displacement);
}

void emit_avx512_integer_binary(Buffer& out, XmmRegister destination, XmmRegister lhs, XmmRegister rhs,
                                std::uint8_t opcode, bool wide) {
    const auto dst = static_cast<std::uint8_t>(destination);
    const auto left = static_cast<std::uint8_t>(lhs);
    const auto right = static_cast<std::uint8_t>(rhs);
    emit_evex(out, 1U, 1U, wide, 512U, dst, left, right);
    out.byte(opcode);
    emit_modrm(out, 3, dst, right);
}

void emit_avx512_broadcast(Buffer& out, XmmRegister destination, XmmRegister source, bool wide) {
    const auto dst = static_cast<std::uint8_t>(destination);
    const auto src = static_cast<std::uint8_t>(source);
    emit_evex(out, 2U, 1U, wide, 512U, dst, 0U, src); // VPBROADCASTD/Q zmm, xmm
    out.byte(wide ? 0x59 : 0x58);
    emit_modrm(out, 3, dst, src);
}

void emit_kmovw(Buffer& out, MaskRegister destination, Register source) {
    const auto src = static_cast<std::uint8_t>(source);
    // KMOVW k, r32. Use VEX3 only when the GPR requires the B extension.
    if (src >= 8U) {
        emit_vex3(out, 1U, 0U, false, static_cast<std::uint8_t>(destination), 0U, src);
        out.byte(0x92);
        emit_modrm(out, 3, static_cast<std::uint8_t>(destination), src);
    } else {
        out.byte(0xC5); out.byte(0xF8); out.byte(0x92);
        emit_modrm(out, 3, static_cast<std::uint8_t>(destination), src);
    }
}

void emit_mask_lane_count(Buffer& out, MaskRegister destination, std::uint8_t lanes) {
    const auto bits = lanes >= 16U ? 0xffffU : static_cast<std::uint16_t>((1U << lanes) - 1U);
    // Preserve r11 even if register allocation happened to place a live pointer
    // there. The mask setup is therefore transparent to the surrounding pack.
    out.byte(0x41); out.byte(0x53);                 // push r11
    out.byte(0x41); out.byte(0xBB); out.i32(bits);  // mov r11d, imm32
    emit_kmovw(out, destination, Register::r11d);
    out.byte(0x41); out.byte(0x5B);                 // pop r11
}

[[maybe_unused]] void emit_vzeroupper(Buffer& out) {
    out.byte(0xC5); out.byte(0xF8); out.byte(0x77);
}
void emit_packed_ptr_load(Buffer& out, XmmRegister destination, Register pointer, std::int32_t displacement, std::int32_t bytes) {
    if (bytes == 64) emit_zmm512_ptr_load(out, destination, pointer, displacement, false);
    else if (bytes == 32) emit_ymm256_ptr_load(out, destination, pointer, displacement);
    else if (bytes == 8) emit_xmm64_ptr_load(out, destination, pointer, displacement);
    else emit_xmm128_ptr_load(out, destination, pointer, displacement);
}
void emit_packed_ptr_store(Buffer& out, Register pointer, XmmRegister source, std::int32_t displacement, std::int32_t bytes) {
    if (bytes == 64) emit_zmm512_ptr_store(out, pointer, source, displacement, false);
    else if (bytes == 32) emit_ymm256_ptr_store(out, pointer, source, displacement);
    else if (bytes == 8) emit_xmm64_ptr_store(out, pointer, source, displacement);
    else emit_xmm128_ptr_store(out, pointer, source, displacement);
}
void emit_xmm128_move(Buffer& out, XmmRegister destination, XmmRegister source) {
    out.byte(0x66); out.byte(0x0F); out.byte(0x6F);
    emit_modrm(out, 3, static_cast<std::uint8_t>(destination), static_cast<std::uint8_t>(source));
}
void emit_packed_move(Buffer& out, XmmRegister destination, XmmRegister source, std::int32_t bytes) {
    const auto dst = static_cast<std::uint8_t>(destination);
    const auto src = static_cast<std::uint8_t>(source);
    if (bytes == 64) {
        emit_evex(out, 1U, 1U, false, 512U, dst, 0U, src); // VMOVDQA32 zmm, zmm
        out.byte(0x6F);
        emit_modrm(out, 3, dst, src);
    } else if (bytes == 32) {
        emit_vex3(out, 1U, 1U, true, dst, 0U, src); // VMOVDQA ymm, ymm
        out.byte(0x6F);
        emit_modrm(out, 3, dst, src);
    } else {
        emit_xmm128_move(out, destination, source);
    }
}

void emit_sse2_integer_binary(Buffer& out, XmmRegister destination, XmmRegister source, std::uint8_t opcode);

void emit_packed_integer_binary(Buffer& out, XmmRegister destination, XmmRegister lhs, XmmRegister rhs,
                                std::uint8_t opcode, bool wide, std::int32_t bytes) {
    if (bytes == 64) emit_avx512_integer_binary(out, destination, lhs, rhs, opcode, wide);
    else if (bytes == 32) emit_avx2_integer_binary(out, destination, lhs, rhs, opcode);
    else {
        if (destination != lhs) emit_xmm128_move(out, destination, lhs);
        emit_sse2_integer_binary(out, destination, rhs, opcode);
    }
}
void emit_paddd(Buffer& out, XmmRegister destination, XmmRegister source) {
    out.byte(0x66); out.byte(0x0F); out.byte(0xFE);
    emit_modrm(out, 3, static_cast<std::uint8_t>(destination), static_cast<std::uint8_t>(source));
}
void emit_paddd_ptr(Buffer& out, XmmRegister destination, Register pointer, std::int32_t displacement = 0) {
    const auto dst = static_cast<std::uint8_t>(destination);
    const auto ptr = static_cast<std::uint8_t>(pointer);
    out.byte(0x66);
    if (dst >= 8U || ptr >= 8U) out.byte(static_cast<std::uint8_t>(0x40U | (dst >= 8U ? 0x04U : 0U) | (ptr >= 8U ? 0x01U : 0U)));
    out.byte(0x0F); out.byte(0xFE);
    emit_ptr_modrm(out, dst, pointer, displacement);
}
void emit_pshufd(Buffer& out, XmmRegister destination, XmmRegister source, std::uint8_t control) {
    out.byte(0x66); out.byte(0x0F); out.byte(0x70);
    emit_modrm(out, 3, static_cast<std::uint8_t>(destination), static_cast<std::uint8_t>(source));
    out.byte(control);
}
void emit_movd_xmm_to_gpr(Buffer& out, Register destination, XmmRegister source) {
    const auto dst = static_cast<std::uint8_t>(destination);
    const auto src = static_cast<std::uint8_t>(source);
    out.byte(0x66);
    if (src >= 8U || dst >= 8U) out.byte(static_cast<std::uint8_t>(0x40U | (src >= 8U ? 0x04U : 0U) | (dst >= 8U ? 0x01U : 0U)));
    out.byte(0x0F); out.byte(0x7E);
    emit_modrm(out, 3, src, dst);
}
void emit_paddq(Buffer& out, XmmRegister destination, XmmRegister source) {
    out.byte(0x66); out.byte(0x0F); out.byte(0xD4);
    emit_modrm(out, 3, static_cast<std::uint8_t>(destination), static_cast<std::uint8_t>(source));
}
void emit_paddq_ptr(Buffer& out, XmmRegister destination, Register pointer, std::int32_t displacement = 0) {
    const auto dst = static_cast<std::uint8_t>(destination);
    const auto ptr = static_cast<std::uint8_t>(pointer);
    out.byte(0x66);
    if (dst >= 8U || ptr >= 8U) out.byte(static_cast<std::uint8_t>(0x40U | (dst >= 8U ? 0x04U : 0U) | (ptr >= 8U ? 0x01U : 0U)));
    out.byte(0x0F); out.byte(0xD4);
    emit_ptr_modrm(out, dst, pointer, displacement);
}
void emit_sse2_integer_binary(Buffer& out, XmmRegister destination, XmmRegister source, std::uint8_t opcode) {
    out.byte(0x66); out.byte(0x0F); out.byte(opcode);
    emit_modrm(out, 3, static_cast<std::uint8_t>(destination), static_cast<std::uint8_t>(source));
}
void emit_packed_integer_binary(Buffer& out, XmmRegister destination, XmmRegister source, std::uint8_t opcode, std::int32_t bytes) {
    const bool wide = opcode == 0xD4U || opcode == 0xFBU;
    if (bytes == 64) emit_avx512_integer_binary(out, destination, destination, source, opcode, wide);
    else if (bytes == 32) emit_avx2_integer_binary(out, destination, destination, source, opcode);
    else emit_sse2_integer_binary(out, destination, source, opcode);
}
void emit_movq_gpr_to_xmm(Buffer& out, XmmRegister destination, Register source) {
    const auto dst = static_cast<std::uint8_t>(destination);
    const auto src = static_cast<std::uint8_t>(source);
    out.byte(0x66);
    out.byte(static_cast<std::uint8_t>(0x48U | (dst >= 8U ? 0x04U : 0U) | (src >= 8U ? 0x01U : 0U)));
    out.byte(0x0F); out.byte(0x6E);
    emit_modrm(out, 3, dst, src);
}
void emit_movq_xmm_to_gpr(Buffer& out, Register destination, XmmRegister source) {
    const auto dst = static_cast<std::uint8_t>(destination);
    const auto src = static_cast<std::uint8_t>(source);
    out.byte(0x66);
    out.byte(static_cast<std::uint8_t>(0x48U | (src >= 8U ? 0x04U : 0U) | (dst >= 8U ? 0x01U : 0U)));
    out.byte(0x0F); out.byte(0x7E);
    emit_modrm(out, 3, src, dst);
}

[[maybe_unused]] void emit_sse_rip_load_placeholder(Buffer& out, XmmRegister destination, bool wide) {
    const auto dst = static_cast<std::uint8_t>(destination);
    out.byte(wide ? 0xF2 : 0xF3);
    if (dst >= 8U) out.byte(0x44);
    out.byte(0x0F); out.byte(0x10);
    emit_modrm(out, 0, dst, 5); // [rip + disp32]
    out.i32(0);
}

[[maybe_unused]] void emit_sse_binary_ptr(Buffer& out, XmmRegister destination, Register pointer, bool wide,
                         std::uint8_t opcode, std::int32_t displacement = 0) {
    const auto dst = static_cast<std::uint8_t>(destination);
    const auto ptr = static_cast<std::uint8_t>(pointer);
    out.byte(wide ? 0xF2 : 0xF3);
    if (dst >= 8U || ptr >= 8U)
        out.byte(static_cast<std::uint8_t>(0x40U | (dst >= 8U ? 0x04U : 0U) | (ptr >= 8U ? 0x01U : 0U)));
    out.byte(0x0F); out.byte(opcode);
    emit_ptr_modrm(out, dst, pointer, displacement);
}

void emit_sse_ptr_load(Buffer& out, XmmRegister destination, Register pointer, bool wide, std::int32_t displacement = 0) {
    const auto dst = static_cast<std::uint8_t>(destination);
    const auto ptr = static_cast<std::uint8_t>(pointer);
    out.byte(wide ? 0xF2 : 0xF3);
    if (dst >= 8U || ptr >= 8U) out.byte(static_cast<std::uint8_t>(0x40U | (dst >= 8U ? 0x04U : 0U) | (ptr >= 8U ? 0x01U : 0U)));
    out.byte(0x0F); out.byte(0x10);
    emit_ptr_modrm(out, dst, pointer, displacement);
}

void emit_sse_ptr_store(Buffer& out, Register pointer, XmmRegister source, bool wide, std::int32_t displacement = 0) {
    const auto src = static_cast<std::uint8_t>(source);
    const auto ptr = static_cast<std::uint8_t>(pointer);
    out.byte(wide ? 0xF2 : 0xF3);
    if (src >= 8U || ptr >= 8U) out.byte(static_cast<std::uint8_t>(0x40U | (src >= 8U ? 0x04U : 0U) | (ptr >= 8U ? 0x01U : 0U)));
    out.byte(0x0F); out.byte(0x11);
    emit_ptr_modrm(out, src, pointer, displacement);
}

void emit_sse_xor(Buffer& out, XmmRegister destination, XmmRegister source, bool wide) {
    if (wide) out.byte(0x66);
    out.byte(0x0F); out.byte(0x57);
    emit_modrm(out, 3, static_cast<std::uint8_t>(destination), static_cast<std::uint8_t>(source));
}

void emit_sse_binary(Buffer& out, XmmRegister destination, XmmRegister source, bool wide, std::uint8_t opcode) {
    out.byte(wide ? 0xF2 : 0xF3); out.byte(0x0F); out.byte(opcode);
    emit_modrm(out, 3, static_cast<std::uint8_t>(destination), static_cast<std::uint8_t>(source));
}

void emit_ucomi(Buffer& out, XmmRegister left, XmmRegister right, bool wide) {
    if (wide) out.byte(0x66);
    out.byte(0x0F); out.byte(0x2E);
    emit_modrm(out, 3, static_cast<std::uint8_t>(left), static_cast<std::uint8_t>(right));
}

void emit_mov_gpr_to_xmm(Buffer& out, XmmRegister destination, Register source, bool wide) {
    out.byte(0x66);
    if (wide) out.byte(0x48);
    out.byte(0x0F); out.byte(0x6E);
    emit_modrm(out, 3, static_cast<std::uint8_t>(destination), static_cast<std::uint8_t>(source));
}

void emit_int_to_float(Buffer& out, XmmRegister destination, Register source, bool source_wide, bool result_wide) {
    out.byte(result_wide ? 0xF2 : 0xF3);
    if (source_wide) out.byte(0x48);
    out.byte(0x0F); out.byte(0x2A);
    emit_modrm(out, 3, static_cast<std::uint8_t>(destination), static_cast<std::uint8_t>(source));
}

void emit_float_to_int(Buffer& out, Register destination, XmmRegister source, bool result_wide, bool source_wide) {
    out.byte(source_wide ? 0xF2 : 0xF3);
    if (result_wide) out.byte(0x48);
    out.byte(0x0F); out.byte(0x2C);
    emit_modrm(out, 3, static_cast<std::uint8_t>(destination), static_cast<std::uint8_t>(source));
}

void emit_float_convert(Buffer& out, XmmRegister destination, XmmRegister source, bool source_wide) {
    out.byte(source_wide ? 0xF2 : 0xF3);
    out.byte(0x0F); out.byte(0x5A);
    emit_modrm(out, 3, static_cast<std::uint8_t>(destination), static_cast<std::uint8_t>(source));
}

void emit_sign_extend_eax(Buffer& out, unsigned bits) {
    if (bits == 8U) { out.byte(0x0F); out.byte(0xBE); out.byte(0xC0); }
    else if (bits == 16U) { out.byte(0x0F); out.byte(0xBF); out.byte(0xC0); }
}

void emit_sse_rsp_load(Buffer& out, XmmRegister destination, std::int32_t displacement, bool wide) {
    out.byte(wide ? 0xF2 : 0xF3); out.byte(0x0F); out.byte(0x10);
    emit_modrm(out, 2, static_cast<std::uint8_t>(destination), static_cast<std::uint8_t>(Register::esp));
    out.byte(0x24);
    out.i32(displacement);
}

void emit_sse_rsp_store(Buffer& out, XmmRegister source, std::int32_t displacement, bool wide) {
    out.byte(wide ? 0xF2 : 0xF3); out.byte(0x0F); out.byte(0x11);
    emit_modrm(out, 2, static_cast<std::uint8_t>(source), static_cast<std::uint8_t>(Register::esp));
    out.byte(0x24);
    out.i32(displacement);
}

void emit_rex(Buffer& out, bool r, bool b) {
    if (r || b) out.byte(static_cast<std::uint8_t>(0x40U | (r ? 0x04U : 0U) | (b ? 0x01U : 0U)));
}

void emit_modrm(Buffer& out, std::uint8_t mod, std::uint8_t reg, std::uint8_t rm) {
    out.byte(static_cast<std::uint8_t>((mod << 6U) | ((reg & 7U) << 3U) | (rm & 7U)));
}

void emit_load_stack_i8(Buffer& out, Register destination, std::int32_t displacement) {
    const auto reg = static_cast<std::uint8_t>(destination);
    emit_rex(out, reg >= 8, false);
    out.byte(0x0F); out.byte(0xB6);
    emit_modrm(out, 2, reg, static_cast<std::uint8_t>(Register::ebp));
    out.i32(displacement);
}

void emit_load_stack_i16(Buffer& out, Register destination, std::int32_t displacement) {
    const auto reg = static_cast<std::uint8_t>(destination);
    emit_rex(out, reg >= 8, false);
    out.byte(0x0F); out.byte(0xB7);
    emit_modrm(out, 2, reg, static_cast<std::uint8_t>(Register::ebp));
    out.i32(displacement);
}

void emit_store_stack_immediate(Buffer& out, std::uint32_t immediate, std::int32_t displacement, unsigned width) {
    if (width == 2U) out.byte(0x66);
    if (width == 8U) out.byte(0x48);
    out.byte(width == 1U ? 0xC6 : 0xC7);
    emit_modrm(out, 2, 0, static_cast<std::uint8_t>(Register::ebp));
    out.i32(displacement);
    if (width == 1U) out.byte(static_cast<std::uint8_t>(immediate));
    else if (width == 2U) {
        out.byte(static_cast<std::uint8_t>(immediate));
        out.byte(static_cast<std::uint8_t>(immediate >> 8U));
    } else out.i32(static_cast<std::int32_t>(immediate));
}

void emit_store_stack_i8(Buffer& out, Register source, std::int32_t displacement) {
    const auto reg = static_cast<std::uint8_t>(source);
    emit_rex(out, reg >= 8, false);
    out.byte(0x88);
    emit_modrm(out, 2, reg, static_cast<std::uint8_t>(Register::ebp));
    out.i32(displacement);
}

void emit_store_stack_i16(Buffer& out, Register source, std::int32_t displacement) {
    const auto reg = static_cast<std::uint8_t>(source);
    out.byte(0x66);
    emit_rex(out, reg >= 8, false);
    out.byte(0x89);
    emit_modrm(out, 2, reg, static_cast<std::uint8_t>(Register::ebp));
    out.i32(displacement);
}

void emit_store_stack(Buffer& out, Register source, std::int32_t displacement) {
    const auto reg = static_cast<std::uint8_t>(source);
    emit_rex(out, reg >= 8, false);
    out.byte(0x89);
    emit_modrm(out, 2, reg, static_cast<std::uint8_t>(Register::ebp));
    out.i32(displacement);
}

void emit_load_stack(Buffer& out, Register destination, std::int32_t displacement) {
    const auto reg = static_cast<std::uint8_t>(destination);
    emit_rex(out, reg >= 8, false);
    out.byte(0x8B);
    emit_modrm(out, 2, reg, static_cast<std::uint8_t>(Register::ebp));
    out.i32(displacement);
}

void emit_store_stack64(Buffer& out, Register source, std::int32_t displacement) {
    const auto reg = static_cast<std::uint8_t>(source);
    out.byte(static_cast<std::uint8_t>(0x48U | (reg >= 8 ? 0x04U : 0U)));
    out.byte(0x89);
    emit_modrm(out, 2, reg, static_cast<std::uint8_t>(Register::ebp));
    out.i32(displacement);
}

void emit_load_stack64(Buffer& out, Register destination, std::int32_t displacement) {
    const auto reg = static_cast<std::uint8_t>(destination);
    out.byte(static_cast<std::uint8_t>(0x48U | (reg >= 8 ? 0x04U : 0U)));
    out.byte(0x8B);
    emit_modrm(out, 2, reg, static_cast<std::uint8_t>(Register::ebp));
    out.i32(displacement);
}

void emit_load_ptr_i8(Buffer& out, Register destination, Register pointer, std::int32_t displacement = 0) {
    const auto dst = static_cast<std::uint8_t>(destination);
    const auto ptr = static_cast<std::uint8_t>(pointer);
    emit_rex(out, dst >= 8, ptr >= 8);
    out.byte(0x0F); out.byte(0xB6);
    emit_ptr_modrm(out, dst, pointer, displacement);
}

void emit_load_ptr_i16(Buffer& out, Register destination, Register pointer, std::int32_t displacement = 0) {
    const auto dst = static_cast<std::uint8_t>(destination);
    const auto ptr = static_cast<std::uint8_t>(pointer);
    emit_rex(out, dst >= 8, ptr >= 8);
    out.byte(0x0F); out.byte(0xB7);
    emit_ptr_modrm(out, dst, pointer, displacement);
}

void emit_store_ptr_immediate(Buffer& out, Register pointer, std::uint32_t immediate,
                              std::int32_t displacement, unsigned width) {
    const auto ptr = static_cast<std::uint8_t>(pointer);
    if (width == 2U) out.byte(0x66);
    if (width == 8U) out.byte(static_cast<std::uint8_t>(0x48U | (ptr >= 8 ? 0x01U : 0U)));
    else emit_rex(out, false, ptr >= 8);
    out.byte(width == 1U ? 0xC6 : 0xC7);
    emit_ptr_modrm(out, 0, pointer, displacement);
    if (width == 1U) out.byte(static_cast<std::uint8_t>(immediate));
    else if (width == 2U) {
        out.byte(static_cast<std::uint8_t>(immediate));
        out.byte(static_cast<std::uint8_t>(immediate >> 8U));
    } else out.i32(static_cast<std::int32_t>(immediate));
}

void emit_store_ptr_i8(Buffer& out, Register pointer, Register source, std::int32_t displacement = 0) {
    const auto src = static_cast<std::uint8_t>(source);
    const auto ptr = static_cast<std::uint8_t>(pointer);
    emit_rex(out, src >= 8, ptr >= 8);
    out.byte(0x88);
    emit_ptr_modrm(out, src, pointer, displacement);
}

void emit_store_ptr_i16(Buffer& out, Register pointer, Register source, std::int32_t displacement = 0) {
    const auto src = static_cast<std::uint8_t>(source);
    const auto ptr = static_cast<std::uint8_t>(pointer);
    out.byte(0x66);
    emit_rex(out, src >= 8, ptr >= 8);
    out.byte(0x89);
    emit_ptr_modrm(out, src, pointer, displacement);
}

void emit_load_ptr_i32(Buffer& out, Register destination, Register pointer, std::int32_t displacement = 0) {
    const auto dst = static_cast<std::uint8_t>(destination);
    const auto ptr = static_cast<std::uint8_t>(pointer);
    emit_rex(out, dst >= 8, ptr >= 8);
    out.byte(0x8B);
    emit_ptr_modrm(out, dst, pointer, displacement);
}

void emit_store_ptr_i32(Buffer& out, Register pointer, Register source, std::int32_t displacement = 0) {
    const auto src = static_cast<std::uint8_t>(source);
    const auto ptr = static_cast<std::uint8_t>(pointer);
    emit_rex(out, src >= 8, ptr >= 8);
    out.byte(0x89);
    emit_ptr_modrm(out, src, pointer, displacement);
}

void emit_load_ptr_i64(Buffer& out, Register destination, Register pointer, std::int32_t displacement = 0) {
    const auto dst = static_cast<std::uint8_t>(destination);
    const auto ptr = static_cast<std::uint8_t>(pointer);
    out.byte(static_cast<std::uint8_t>(0x48U | (dst >= 8 ? 0x04U : 0U) | (ptr >= 8 ? 0x01U : 0U)));
    out.byte(0x8B);
    emit_ptr_modrm(out, dst, pointer, displacement);
}

void emit_store_ptr_i64(Buffer& out, Register pointer, Register source, std::int32_t displacement = 0) {
    const auto src = static_cast<std::uint8_t>(source);
    const auto ptr = static_cast<std::uint8_t>(pointer);
    out.byte(static_cast<std::uint8_t>(0x48U | (src >= 8 ? 0x04U : 0U) | (ptr >= 8 ? 0x01U : 0U)));
    out.byte(0x89);
    emit_ptr_modrm(out, src, pointer, displacement);
}

void emit_add_imm64(Buffer& out, Register destination, std::int32_t immediate) {
    const auto reg = static_cast<std::uint8_t>(destination);
    out.byte(static_cast<std::uint8_t>(0x48U | (reg >= 8 ? 0x01U : 0U)));
    out.byte(0x81);
    emit_modrm(out, 3, 0, reg);
    out.i32(immediate);
}

void emit_lea_sum(Buffer& out, Register destination, Register left, Register right, bool wide) {
    const auto dst = static_cast<std::uint8_t>(destination);
    const auto base = static_cast<std::uint8_t>(left);
    const auto index = static_cast<std::uint8_t>(right);
    out.byte(static_cast<std::uint8_t>((wide ? 0x48U : 0x40U) |
        (dst >= 8U ? 0x04U : 0U) | (index >= 8U ? 0x02U : 0U) | (base >= 8U ? 0x01U : 0U)));
    out.byte(0x8D);
    const bool base_needs_displacement = (base & 7U) == 5U;
    emit_modrm(out, base_needs_displacement ? 1U : 0U, dst, 4U);
    out.byte(static_cast<std::uint8_t>(((index & 7U) << 3U) | (base & 7U)));
    if (base_needs_displacement) out.byte(0);
}

void emit_lea_scaled_self(Buffer& out, Register destination, Register source,
                          unsigned scale_log2, bool wide) {
    const auto dst = static_cast<std::uint8_t>(destination);
    const auto src = static_cast<std::uint8_t>(source);
    out.byte(static_cast<std::uint8_t>((wide ? 0x48U : 0x40U) |
        (dst >= 8U ? 0x04U : 0U) | (src >= 8U ? 0x02U : 0U) |
        (src >= 8U ? 0x01U : 0U)));
    out.byte(0x8D);
    const bool base_needs_displacement = (src & 7U) == 5U;
    emit_modrm(out, base_needs_displacement ? 1U : 0U, dst, 4U);
    out.byte(static_cast<std::uint8_t>((scale_log2 << 6U) |
                                      ((src & 7U) << 3U) | (src & 7U)));
    if (base_needs_displacement) out.byte(0);
}

void emit_lea_offset(Buffer& out, Register destination, Register source, std::int32_t displacement, bool wide) {
    const auto dst = static_cast<std::uint8_t>(destination);
    const auto base = static_cast<std::uint8_t>(source);
    out.byte(static_cast<std::uint8_t>((wide ? 0x48U : 0x40U) |
        (dst >= 8U ? 0x04U : 0U) | (base >= 8U ? 0x01U : 0U)));
    out.byte(0x8D);
    const bool short_displacement = displacement >= -128 && displacement <= 127;
    emit_modrm(out, short_displacement ? 1U : 2U, dst, base);
    if ((base & 7U) == 4U) out.byte(0x24);
    if (short_displacement) out.byte(static_cast<std::uint8_t>(displacement));
    else out.i32(displacement);
}

void emit_integer_immediate_binary(Buffer& out, machine::Opcode opcode, Register destination,
                                   std::int32_t immediate, bool wide) {
    const auto reg = static_cast<std::uint8_t>(destination);
    if (wide) out.byte(static_cast<std::uint8_t>(0x48U | (reg >= 8 ? 0x01U : 0U)));
    else if (reg >= 8) out.byte(0x41);
    if (opcode == machine::Opcode::mul_i32 || opcode == machine::Opcode::mul_i64) {
        out.byte(immediate >= -128 && immediate <= 127 ? 0x6B : 0x69);
        emit_modrm(out, 3, reg, reg);
        if (immediate >= -128 && immediate <= 127) out.byte(static_cast<std::uint8_t>(immediate));
        else out.i32(immediate);
        return;
    }
    std::uint8_t extension = 0;
    switch (opcode) {
    case machine::Opcode::add_i32: case machine::Opcode::add_i64: extension = 0; break;
    case machine::Opcode::or_i32: case machine::Opcode::or_i64: extension = 1; break;
    case machine::Opcode::and_i32: case machine::Opcode::and_i64: extension = 4; break;
    case machine::Opcode::sub_i32: case machine::Opcode::sub_i64: extension = 5; break;
    case machine::Opcode::xor_i32: case machine::Opcode::xor_i64: extension = 6; break;
    default: break;
    }
    const bool short_form = immediate >= -128 && immediate <= 127;
    out.byte(short_form ? 0x83 : 0x81);
    emit_modrm(out, 3, extension, reg);
    if (short_form) out.byte(static_cast<std::uint8_t>(immediate));
    else out.i32(immediate);
}

void emit_shift_immediate(Buffer& out, machine::Opcode opcode, Register destination,
                          std::uint8_t immediate, bool wide) {
    const auto reg = static_cast<std::uint8_t>(destination);
    if (wide) out.byte(static_cast<std::uint8_t>(0x48U | (reg >= 8 ? 0x01U : 0U)));
    else if (reg >= 8) out.byte(0x41);
    out.byte(0xC1);
    const std::uint8_t extension = (opcode == machine::Opcode::shl_i32 || opcode == machine::Opcode::shl_i64) ? std::uint8_t{4} :
                                   (opcode == machine::Opcode::shr_u_i32 || opcode == machine::Opcode::shr_u_i64) ? std::uint8_t{5} : std::uint8_t{7};
    emit_modrm(out, 3, extension, reg);
    out.byte(immediate);
}

void emit_store_rsp(Buffer& out, Register source, std::int32_t displacement) {
    const auto reg = static_cast<std::uint8_t>(source);
    emit_rex(out, reg >= 8, false);
    out.byte(0x89);
    emit_modrm(out, 2, reg, static_cast<std::uint8_t>(Register::esp));
    out.byte(0x24); // SIB: [rsp + disp32]
    out.i32(displacement);
}

void emit_load_rsp(Buffer& out, Register destination, std::int32_t displacement) {
    const auto reg = static_cast<std::uint8_t>(destination);
    emit_rex(out, reg >= 8, false);
    out.byte(0x8B);
    emit_modrm(out, 2, reg, static_cast<std::uint8_t>(Register::esp));
    out.byte(0x24);
    out.i32(displacement);
}

void emit_load_rsp64(Buffer& out, Register destination, std::int32_t displacement) {
    const auto reg = static_cast<std::uint8_t>(destination);
    out.byte(static_cast<std::uint8_t>(0x48U | (reg >= 8 ? 0x04U : 0U)));
    out.byte(0x8B);
    emit_modrm(out, 2, reg, static_cast<std::uint8_t>(Register::esp));
    out.byte(0x24);
    out.i32(displacement);
}

void emit_store_rsp64(Buffer& out, Register source, std::int32_t displacement) {
    const auto reg = static_cast<std::uint8_t>(source);
    out.byte(static_cast<std::uint8_t>(0x48U | (reg >= 8 ? 0x04U : 0U)));
    out.byte(0x89);
    emit_modrm(out, 2, reg, static_cast<std::uint8_t>(Register::esp));
    out.byte(0x24);
    out.i32(displacement);
}

void emit_adjust_rsp(Buffer& out, bool subtract, std::uint32_t amount) {
    if (amount == 0) return;
    out.byte(0x48); out.byte(0x81); out.byte(subtract ? 0xEC : 0xC4);
    out.i32(static_cast<std::int32_t>(amount));
}

Register physical_register(machine::PhysicalRegister physical) {
    switch (physical) {
    case machine::PhysicalRegister::r8d: return Register::r8d;
    case machine::PhysicalRegister::r9d: return Register::r9d;
    case machine::PhysicalRegister::ebx: return Register::ebx;
    case machine::PhysicalRegister::r10d: return Register::r10d;
    case machine::PhysicalRegister::r11d: return Register::r11d;
    case machine::PhysicalRegister::r12d: return Register::r12d;
    case machine::PhysicalRegister::r13d: return Register::r13d;
    case machine::PhysicalRegister::r14d: return Register::r14d;
    case machine::PhysicalRegister::r15d: return Register::r15d;
    }
    return Register::r10d;
}

void emit_mov_register(Buffer& out, Register destination, Register source) {
    if (destination == source) return;
    const auto dst = static_cast<std::uint8_t>(destination);
    const auto src = static_cast<std::uint8_t>(source);
    emit_rex(out, src >= 8, dst >= 8);
    out.byte(0x89);
    emit_modrm(out, 3, src, dst);
}

void emit_cmov_register(Buffer& out, Register destination, Register source,
                        bool wide, bool when_nonzero) {
    const auto dst = static_cast<std::uint8_t>(destination);
    const auto src = static_cast<std::uint8_t>(source);
    const auto rex = static_cast<std::uint8_t>((wide ? 0x48U : 0x40U) |
        (dst >= 8U ? 0x04U : 0U) | (src >= 8U ? 0x01U : 0U));
    if (rex != 0x40U) out.byte(rex);
    out.byte(0x0F);
    out.byte(when_nonzero ? 0x45 : 0x44);
    emit_modrm(out, 3, dst, src);
}

void emit_mov_imm32(Buffer& out, Register destination, std::int32_t immediate);
void emit_mov_imm64(Buffer& out, Register destination, std::int64_t immediate);

void emit_read_location(Buffer& out, Register destination,
                        const machine::AllocationLocation& location) {
    if (location.kind == machine::LocationKind::physical_register)
        emit_mov_register(out, destination, physical_register(location.physical));
    else if (location.kind == machine::LocationKind::rematerialized_integer)
        emit_mov_imm32(out, destination, static_cast<std::int32_t>(location.rematerialized_immediate));
    else {
        out.record_spill_load();
        emit_load_stack(out, destination, location.stack_offset);
    }
}

void emit_write_location(Buffer& out, const machine::AllocationLocation& location,
                         Register source) {
    if (location.kind == machine::LocationKind::physical_register)
        emit_mov_register(out, physical_register(location.physical), source);
    else if (location.kind == machine::LocationKind::rematerialized_integer)
        return;
    else {
        out.record_spill_store();
        emit_store_stack(out, source, location.stack_offset);
    }
}

void emit_cmp_registers(Buffer& out, Register left, Register right, bool wide) {
    const auto lhs = static_cast<std::uint8_t>(left);
    const auto rhs = static_cast<std::uint8_t>(right);
    const auto rex = static_cast<std::uint8_t>((wide ? 0x48U : 0x40U) |
        (rhs >= 8U ? 0x04U : 0U) | (lhs >= 8U ? 0x01U : 0U));
    if (rex != 0x40U) out.byte(rex);
    out.byte(0x39); // cmp r/m, reg
    emit_modrm(out, 3, rhs, lhs);
}

void emit_cmp_register_immediate(Buffer& out, Register left, std::int32_t value, bool wide) {
    const auto lhs = static_cast<std::uint8_t>(left);
    const auto rex = static_cast<std::uint8_t>((wide ? 0x48U : 0x40U) | (lhs >= 8U ? 0x01U : 0U));
    if (rex != 0x40U) out.byte(rex);
    if (value >= -128 && value <= 127) {
        out.byte(0x83);
        emit_modrm(out, 3, 7, lhs);
        out.byte(static_cast<std::uint8_t>(value));
    } else {
        out.byte(0x81);
        emit_modrm(out, 3, 7, lhs);
        out.i32(value);
    }
}

void emit_test_register_immediate(Buffer& out, Register source, std::int32_t value, bool wide) {
    const auto reg = static_cast<std::uint8_t>(source);
    const auto rex = static_cast<std::uint8_t>((wide ? 0x48U : 0x40U) | (reg >= 8U ? 0x01U : 0U));
    if (rex != 0x40U) out.byte(rex);
    out.byte(0xF7); // test r/m32|64, imm32
    emit_modrm(out, 3, 0, reg);
    out.i32(value);
}

void emit_mov_register64(Buffer& out, Register destination, Register source) {
    if (destination == source) return;
    const auto dst = static_cast<std::uint8_t>(destination);
    const auto src = static_cast<std::uint8_t>(source);
    out.byte(static_cast<std::uint8_t>(0x48U | (src >= 8 ? 0x04U : 0U) | (dst >= 8 ? 0x01U : 0U)));
    out.byte(0x89);
    emit_modrm(out, 3, src, dst);
}

void emit_xchg_registers(Buffer& out, Register left, Register right, bool wide) {
    if (left == right) return;
    const auto lhs = static_cast<std::uint8_t>(left);
    const auto rhs = static_cast<std::uint8_t>(right);
    const auto rex = static_cast<std::uint8_t>((wide ? 0x48U : 0x40U) |
        (rhs >= 8U ? 0x04U : 0U) | (lhs >= 8U ? 0x01U : 0U));
    if (rex != 0x40U) out.byte(rex);
    out.byte(0x87); // xchg r/m, reg
    emit_modrm(out, 3, rhs, lhs);
}

void emit_read_location64(Buffer& out, Register destination,
                          const machine::AllocationLocation& location) {
    if (location.kind == machine::LocationKind::physical_register)
        emit_mov_register64(out, destination, physical_register(location.physical));
    else if (location.kind == machine::LocationKind::rematerialized_integer)
        emit_mov_imm64(out, destination, location.rematerialized_immediate);
    else {
        out.record_spill_load();
        emit_load_stack64(out, destination, location.stack_offset);
    }
}

void emit_write_location64(Buffer& out, const machine::AllocationLocation& location,
                           Register source) {
    if (location.kind == machine::LocationKind::physical_register)
        emit_mov_register64(out, physical_register(location.physical), source);
    else if (location.kind == machine::LocationKind::rematerialized_integer)
        return;
    else {
        out.record_spill_store();
        emit_store_stack64(out, source, location.stack_offset);
    }
}

void emit_read_float_location(Buffer& out, XmmRegister destination,
                              const machine::AllocationLocation& location, bool wide) {
    if (location.kind == machine::LocationKind::floating_register)
        emit_sse_move(out, destination, floating_register(location.floating), wide);
    else if (location.kind == machine::LocationKind::rematerialized_floating) {
        if (wide) emit_mov_imm64(out, Register::eax, location.rematerialized_immediate);
        else emit_mov_imm32(out, Register::eax, static_cast<std::int32_t>(location.rematerialized_immediate));
        emit_mov_gpr_to_xmm(out, destination, Register::eax, wide);
    } else {
        out.record_spill_load();
        emit_sse_stack_load(out, destination, location.stack_offset, wide);
    }
}

void emit_write_float_location(Buffer& out, const machine::AllocationLocation& location,
                               XmmRegister source, bool wide) {
    if (location.kind == machine::LocationKind::floating_register)
        emit_sse_move(out, floating_register(location.floating), source, wide);
    else if (location.kind == machine::LocationKind::rematerialized_floating)
        return;
    else {
        out.record_spill_store();
        emit_sse_stack_store(out, source, location.stack_offset, wide);
    }
}

bool same_location(const machine::AllocationLocation& left, const machine::AllocationLocation& right) {
    if (left.kind != right.kind) return false;
    switch (left.kind) {
    case machine::LocationKind::physical_register: return left.physical == right.physical;
    case machine::LocationKind::floating_register: return left.floating == right.floating;
    case machine::LocationKind::stack_slot: return left.stack_offset == right.stack_offset;
    case machine::LocationKind::rematerialized_integer:
    case machine::LocationKind::rematerialized_floating:
        return left.rematerialized_immediate == right.rematerialized_immediate;
    }
    return false;
}

void emit_mov_imm64(Buffer& out, Register destination, std::int64_t immediate) {
    const auto reg = static_cast<std::uint8_t>(destination);
    if (immediate == 0) {
        emit_rex(out, reg >= 8U, reg >= 8U);
        out.byte(0x31); // xor r32, r32; zero-extends to 64 bits
        emit_modrm(out, 3, reg, reg);
        return;
    }
    if (immediate >= 0 && static_cast<std::uint64_t>(immediate) <= std::numeric_limits<std::uint32_t>::max()) {
        emit_mov_imm32(out, destination, static_cast<std::int32_t>(static_cast<std::uint32_t>(immediate)));
        return;
    }
    if (immediate >= std::numeric_limits<std::int32_t>::min() && immediate <= std::numeric_limits<std::int32_t>::max()) {
        out.byte(static_cast<std::uint8_t>(0x48U | (reg >= 8U ? 0x01U : 0U)));
        out.byte(0xC7);
        emit_modrm(out, 3, 0, reg);
        out.i32(static_cast<std::int32_t>(immediate));
        return;
    }
    out.byte(static_cast<std::uint8_t>(0x48U | (reg >= 8U ? 0x01U : 0U)));
    out.byte(static_cast<std::uint8_t>(0xB8U + (reg & 7U)));
    out.i64(immediate);
}

void emit_mov_imm32(Buffer& out, Register destination, std::int32_t immediate) {
    const auto reg = static_cast<std::uint8_t>(destination);
    emit_rex(out, false, reg >= 8);
    out.byte(static_cast<std::uint8_t>(0xB8U + (reg & 7U)));
    out.i32(immediate);
}

void emit_integer_stack_binary(Buffer& out, machine::Opcode opcode, Register destination,
                               std::int32_t displacement, bool wide) {
    const auto dst = static_cast<std::uint8_t>(destination);
    if (wide) out.byte(static_cast<std::uint8_t>(0x48U | (dst >= 8 ? 0x04U : 0U)));
    else if (dst >= 8) out.byte(0x44);
    if (opcode == machine::Opcode::mul_i32 || opcode == machine::Opcode::mul_i64) {
        out.byte(0x0F); out.byte(0xAF);
    } else {
        const std::uint8_t byte = (opcode == machine::Opcode::add_i32 || opcode == machine::Opcode::add_i64) ? 0x03 :
                          (opcode == machine::Opcode::sub_i32 || opcode == machine::Opcode::sub_i64) ? 0x2B :
                          (opcode == machine::Opcode::and_i32 || opcode == machine::Opcode::and_i64) ? 0x23 :
                          (opcode == machine::Opcode::or_i32 || opcode == machine::Opcode::or_i64) ? 0x0B : 0x33;
        out.byte(byte);
    }
    emit_modrm(out, 2, dst, static_cast<std::uint8_t>(Register::ebp));
    out.i32(displacement);
}

void emit_binary(Buffer& out, machine::Opcode opcode) {
    switch (opcode) {
    case machine::Opcode::add_i32: out.byte(0x01); out.byte(0xC8); break;
    case machine::Opcode::sub_i32: out.byte(0x29); out.byte(0xC8); break;
    case machine::Opcode::mul_i32: out.byte(0x0F); out.byte(0xAF); out.byte(0xC1); break;
    case machine::Opcode::and_i32: out.byte(0x21); out.byte(0xC8); break;
    case machine::Opcode::or_i32: out.byte(0x09); out.byte(0xC8); break;
    case machine::Opcode::xor_i32: out.byte(0x31); out.byte(0xC8); break;
    default: break;
    }
}

void emit_two_address_binary(Buffer& out, machine::Opcode opcode, Register destination, Register source, bool wide) {
    const auto destination_code = static_cast<std::uint8_t>(destination);
    const auto source_code = static_cast<std::uint8_t>(source);
    if (opcode == machine::Opcode::mul_i32 || opcode == machine::Opcode::mul_i64) {
        if (wide) out.byte(static_cast<std::uint8_t>(0x48U | (destination_code >= 8 ? 0x04U : 0U) |
                                                     (source_code >= 8 ? 0x01U : 0U)));
        else emit_rex(out, destination_code >= 8, source_code >= 8);
        out.byte(0x0F); out.byte(0xAF);
        emit_modrm(out, 3, destination_code, source_code);
        return;
    }
    if (wide) out.byte(static_cast<std::uint8_t>(0x48U | (source_code >= 8 ? 0x04U : 0U) |
                                                 (destination_code >= 8 ? 0x01U : 0U)));
    else emit_rex(out, source_code >= 8, destination_code >= 8);
    const std::uint8_t byte = (opcode == machine::Opcode::add_i32 || opcode == machine::Opcode::add_i64) ? 0x01 :
                      (opcode == machine::Opcode::sub_i32 || opcode == machine::Opcode::sub_i64) ? 0x29 :
                      (opcode == machine::Opcode::and_i32 || opcode == machine::Opcode::and_i64) ? 0x21 :
                      (opcode == machine::Opcode::or_i32 || opcode == machine::Opcode::or_i64) ? 0x09 : 0x31;
    out.byte(byte);
    emit_modrm(out, 3, source_code, destination_code);
}

std::uint8_t condition_code(machine::Opcode opcode) {
    switch (opcode) {
    case machine::Opcode::cmp_eq_i32:
    case machine::Opcode::cmp_eq_i64: return 0x94;
    case machine::Opcode::cmp_ne_i32:
    case machine::Opcode::cmp_ne_i64: return 0x95;
    case machine::Opcode::cmp_lt_i32:
    case machine::Opcode::cmp_lt_i64: return 0x9C;
    case machine::Opcode::cmp_le_i32:
    case machine::Opcode::cmp_le_i64: return 0x9E;
    case machine::Opcode::cmp_gt_i32:
    case machine::Opcode::cmp_gt_i64: return 0x9F;
    case machine::Opcode::cmp_ge_i32:
    case machine::Opcode::cmp_ge_i64: return 0x9D;
    case machine::Opcode::cmp_ult_i32:
    case machine::Opcode::cmp_ult_i64: return 0x92;
    case machine::Opcode::cmp_ule_i32:
    case machine::Opcode::cmp_ule_i64: return 0x96;
    case machine::Opcode::cmp_ugt_i32:
    case machine::Opcode::cmp_ugt_i64: return 0x97;
    case machine::Opcode::cmp_uge_i32:
    case machine::Opcode::cmp_uge_i64: return 0x93;
    default: return 0;
    }
}

void add_error(Diagnostics& diagnostics, std::string message) {
    diagnostics.push_back({DiagnosticSeverity::error, std::move(message), {}});
}

std::vector<Register> argument_registers(Abi abi) {
    if (abi == Abi::windows) return {Register::ecx, Register::edx, Register::r8d, Register::r9d};
    return {Register::edi, Register::esi, Register::edx, Register::ecx, Register::r8d, Register::r9d};
}

struct AbiPlacement {
    enum class Kind : std::uint8_t { gpr, xmm, stack } kind{Kind::stack};
    std::uint32_t index{};
    std::uint32_t stack_index{};
};

std::vector<AbiPlacement> classify_arguments(std::span<const machine::RegisterClass> classes, Abi abi) {
    std::vector<AbiPlacement> placements;
    placements.reserve(classes.size());
    std::uint32_t gpr = 0, xmm = 0, stack = 0;
    for (std::size_t position = 0; position < classes.size(); ++position) {
        const bool floating = classes[position] == machine::RegisterClass::floating;
        if (abi == Abi::windows) {
            if (position < 4U) placements.push_back({floating ? AbiPlacement::Kind::xmm : AbiPlacement::Kind::gpr, static_cast<std::uint32_t>(position), 0});
            else placements.push_back({AbiPlacement::Kind::stack, 0, stack++});
        } else if (floating && xmm < 8U) {
            placements.push_back({AbiPlacement::Kind::xmm, xmm++, 0});
        } else if (!floating && gpr < 6U) {
            placements.push_back({AbiPlacement::Kind::gpr, gpr++, 0});
        } else {
            placements.push_back({AbiPlacement::Kind::stack, 0, stack++});
        }
    }
    return placements;
}

struct JumpFixup {
    std::size_t instruction_offset{};
    std::size_t displacement_offset{};
    std::string target;
};

struct LocalBranchFixup {
    std::size_t instruction_offset{};
    std::size_t displacement_offset{};
    std::size_t target_offset{};
    std::string target_label;
    std::uint8_t condition{};
};

void emit_jump(Buffer& out,
               std::string target,
               std::vector<JumpFixup>& fixups) {
    const auto instruction_offset = out.size();
    out.byte(0xE9); // provisional jmp rel32; relaxed after final block layout
    const auto displacement_offset = out.size();
    out.i32(0);
    fixups.push_back({instruction_offset, displacement_offset, std::move(target)});
}

bool same_allocation_location(const machine::AllocationLocation& left,
                              const machine::AllocationLocation& right) noexcept {
    if (left.kind != right.kind) return false;
    switch (left.kind) {
    case machine::LocationKind::physical_register: return left.physical == right.physical;
    case machine::LocationKind::floating_register: return left.floating == right.floating;
    case machine::LocationKind::stack_slot: return left.stack_offset == right.stack_offset;
    case machine::LocationKind::rematerialized_integer:
    case machine::LocationKind::rematerialized_floating:
        return left.rematerialized_immediate == right.rematerialized_immediate;
    }
    return false;
}

void emit_location_copy(Buffer& out,
                        machine::VirtualRegister source,
                        machine::VirtualRegister destination,
                        const machine::RegisterAllocation& allocation,
                        const machine::Function& function,
                        Register integer_scratch = Register::eax,
                        XmmRegister floating_scratch = XmmRegister::xmm0) {
    const bool floating = function.register_classes[source] == machine::RegisterClass::floating;
    const bool wide = function.register_widths[source] == 8;
    const auto& source_location = allocation.location(source);
    const auto& destination_location = allocation.location(destination);
    if (floating && source_location.kind == machine::LocationKind::floating_register &&
        destination_location.kind == machine::LocationKind::floating_register) {
        emit_sse_move(out, floating_register(destination_location.floating),
                      floating_register(source_location.floating), wide);
    } else if (!floating && source_location.kind == machine::LocationKind::physical_register &&
               destination_location.kind == machine::LocationKind::physical_register) {
        if (wide) emit_mov_register64(out, physical_register(destination_location.physical),
                                     physical_register(source_location.physical));
        else emit_mov_register(out, physical_register(destination_location.physical),
                               physical_register(source_location.physical));
    } else if (floating) {
        emit_read_float_location(out, floating_scratch, source_location, wide);
        emit_write_float_location(out, destination_location, floating_scratch, wide);
    } else if (wide) {
        emit_read_location64(out, integer_scratch, source_location);
        emit_write_location64(out, destination_location, integer_scratch);
    } else {
        emit_read_location(out, integer_scratch, source_location);
        emit_write_location(out, destination_location, integer_scratch);
    }
}

template <typename Copy, typename DestinationIsLiveSource, typename EmitCopy>
void emit_acyclic_parallel_copies(std::vector<Copy>& copies,
                                  DestinationIsLiveSource destination_is_live_source,
                                  EmitCopy emit_copy) {
    bool progress = true;
    while (progress) {
        progress = false;
        for (std::size_t index = 0; index < copies.size(); ++index) {
            auto& copy = copies[index];
            if (copy.emitted || destination_is_live_source(copies, index)) continue;
            emit_copy(copy);
            copy.emitted = true;
            progress = true;
        }
    }
}

template <typename Copy>
std::vector<Copy*> unresolved_parallel_copies(std::vector<Copy>& copies) {
    std::vector<Copy*> unresolved;
    unresolved.reserve(copies.size());
    for (auto& copy : copies)
        if (!copy.emitted) unresolved.push_back(&copy);
    return unresolved;
}

// Partition unresolved parallel copies into independent cycle components.
// For an assignment destination <- source, the next member in a rotation is
// the copy whose destination is the current source location. Keeping this
// planning independent from emission lets block edges and ABI entry copies
// share the same cycle decomposition while retaining target-specific moves.
template <typename Copy, typename DestinationMatchesSource>
std::vector<std::vector<Copy*>> unresolved_parallel_copy_cycles(
    std::vector<Copy>& copies, DestinationMatchesSource destination_matches_source) {
    std::vector<std::vector<Copy*>> cycles;
    std::vector<bool> visited(copies.size(), false);
    for (std::size_t start = 0; start < copies.size(); ++start) {
        if (copies[start].emitted || visited[start]) continue;
        std::vector<Copy*> cycle;
        auto current = start;
        while (!copies[current].emitted && !visited[current]) {
            visited[current] = true;
            cycle.push_back(&copies[current]);
            std::optional<std::size_t> next;
            for (std::size_t candidate = 0; candidate < copies.size(); ++candidate) {
                if (copies[candidate].emitted || visited[candidate]) continue;
                if (destination_matches_source(copies[candidate], copies[current])) {
                    next = candidate;
                    break;
                }
            }
            if (!next) break;
            current = *next;
        }
        if (!cycle.empty()) cycles.push_back(std::move(cycle));
    }
    return cycles;
}

bool edge_copies_are_noop(const machine::Successor& successor,
                          const machine::Block& target,
                          const machine::RegisterAllocation& allocation) {
    if (successor.arguments.size() != target.parameters.size()) return false;
    for (std::size_t index = 0; index < successor.arguments.size(); ++index) {
        if (!same_allocation_location(allocation.location(successor.arguments[index]),
                                      allocation.location(target.parameters[index])))
            return false;
    }
    return true;
}

void emit_parallel_copies(Buffer& out,
                          const machine::Successor& successor,
                          const machine::Block& target,
                          const machine::RegisterAllocation& allocation,
                          const machine::Function& function,
                          Diagnostics& diagnostics,
                          const std::string& function_name) {
    if (successor.arguments.size() != target.parameters.size()) {
        add_error(diagnostics, "machine edge argument mismatch for block " + target.name + " in @" + function_name);
        return;
    }
    if (successor.arguments.empty()) return;

    struct PendingCopy {
        machine::VirtualRegister source{};
        machine::VirtualRegister destination{};
        bool emitted{};
    };
    std::vector<PendingCopy> pending;
    pending.reserve(successor.arguments.size());
    for (std::size_t index = 0; index < successor.arguments.size(); ++index) {
        const auto source = successor.arguments[index];
        const auto destination = target.parameters[index];
        if (same_allocation_location(allocation.location(source), allocation.location(destination))) continue;
        pending.push_back({source, destination, false});
    }
    if (pending.empty()) return;

    // Emit every acyclic move directly. A destination is safe to overwrite when
    // no remaining copy still needs its old location as a source. This removes
    // the stack snapshot from the common loop-carried SSA case while retaining
    // a cycle-safe fallback for swaps and rotations.
    emit_acyclic_parallel_copies(
        pending,
        [&](const std::vector<PendingCopy>& copies, std::size_t copy_index) {
            const auto& copy = copies[copy_index];
            const auto& destination_location = allocation.location(copy.destination);
            for (std::size_t other_index = 0; other_index < copies.size(); ++other_index) {
                if (other_index == copy_index || copies[other_index].emitted) continue;
                if (same_allocation_location(destination_location, allocation.location(copies[other_index].source)))
                    return true;
            }
            return false;
        },
        [&](PendingCopy& copy) {
            emit_location_copy(out, copy.source, copy.destination, allocation, function);
        });

    auto cycles = unresolved_parallel_copy_cycles(
        pending,
        [&](const PendingCopy& candidate, const PendingCopy& current) {
            return same_allocation_location(allocation.location(candidate.destination),
                                            allocation.location(current.source));
        });
    for (auto& cyclic : cycles) {
        if (cyclic.empty()) continue;

        // A two-register integer cycle is a swap. Encode it directly with XCHG.
        if (cyclic.size() == 2U) {
            const auto& first_source_location = allocation.location(cyclic[0]->source);
            const auto& first_destination_location = allocation.location(cyclic[0]->destination);
            const auto& second_source_location = allocation.location(cyclic[1]->source);
            const auto& second_destination_location = allocation.location(cyclic[1]->destination);
            const bool integer_cycle =
                function.register_classes[cyclic[0]->source] == machine::RegisterClass::integer &&
                function.register_classes[cyclic[0]->destination] == machine::RegisterClass::integer &&
                function.register_classes[cyclic[1]->source] == machine::RegisterClass::integer &&
                function.register_classes[cyclic[1]->destination] == machine::RegisterClass::integer;
            const bool register_cycle =
                first_source_location.kind == machine::LocationKind::physical_register &&
                first_destination_location.kind == machine::LocationKind::physical_register &&
                second_source_location.kind == machine::LocationKind::physical_register &&
                second_destination_location.kind == machine::LocationKind::physical_register;
            const bool reciprocal =
                same_allocation_location(first_source_location, second_destination_location) &&
                same_allocation_location(second_source_location, first_destination_location);
            const bool same_width =
                function.register_widths[cyclic[0]->source] == function.register_widths[cyclic[0]->destination] &&
                function.register_widths[cyclic[0]->source] == function.register_widths[cyclic[1]->source] &&
                function.register_widths[cyclic[0]->source] == function.register_widths[cyclic[1]->destination];
            if (integer_cycle && register_cycle && reciprocal && same_width) {
                emit_xchg_registers(out, physical_register(first_source_location.physical),
                                    physical_register(second_source_location.physical),
                                    function.register_widths[cyclic[0]->source] == 8U);
                for (auto* copy : cyclic) copy->emitted = true;
                continue;
            }
        }

        const auto first_source = cyclic.front()->source;
        const bool cycle_floating = function.register_classes[first_source] == machine::RegisterClass::floating;
        const bool cycle_wide = function.register_widths[first_source] == 8;
        const bool homogeneous_cycle = std::all_of(cyclic.begin(), cyclic.end(), [&](const PendingCopy* copy) {
            return (function.register_classes[copy->source] == machine::RegisterClass::floating) == cycle_floating &&
                   (function.register_classes[copy->destination] == machine::RegisterClass::floating) == cycle_floating &&
                   (function.register_widths[copy->source] == 8) == cycle_wide &&
                   (function.register_widths[copy->destination] == 8) == cycle_wide;
        });
        if (homogeneous_cycle) {
            const auto first_destination = cyclic.front()->destination;
            if (cycle_floating)
                emit_read_float_location(out, XmmRegister::xmm0, allocation.location(first_source), cycle_wide);
            else if (cycle_wide)
                emit_read_location64(out, Register::eax, allocation.location(first_source));
            else
                emit_read_location(out, Register::eax, allocation.location(first_source));

            auto hole_location = allocation.location(first_source);
            std::vector<bool> rotated(cyclic.size(), false);
            rotated.front() = true;
            std::size_t rotated_count = 1U;
            while (rotated_count < cyclic.size()) {
                bool found = false;
                for (std::size_t index = 1; index < cyclic.size(); ++index) {
                    if (rotated[index] ||
                        !same_allocation_location(allocation.location(cyclic[index]->destination), hole_location)) continue;
                    emit_location_copy(out, cyclic[index]->source, cyclic[index]->destination, allocation, function,
                                       Register::ecx, XmmRegister::xmm1);
                    hole_location = allocation.location(cyclic[index]->source);
                    rotated[index] = true;
                    ++rotated_count;
                    found = true;
                    break;
                }
                if (!found) break;
            }
            if (rotated_count == cyclic.size() &&
                same_allocation_location(hole_location, allocation.location(first_destination))) {
                if (cycle_floating)
                    emit_write_float_location(out, allocation.location(first_destination), XmmRegister::xmm0, cycle_wide);
                else if (cycle_wide)
                    emit_write_location64(out, allocation.location(first_destination), Register::eax);
                else
                    emit_write_location(out, allocation.location(first_destination), Register::eax);
                for (auto* copy : cyclic) copy->emitted = true;
                continue;
            }
        }

        // Snapshot only this irreducible or heterogeneous component. Independent
        // cycles no longer force one another through the stack fallback.
        const auto scratch_size = static_cast<std::uint32_t>(cyclic.size() * 8U);
        emit_adjust_rsp(out, true, scratch_size);
        for (std::size_t index = 0; index < cyclic.size(); ++index) {
            const auto source = cyclic[index]->source;
            const auto displacement = static_cast<std::int32_t>(index * 8U);
            const bool floating = function.register_classes[source] == machine::RegisterClass::floating;
            const bool wide = function.register_widths[source] == 8;
            if (floating) {
                emit_read_float_location(out, XmmRegister::xmm0, allocation.location(source), wide);
                emit_sse_rsp_store(out, XmmRegister::xmm0, displacement, wide);
            } else if (wide) {
                emit_read_location64(out, Register::eax, allocation.location(source));
                emit_store_rsp64(out, Register::eax, displacement);
            } else {
                emit_read_location(out, Register::eax, allocation.location(source));
                emit_store_rsp(out, Register::eax, displacement);
            }
        }
        for (std::size_t index = 0; index < cyclic.size(); ++index) {
            const auto destination = cyclic[index]->destination;
            const auto displacement = static_cast<std::int32_t>(index * 8U);
            const bool floating = function.register_classes[destination] == machine::RegisterClass::floating;
            const bool wide = function.register_widths[destination] == 8;
            if (floating) {
                emit_sse_rsp_load(out, XmmRegister::xmm0, displacement, wide);
                emit_write_float_location(out, allocation.location(destination), XmmRegister::xmm0, wide);
            } else if (wide) {
                emit_load_rsp64(out, Register::eax, displacement);
                emit_write_location64(out, allocation.location(destination), Register::eax);
            } else {
                emit_load_rsp(out, Register::eax, displacement);
                emit_write_location(out, allocation.location(destination), Register::eax);
            }
            cyclic[index]->emitted = true;
        }
        emit_adjust_rsp(out, false, scratch_size);
    }

}

std::vector<machine::PhysicalRegister> used_callee_saved_registers(
    const machine::RegisterAllocation& allocation) {
    std::vector<machine::PhysicalRegister> result;
    for (const auto& location : allocation.locations) {
        if (location.kind != machine::LocationKind::physical_register ||
            !machine::is_callee_saved(location.physical)) continue;
        if (std::find(result.begin(), result.end(), location.physical) == result.end())
            result.push_back(location.physical);
    }
    std::sort(result.begin(), result.end(), [](auto left, auto right) {
        return static_cast<unsigned>(left) < static_cast<unsigned>(right);
    });
    return result;
}

void emit_push_physical(Buffer& out, machine::PhysicalRegister reg) {
    switch (reg) {
    case machine::PhysicalRegister::ebx: out.byte(0x53); break;
    case machine::PhysicalRegister::r12d: out.byte(0x41); out.byte(0x54); break;
    case machine::PhysicalRegister::r13d: out.byte(0x41); out.byte(0x55); break;
    case machine::PhysicalRegister::r14d: out.byte(0x41); out.byte(0x56); break;
    case machine::PhysicalRegister::r15d: out.byte(0x41); out.byte(0x57); break;
    default: break;
    }
}

void emit_pop_physical(Buffer& out, machine::PhysicalRegister reg) {
    switch (reg) {
    case machine::PhysicalRegister::ebx: out.byte(0x5B); break;
    case machine::PhysicalRegister::r12d: out.byte(0x41); out.byte(0x5C); break;
    case machine::PhysicalRegister::r13d: out.byte(0x41); out.byte(0x5D); break;
    case machine::PhysicalRegister::r14d: out.byte(0x41); out.byte(0x5E); break;
    case machine::PhysicalRegister::r15d: out.byte(0x41); out.byte(0x5F); break;
    default: break;
    }
}

EncodedFunction encode_function(const machine::Function& source_function, Abi abi, Diagnostics& diagnostics) {
    machine::Function split_function = source_function;
    const auto split_stats = machine::split_live_ranges_around_calls(split_function);
    const auto& function = split_function;
    EncodedFunction encoded;
    encoded.name = function.name;
    encoded.machine_instruction_count_before_optimization = function.machine_instructions_before_optimization;
    encoded.machine_copy_propagated_count = function.machine_copies_propagated;
    encoded.machine_zero_offset_eliminated_count = function.machine_zero_offsets_eliminated;
    encoded.machine_redundant_cast_eliminated_count = function.machine_redundant_casts_eliminated;
    encoded.machine_address_mode_folded_count = function.machine_address_modes_folded;
    encoded.machine_compare_branch_fused_count = function.machine_compare_branches_fused;
    encoded.machine_compare_branch_byte_avoided_count = function.machine_compare_branch_bytes_avoided;
    encoded.machine_floating_compare_branch_fused_count = function.machine_floating_compare_branches_fused;
    encoded.machine_floating_compare_branch_byte_avoided_count = function.machine_floating_compare_branch_bytes_avoided;
    encoded.machine_jump_thread_count = function.machine_jump_threads;
    encoded.machine_empty_block_removed_count = function.machine_empty_blocks_removed;
    encoded.machine_unreachable_block_removed_count = function.machine_unreachable_blocks_removed;
    encoded.machine_block_reordered_count = function.machine_blocks_reordered;
    encoded.machine_immediate_form_selected_count = function.machine_immediate_forms_selected;
    encoded.machine_constant_definition_eliminated_count = function.machine_constant_definitions_eliminated;
    encoded.machine_immediate_comparison_selected_count = function.machine_immediate_comparisons_selected;
    encoded.machine_direct_constant_return_count = function.machine_direct_constant_returns;
    encoded.machine_zeroing_idiom_selected_count = function.machine_zeroing_idioms_selected;
    encoded.machine_constant_store_selected_count = function.machine_constant_stores_selected;
    encoded.machine_extension_chain_eliminated_count = function.machine_extension_chains_eliminated;
    encoded.machine_load_return_folded_count = function.machine_load_returns_folded;
    encoded.machine_load_arithmetic_folded_count = function.machine_load_arithmetic_folded;
    encoded.machine_dead_instruction_eliminated_count = function.machine_dead_instructions_eliminated;
    encoded.machine_dead_comparison_eliminated_count = function.machine_dead_comparisons_eliminated;
    encoded.machine_cross_block_copy_propagated_count = function.machine_cross_block_copies_propagated;
    encoded.machine_liveness_iteration_count = function.machine_liveness_iterations;
    encoded.machine_cross_block_live_value_count = function.machine_cross_block_live_values;
    if (function.blocks.empty()) {
        add_error(diagnostics, "machine function has no blocks in @" + function.name);
        return encoded;
    }

    const auto allocation = machine::allocate_linear_scan(function);
    if (!allocation.ok()) {
        diagnostics.insert(diagnostics.end(), allocation.diagnostics.begin(), allocation.diagnostics.end());
        return encoded;
    }

    std::unordered_set<machine::VirtualRegister> arithmetic_flag_results;
    for (const auto& block : function.blocks)
        for (const auto& instruction : block.instructions)
            if (instruction.opcode == machine::Opcode::branch_i1 && instruction.symbol == "$flags" &&
                instruction.inputs.size() == 1U)
                arithmetic_flag_results.insert(instruction.inputs.front());

    const auto args = argument_registers(abi);
    const auto argument_placements = classify_arguments(function.argument_classes, abi);
    const auto allocation_uses = [&](machine::PhysicalRegister physical) {
        return std::any_of(allocation.locations.begin(), allocation.locations.end(),
            [&](const machine::AllocationLocation& location) {
                return location.kind == machine::LocationKind::physical_register && location.physical == physical;
            });
    };
    const auto incoming_argument_index = [&](Register physical) -> std::optional<std::size_t> {
        for (std::size_t index = 0; index < argument_placements.size(); ++index) {
            const auto& placement = argument_placements[index];
            if (placement.kind == AbiPlacement::Kind::gpr && args[placement.index] == physical)
                return index;
        }
        return std::nullopt;
    };
    // r8/r9 only need an entry snapshot when their incoming ABI value can be
    // overwritten before the corresponding load_argument consumes it. Earlier
    // versions captured whenever the allocator used the register anywhere in
    // the function, forcing a frame and two memory round trips even when all
    // arguments were copied out first. This scan is deliberately based on the
    // final machine order and allocation, so it remains valid as scheduling and
    // register assignment evolve.
    const auto incoming_needs_capture = [&](machine::PhysicalRegister physical,
                                            Register abi_register) {
        if (!allocation_uses(physical)) return false;
        const auto argument_index = incoming_argument_index(abi_register);
        if (!argument_index) return false;

        for (const auto& block : function.blocks) {
            for (const auto& instruction : block.instructions) {
                const bool is_target_load =
                    (instruction.opcode == machine::Opcode::load_argument ||
                     instruction.opcode == machine::Opcode::load_argument_i64 ||
                     instruction.opcode == machine::Opcode::load_argument_f32 ||
                     instruction.opcode == machine::Opcode::load_argument_f64) &&
                    instruction.argument_index == *argument_index;
                if (is_target_load) return false;
                switch (instruction.opcode) {
                case machine::Opcode::call_i32: case machine::Opcode::call_i64:
                case machine::Opcode::call_f32: case machine::Opcode::call_f64:
                case machine::Opcode::call_void: case machine::Opcode::call_aggregate:
                case machine::Opcode::call_indirect_i32: case machine::Opcode::call_indirect_i64:
                case machine::Opcode::call_indirect_f32: case machine::Opcode::call_indirect_f64:
                case machine::Opcode::call_indirect_void:
                    return true;
                default:
                    break;
                }
                if (instruction.result < function.register_count) {
                    const auto location = allocation.location(instruction.result);
                    if (location.kind == machine::LocationKind::physical_register &&
                        location.physical == physical)
                        return true;
                }
            }
        }
        return true;
    };
    const bool capture_r8 = incoming_needs_capture(machine::PhysicalRegister::r8d, Register::r8d);
    const bool capture_r9 = incoming_needs_capture(machine::PhysicalRegister::r9d, Register::r9d);
    const std::uint32_t argument_capture_size = capture_r8 || capture_r9 ? 16U : 0U;
    const std::uint32_t encoded_frame_size = allocation.frame_size + argument_capture_size;
    const std::int32_t r8_capture_offset = -static_cast<std::int32_t>(allocation.frame_size + 8U);
    const std::int32_t r9_capture_offset = -static_cast<std::int32_t>(allocation.frame_size + 16U);

    encoded.frame_size = encoded_frame_size;
    encoded.allocated_register_count = allocation.physical_count;
    encoded.spill_slot_count = allocation.spill_slot_count;
    encoded.spilled_value_count = allocation.spill_count;
    encoded.reused_spill_slot_count = allocation.reused_spill_slot_count;
    encoded.frame_size_before_slot_reuse = allocation.frame_size_before_slot_reuse;
    encoded.frame_bytes_saved = allocation.frame_bytes_saved;
    encoded.rematerialized_value_count = allocation.rematerialized_value_count;
    encoded.rematerialized_use_count = allocation.rematerialized_use_count;
    encoded.peak_integer_pressure = allocation.peak_integer_pressure;
    encoded.peak_floating_pressure = allocation.peak_floating_pressure;
    encoded.call_crossing_interval_count = allocation.call_crossing_interval_count;
    encoded.caller_saved_allocation_count = allocation.caller_saved_allocation_count;
    encoded.callee_saved_allocation_count = allocation.callee_saved_allocation_count;
    encoded.weighted_spill_decision_count = allocation.weighted_spill_decision_count;
    encoded.allocation_copy_hint_count = allocation.copy_hint_count;
    encoded.segmented_interval_count = allocation.segmented_interval_count;
    encoded.live_range_hole_count = allocation.live_range_hole_count;
    encoded.interference_edge_count = allocation.interference_edge_count;
    encoded.hole_aware_register_reuse_count = allocation.hole_aware_register_reuse_count;
    encoded.live_range_split_count = split_stats.split_values;
    encoded.split_transition_store_count = split_stats.transition_stores;
    encoded.split_transition_load_count = split_stats.transition_loads;
    encoded.split_transition_byte_count = split_stats.transition_bytes;

    const auto callee_saved = used_callee_saved_registers(allocation);
    // A frame pointer is only required when this function has rbp-relative
    // storage. Calls alone do not require one: call-site alignment is computed
    // from the actual fixed stack depth below. This keeps small wrappers and
    // call-heavy leaf-like functions frameless while preserving unwind-neutral
    // callee-saved pushes and ABI alignment.
    const bool has_incoming_stack_arguments = std::any_of(
        argument_placements.begin(), argument_placements.end(),
        [](const AbiPlacement& placement) { return placement.kind == AbiPlacement::Kind::stack; });
    // Incoming stack arguments are addressed relative to rbp by the baseline
    // encoder. Keep a stable frame pointer for those functions even when they
    // have no local spill frame; otherwise a frameless Windows x64 function
    // reads argument five and later through an uninitialized rbp.
    const bool omit_frame_pointer = encoded_frame_size == 0U && !has_incoming_stack_arguments;
    if (omit_frame_pointer) {
        encoded.leaf_frame_elided_count = 1U;
        encoded.leaf_frame_byte_avoided_count = 4U; // push rbp + mov rbp,rsp
    }
    if (function.argument_count > 64) {
        add_error(diagnostics, "x86-64 baseline encoder supports at most 64 arguments in @" + function.name);
        return encoded;
    }

    std::unordered_map<std::string, const machine::Block*> blocks;
    for (const auto& block : function.blocks) blocks.emplace(block.name, &block);

    Buffer out;
    struct SpillCacheState {
        std::optional<std::int32_t> offset;
        std::optional<machine::VirtualRegister> owner;
        std::uint32_t generation{};
        std::uint64_t last_touch{};
        bool wide{};
        bool pending_store{};
    };
    constexpr std::size_t spill_cache_capacity = 2;
    std::array<SpillCacheState, spill_cache_capacity> integer_cache{};
    std::array<SpillCacheState, spill_cache_capacity> floating_cache{};
    constexpr std::array integer_cache_registers{Register::r8d, Register::r9d};
    constexpr std::array floating_cache_registers{XmmRegister::xmm6, XmmRegister::xmm7};
    std::uint64_t spill_cache_clock = 0;
    std::unordered_map<std::int32_t, std::uint32_t> integer_generations;
    std::unordered_map<std::int32_t, std::uint32_t> floating_generations;
    bool store_forwarding_enabled = true;
    std::vector<std::uint32_t> result_use_counts(function.register_count, 0);
    // A call result consumed only by the immediately following return already
    // resides in the ABI return register.  Preserve that placement through the
    // epilogue instead of copying it to its allocated location and immediately
    // loading it back.  This applies uniformly to direct and indirect integer
    // and floating-point calls.
    std::vector<bool> direct_call_return(function.register_count, false);
    // A floating call result consumed exactly once by the immediately following
    // arithmetic instruction can stay in xmm0.  The arithmetic encoder consumes
    // it directly instead of storing the result to its allocation and loading it
    // back one instruction later.
    std::vector<bool> direct_call_float_arithmetic(function.register_count, false);
    // A floating call result consumed only by the immediately following call can
    // remain in xmm0 and participate directly in that call's ABI parallel copy.
    // This forwards general call chains without assigning a benchmark-specific
    // physical location or weakening normal cross-call liveness rules.
    std::vector<bool> direct_call_float_next_call(function.register_count, false);
    // Integer call results consumed only by the next call can remain in rax/eax
    // and enter the outgoing GPR parallel-copy plan directly.
    std::vector<bool> direct_call_integer_next_call(function.register_count, false);
    const auto is_value_call = [](machine::Opcode opcode) {
        switch (opcode) {
        case machine::Opcode::call_i32:
        case machine::Opcode::call_i64:
        case machine::Opcode::call_f32:
        case machine::Opcode::call_f64:
        case machine::Opcode::call_indirect_i32:
        case machine::Opcode::call_indirect_i64:
        case machine::Opcode::call_indirect_f32:
        case machine::Opcode::call_indirect_f64:
            return true;
        default:
            return false;
        }
    };
    const auto call_argument_begin = [](const machine::Instruction& call) -> std::size_t {
        return call.opcode == machine::Opcode::call_aggregate ||
               call.opcode == machine::Opcode::call_indirect_i32 ||
               call.opcode == machine::Opcode::call_indirect_i64 ||
               call.opcode == machine::Opcode::call_indirect_f32 ||
               call.opcode == machine::Opcode::call_indirect_f64 ||
               call.opcode == machine::Opcode::call_indirect_void ? 1U : 0U;
    };
    const auto call_has_stack_arguments = [&](const machine::Instruction& call) {
        const auto begin = call_argument_begin(call);
        if (begin > call.inputs.size()) return true;
        std::vector<machine::RegisterClass> classes;
        classes.reserve(call.inputs.size() - begin);
        for (std::size_t input = begin; input < call.inputs.size(); ++input) {
            const auto reg = call.inputs[input];
            if (reg >= function.register_classes.size()) return true;
            classes.push_back(function.register_classes[reg]);
        }
        const auto placements = classify_arguments(classes, abi);
        return std::any_of(placements.begin(), placements.end(), [](const AbiPlacement& placement) {
            return placement.kind == AbiPlacement::Kind::stack;
        });
    };
    const auto is_matching_return = [](machine::Opcode call, machine::Opcode ret) {
        return (call == machine::Opcode::call_i32 || call == machine::Opcode::call_indirect_i32)
                   ? ret == machine::Opcode::return_i32
             : (call == machine::Opcode::call_i64 || call == machine::Opcode::call_indirect_i64)
                   ? ret == machine::Opcode::return_i64
             : (call == machine::Opcode::call_f32 || call == machine::Opcode::call_indirect_f32)
                   ? ret == machine::Opcode::return_f32
             : (call == machine::Opcode::call_f64 || call == machine::Opcode::call_indirect_f64)
                   ? ret == machine::Opcode::return_f64
                   : false;
    };
    for (const auto& block : function.blocks) {
        for (std::size_t index = 0; index + 1U < block.instructions.size(); ++index) {
            const auto& call = block.instructions[index];
            const auto& ret = block.instructions[index + 1U];
            if (!is_value_call(call.opcode) || call.result >= direct_call_return.size() ||
                !is_matching_return(call.opcode, ret.opcode) || ret.inputs.size() != 1U ||
                ret.inputs[0] != call.result)
                continue;
            direct_call_return[call.result] = true;
        }
        for (std::size_t index = 0; index + 1U < block.instructions.size(); ++index) {
            const auto& call = block.instructions[index];
            const bool floating_call = call.opcode == machine::Opcode::call_f32 ||
                                       call.opcode == machine::Opcode::call_f64 ||
                                       call.opcode == machine::Opcode::call_indirect_f32 ||
                                       call.opcode == machine::Opcode::call_indirect_f64;
            if (!floating_call || call.result >= direct_call_float_next_call.size()) continue;
            std::size_t next_index = index + 1U;
            // Floating constants between calls are harmless when they are emitted
            // directly into their allocated XMM register.  Skipping them here
            // enables general call-chain forwarding without requiring adjacency
            // in the pre-encoded machine stream.
            while (next_index < block.instructions.size()) {
                const auto& middle = block.instructions[next_index];
                if (middle.opcode != machine::Opcode::load_immediate_f32 &&
                    middle.opcode != machine::Opcode::load_immediate_f64)
                    break;
                const auto& location = allocation.location(middle.result);
                if (location.kind != machine::LocationKind::floating_register) break;
                ++next_index;
            }
            if (next_index >= block.instructions.size()) continue;
            const auto& next_call = block.instructions[next_index];
            const bool next_is_call = is_value_call(next_call.opcode) ||
                                      next_call.opcode == machine::Opcode::call_void ||
                                      next_call.opcode == machine::Opcode::call_aggregate ||
                                      next_call.opcode == machine::Opcode::call_indirect_void;
            if (!next_is_call) continue;
            // Outgoing stack arguments are staged before register arguments.
            // That staging uses xmm0/eax as scratch.  A forwarded previous call
            // result also lives in xmm0/eax, so forwarding across a call with
            // stack arguments can destroy the value before it is copied into an
            // ABI register.  Keep the result in its allocated location for those
            // calls; register-only calls retain the zero-copy forwarding path.
            if (call_has_stack_arguments(next_call)) continue;
            const auto argument_begin = call_argument_begin(next_call);
            if (std::find(next_call.inputs.begin() + static_cast<std::ptrdiff_t>(argument_begin),
                          next_call.inputs.end(), call.result) != next_call.inputs.end())
                direct_call_float_next_call[call.result] = true;
        }
        for (std::size_t index = 0; index + 1U < block.instructions.size(); ++index) {
            const auto& call = block.instructions[index];
            const bool integer_call = call.opcode == machine::Opcode::call_i32 ||
                                      call.opcode == machine::Opcode::call_i64 ||
                                      call.opcode == machine::Opcode::call_indirect_i32 ||
                                      call.opcode == machine::Opcode::call_indirect_i64;
            if (!integer_call || call.result >= direct_call_integer_next_call.size()) continue;
            const auto& next_call = block.instructions[index + 1U];
            const bool next_is_call = is_value_call(next_call.opcode) ||
                                      next_call.opcode == machine::Opcode::call_void ||
                                      next_call.opcode == machine::Opcode::call_aggregate ||
                                      next_call.opcode == machine::Opcode::call_indirect_void;
            if (!next_is_call) continue;
            if (call_has_stack_arguments(next_call)) continue;
            const auto argument_begin = call_argument_begin(next_call);
            if (std::find(next_call.inputs.begin() + static_cast<std::ptrdiff_t>(argument_begin),
                          next_call.inputs.end(), call.result) != next_call.inputs.end())
                direct_call_integer_next_call[call.result] = true;
        }
        for (std::size_t index = 0; index + 1U < block.instructions.size(); ++index) {
            const auto& call = block.instructions[index];
            const auto& arithmetic = block.instructions[index + 1U];
            const bool floating_call = call.opcode == machine::Opcode::call_f32 ||
                                       call.opcode == machine::Opcode::call_f64 ||
                                       call.opcode == machine::Opcode::call_indirect_f32 ||
                                       call.opcode == machine::Opcode::call_indirect_f64;
            const bool floating_arithmetic = arithmetic.opcode == machine::Opcode::add_f32 ||
                                             arithmetic.opcode == machine::Opcode::add_f64 ||
                                             arithmetic.opcode == machine::Opcode::sub_f32 ||
                                             arithmetic.opcode == machine::Opcode::sub_f64 ||
                                             arithmetic.opcode == machine::Opcode::mul_f32 ||
                                             arithmetic.opcode == machine::Opcode::mul_f64 ||
                                             arithmetic.opcode == machine::Opcode::div_f32 ||
                                             arithmetic.opcode == machine::Opcode::div_f64;
            if (!floating_call || !floating_arithmetic || call.result >= direct_call_float_arithmetic.size() ||
                arithmetic.inputs.size() != 2U) continue;
            const bool commutative = arithmetic.opcode == machine::Opcode::add_f32 ||
                                     arithmetic.opcode == machine::Opcode::add_f64 ||
                                     arithmetic.opcode == machine::Opcode::mul_f32 ||
                                     arithmetic.opcode == machine::Opcode::mul_f64;
            if (arithmetic.inputs[0] == call.result ||
                (commutative && arithmetic.inputs[1] == call.result))
                direct_call_float_arithmetic[call.result] = true;
        }
    }
    for (const auto& block : function.blocks) {
        for (const auto& instruction : block.instructions) {
            for (const auto input : instruction.inputs)
                if (input < result_use_counts.size()) ++result_use_counts[input];
            for (const auto& successor : instruction.successors)
                for (const auto argument : successor.arguments)
                    if (argument < result_use_counts.size()) ++result_use_counts[argument];
        }
    }
    for (machine::VirtualRegister reg = 0; reg < direct_call_float_arithmetic.size(); ++reg) {
        if (direct_call_float_arithmetic[reg] && result_use_counts[reg] != 1U)
            direct_call_float_arithmetic[reg] = false;
        if (direct_call_float_next_call[reg] && result_use_counts[reg] != 1U)
            direct_call_float_next_call[reg] = false;
        if (direct_call_integer_next_call[reg] && result_use_counts[reg] != 1U)
            direct_call_integer_next_call[reg] = false;
    }

    std::optional<machine::AllocationLocation> current_result_location;
    bool current_result_is_dead = false;
    machine::VirtualRegister instruction_result_owner{};

    const auto is_dead_current_result = [&](const machine::AllocationLocation& location) {
        return current_result_is_dead && current_result_location && same_location(*current_result_location, location);
    };

    const auto measure_integer_store = [&](bool wide, Register cache_register = Register::r8d) {
        Buffer measurement;
        machine::AllocationLocation slot{machine::LocationKind::stack_slot, {}, {}, -8};
        if (wide) emit_write_location64(measurement, slot, cache_register);
        else emit_write_location(measurement, slot, cache_register);
        return static_cast<std::uint32_t>(measurement.size());
    };
    const auto measure_floating_store = [&](bool wide, XmmRegister cache_register = XmmRegister::xmm6) {
        Buffer measurement;
        machine::AllocationLocation slot{machine::LocationKind::stack_slot, {}, {}, -8};
        emit_write_float_location(measurement, slot, cache_register, wide);
        return static_cast<std::uint32_t>(measurement.size());
    };
    const auto measure_integer_load = [&](bool wide) {
        Buffer measurement;
        machine::AllocationLocation slot{machine::LocationKind::stack_slot, {}, {}, -8};
        if (wide) emit_read_location64(measurement, Register::eax, slot);
        else emit_read_location(measurement, Register::eax, slot);
        return static_cast<std::uint32_t>(measurement.size());
    };
    const auto measure_floating_load = [&](bool wide) {
        Buffer measurement;
        machine::AllocationLocation slot{machine::LocationKind::stack_slot, {}, {}, -8};
        emit_read_float_location(measurement, XmmRegister::xmm0, slot, wide);
        return static_cast<std::uint32_t>(measurement.size());
    };
    const auto update_peak_residency = [&] {
        std::uint32_t resident = 0;
        for (const auto& entry : integer_cache) resident += entry.offset.has_value() ? 1U : 0U;
        for (const auto& entry : floating_cache) resident += entry.offset.has_value() ? 1U : 0U;
        encoded.spill_cache_peak_resident_count = std::max(encoded.spill_cache_peak_resident_count, resident);
    };
    const auto flush_integer_entry = [&](std::size_t index) {
        auto& entry = integer_cache[index];
        if (!entry.pending_store || !entry.offset) return;
        if (entry.owner && *entry.owner < result_use_counts.size() && result_use_counts[*entry.owner] == 0) {
            const auto bytes = measure_integer_store(entry.wide, integer_cache_registers[index]);
            ++encoded.eliminated_spill_store_count;
            ++encoded.spill_cache_last_use_drop_count;
            encoded.eliminated_encoded_byte_count += bytes;
            encoded.avoided_spill_store_byte_count += bytes;
            entry.pending_store = false;
            return;
        }
        machine::AllocationLocation slot{machine::LocationKind::stack_slot, {}, {}, *entry.offset};
        if (entry.wide) emit_write_location64(out, slot, integer_cache_registers[index]);
        else emit_write_location(out, slot, integer_cache_registers[index]);
        entry.pending_store = false;
        ++encoded.spill_cache_dirty_eviction_count;
    };
    const auto flush_floating_entry = [&](std::size_t index) {
        auto& entry = floating_cache[index];
        if (!entry.pending_store || !entry.offset) return;
        if (entry.owner && *entry.owner < result_use_counts.size() && result_use_counts[*entry.owner] == 0) {
            const auto bytes = measure_floating_store(entry.wide, floating_cache_registers[index]);
            ++encoded.eliminated_spill_store_count;
            ++encoded.spill_cache_last_use_drop_count;
            encoded.eliminated_encoded_byte_count += bytes;
            encoded.avoided_spill_store_byte_count += bytes;
            entry.pending_store = false;
            return;
        }
        machine::AllocationLocation slot{machine::LocationKind::stack_slot, {}, {}, *entry.offset};
        emit_write_float_location(out, slot, floating_cache_registers[index], entry.wide);
        entry.pending_store = false;
        ++encoded.spill_cache_dirty_eviction_count;
    };
    const auto invalidate_spill_caches = [&] {
        bool had_entry = false;
        for (std::size_t index = 0; index < spill_cache_capacity; ++index) {
            had_entry = had_entry || integer_cache[index].offset.has_value() || floating_cache[index].offset.has_value();
            flush_integer_entry(index);
            flush_floating_entry(index);
        }
        if (had_entry) { ++encoded.spill_cache_invalidation_count; ++encoded.spill_cache_boundary_flush_count; }
        integer_cache = {};
        floating_cache = {};
    };
    const auto choose_integer_entry = [&]() {
        for (std::size_t index = 0; index < spill_cache_capacity; ++index)
            if (!integer_cache[index].offset) return index;
        return integer_cache[0].last_touch <= integer_cache[1].last_touch ? std::size_t{0} : std::size_t{1};
    };
    const auto choose_floating_entry = [&]() {
        for (std::size_t index = 0; index < spill_cache_capacity; ++index)
            if (!floating_cache[index].offset) return index;
        return floating_cache[0].last_touch <= floating_cache[1].last_touch ? std::size_t{0} : std::size_t{1};
    };
    const auto read_integer_cached = [&](Register destination,
                                         const machine::AllocationLocation& location, bool wide) {
        if (location.kind != machine::LocationKind::stack_slot) {
            if (wide) emit_read_location64(out, destination, location);
            else emit_read_location(out, destination, location);
            return;
        }
        const auto generation = integer_generations[location.stack_offset];
        for (std::size_t index = 0; index < spill_cache_capacity; ++index) {
            auto& entry = integer_cache[index];
            if (entry.offset == location.stack_offset && entry.wide == wide && entry.generation == generation) {
                if (wide) emit_mov_register64(out, destination, integer_cache_registers[index]);
                else emit_mov_register(out, destination, integer_cache_registers[index]);
                entry.last_touch = ++spill_cache_clock;
                ++encoded.cached_spill_load_count;
                ++encoded.spill_cache_hit_count;
                if (entry.pending_store) ++encoded.folded_spill_store_load_count;
                encoded.avoided_spill_load_byte_count += measure_integer_load(wide);
                return;
            }
        }
        ++encoded.spill_cache_miss_count;
        const auto index = choose_integer_entry();
        if (integer_cache[index].offset) {
            if (!integer_cache[index].pending_store) ++encoded.spill_cache_clean_eviction_count;
            flush_integer_entry(index);
        }
        if (wide) {
            emit_read_location64(out, destination, location);
            emit_mov_register64(out, integer_cache_registers[index], destination);
        } else {
            emit_read_location(out, destination, location);
            emit_mov_register(out, integer_cache_registers[index], destination);
        }
        integer_cache[index] = {location.stack_offset, std::nullopt, generation, ++spill_cache_clock, wide, false};
        update_peak_residency();
    };
    const auto read_floating_cached = [&](XmmRegister destination,
                                          const machine::AllocationLocation& location, bool wide) {
        if (location.kind != machine::LocationKind::stack_slot) {
            emit_read_float_location(out, destination, location, wide);
            return;
        }
        const auto generation = floating_generations[location.stack_offset];
        for (std::size_t index = 0; index < spill_cache_capacity; ++index) {
            auto& entry = floating_cache[index];
            if (entry.offset == location.stack_offset && entry.wide == wide && entry.generation == generation) {
                emit_sse_move(out, destination, floating_cache_registers[index], wide);
                entry.last_touch = ++spill_cache_clock;
                ++encoded.cached_spill_load_count;
                ++encoded.spill_cache_hit_count;
                if (entry.pending_store) ++encoded.folded_spill_store_load_count;
                encoded.avoided_spill_load_byte_count += measure_floating_load(wide);
                return;
            }
        }
        ++encoded.spill_cache_miss_count;
        const auto index = choose_floating_entry();
        if (floating_cache[index].offset) {
            if (!floating_cache[index].pending_store) ++encoded.spill_cache_clean_eviction_count;
            flush_floating_entry(index);
        }
        emit_read_float_location(out, destination, location, wide);
        emit_sse_move(out, floating_cache_registers[index], destination, wide);
        floating_cache[index] = {location.stack_offset, std::nullopt, generation, ++spill_cache_clock, wide, false};
        update_peak_residency();
    };
    const auto write_integer_cached = [&](const machine::AllocationLocation& location,
                                           Register source, bool wide) {
        if (location.kind == machine::LocationKind::stack_slot && is_dead_current_result(location)) {
            const auto store_bytes = measure_integer_store(wide);
            ++encoded.eliminated_spill_store_count;
            ++encoded.dead_spill_store_count;
            encoded.eliminated_encoded_byte_count += store_bytes;
            return;
        }
        if (!store_forwarding_enabled || location.kind != machine::LocationKind::stack_slot) {
            if (wide) emit_write_location64(out, location, source);
            else emit_write_location(out, location, source);
            return;
        }
        const auto store_bytes = measure_integer_store(wide);
        std::size_t index = spill_cache_capacity;
        for (std::size_t candidate = 0; candidate < spill_cache_capacity; ++candidate)
            if (integer_cache[candidate].offset == location.stack_offset) { index = candidate; break; }
        if (index == spill_cache_capacity) {
            index = choose_integer_entry();
            if (integer_cache[index].offset) {
                if (!integer_cache[index].pending_store) ++encoded.spill_cache_clean_eviction_count;
                flush_integer_entry(index);
            }
        } else if (integer_cache[index].pending_store) {
            ++encoded.eliminated_spill_store_count;
            encoded.eliminated_encoded_byte_count += store_bytes;
            encoded.avoided_spill_store_byte_count += store_bytes;
        }
        if (wide) emit_mov_register64(out, integer_cache_registers[index], source);
        else emit_mov_register(out, integer_cache_registers[index], source);
        const auto generation = ++integer_generations[location.stack_offset];
        ++encoded.spill_cache_generation_count;
        integer_cache[index] = {location.stack_offset, current_result_location ? std::optional<machine::VirtualRegister>(instruction_result_owner) : std::nullopt, generation, ++spill_cache_clock, wide, true};
        update_peak_residency();
        ++encoded.spill_store_cache_count;
        ++encoded.deferred_spill_store_count;
    };
    const auto write_floating_cached = [&](const machine::AllocationLocation& location,
                                            XmmRegister source, bool wide) {
        if (location.kind == machine::LocationKind::stack_slot && is_dead_current_result(location)) {
            const auto store_bytes = measure_floating_store(wide);
            ++encoded.eliminated_spill_store_count;
            ++encoded.dead_spill_store_count;
            encoded.eliminated_encoded_byte_count += store_bytes;
            return;
        }
        if (!store_forwarding_enabled || location.kind != machine::LocationKind::stack_slot) {
            emit_write_float_location(out, location, source, wide);
            return;
        }
        const auto store_bytes = measure_floating_store(wide);
        std::size_t index = spill_cache_capacity;
        for (std::size_t candidate = 0; candidate < spill_cache_capacity; ++candidate)
            if (floating_cache[candidate].offset == location.stack_offset) { index = candidate; break; }
        if (index == spill_cache_capacity) {
            index = choose_floating_entry();
            if (floating_cache[index].offset) {
                if (!floating_cache[index].pending_store) ++encoded.spill_cache_clean_eviction_count;
                flush_floating_entry(index);
            }
        } else if (floating_cache[index].pending_store) {
            ++encoded.eliminated_spill_store_count;
            encoded.eliminated_encoded_byte_count += store_bytes;
            encoded.avoided_spill_store_byte_count += store_bytes;
        }
        emit_sse_move(out, floating_cache_registers[index], source, wide);
        const auto generation = ++floating_generations[location.stack_offset];
        ++encoded.spill_cache_generation_count;
        floating_cache[index] = {location.stack_offset, current_result_location ? std::optional<machine::VirtualRegister>(instruction_result_owner) : std::nullopt, generation, ++spill_cache_clock, wide, true};
        update_peak_residency();
        ++encoded.spill_store_cache_count;
        ++encoded.deferred_spill_store_count;
    };
    const auto write_integer_cached32 = [&](const machine::AllocationLocation& location, Register source) {
        write_integer_cached(location, source, false);
    };
    const auto write_integer_cached64 = [&](const machine::AllocationLocation& location, Register source) {
        write_integer_cached(location, source, true);
    };
    struct PreparedCall {
        std::uint32_t area{};
        std::optional<std::int32_t> preserved_pointer_offset;
    };
    const auto prepare_call_arguments = [&](std::span<const machine::VirtualRegister> call_arguments,
                                            std::optional<machine::VirtualRegister> preserved_pointer = std::nullopt) -> PreparedCall {
        std::vector<machine::RegisterClass> classes;
        classes.reserve(call_arguments.size());
        bool has_integer = false;
        bool has_floating = false;
        for (const auto reg : call_arguments) {
            const auto register_class = function.register_classes[reg];
            classes.push_back(register_class);
            has_integer = has_integer || register_class == machine::RegisterClass::integer;
            has_floating = has_floating || register_class == machine::RegisterClass::floating;
        }
        if (has_integer && has_floating) ++encoded.abi_mixed_class_call_count;

        const auto placements = classify_arguments(classes, abi);
        std::uint32_t stack_count = 0;
        for (const auto& placement : placements)
            if (placement.kind == AbiPlacement::Kind::stack)
                stack_count = std::max(stack_count, placement.stack_index + 1U);
        encoded.abi_stack_argument_count += stack_count;

        const auto stack_base = abi == Abi::windows ? 32U : 0U;
        if (abi == Abi::windows) encoded.abi_shadow_space_byte_count += 32U;
        const bool has_preserved_pointer_offset = preserved_pointer.has_value();
        const std::uint32_t preserved_pointer_offset =
            has_preserved_pointer_offset ? stack_base + stack_count * 8U : 0U;
        const auto raw_call_area = stack_base + stack_count * 8U + (preserved_pointer ? 8U : 0U);
        // Include the return address and, when present, the saved frame
        // pointer. Callee-saved pushes are already reflected explicitly.
        const auto fixed_depth = static_cast<std::uint32_t>(
            callee_saved.size() * 8U + (omit_frame_pointer ? 8U : 16U));
        const auto alignment_pad = (16U - ((fixed_depth + raw_call_area) & 15U)) & 15U;
        encoded.abi_alignment_padding_byte_count += alignment_pad;
        const auto call_area = raw_call_area + alignment_pad;
        emit_adjust_rsp(out, true, call_area);

        if (preserved_pointer && has_preserved_pointer_offset) {
            emit_read_location64(out, Register::eax, allocation.location(*preserved_pointer));
            emit_store_rsp64(out, Register::eax, static_cast<std::int32_t>(preserved_pointer_offset));
        }

        // Stack arguments must be committed before any ABI-register placement,
        // because their sources may currently occupy a destination register.
        for (std::size_t index = 0; index < call_arguments.size(); ++index) {
            const auto placement = placements[index];
            if (placement.kind != AbiPlacement::Kind::stack) continue;
            const auto reg = call_arguments[index];
            const bool floating = function.register_classes[reg] == machine::RegisterClass::floating;
            const bool wide = function.register_widths[reg] == 8;
            const auto destination_offset = stack_base + placement.stack_index * 8U;
            if (floating) {
                if (!direct_call_float_next_call[reg])
                    emit_read_float_location(out, XmmRegister::xmm0, allocation.location(reg), wide);
                emit_sse_rsp_store(out, XmmRegister::xmm0, static_cast<std::int32_t>(destination_offset), wide);
            } else {
                if (!direct_call_integer_next_call[reg]) {
                    if (wide) emit_read_location64(out, Register::eax, allocation.location(reg));
                    else emit_read_location(out, Register::eax, allocation.location(reg));
                }
                if (wide) emit_store_rsp64(out, Register::eax, static_cast<std::int32_t>(destination_offset));
                else emit_store_rsp(out, Register::eax, static_cast<std::int32_t>(destination_offset));
            }
        }

        struct PendingXmmCopy {
            machine::VirtualRegister source{};
            XmmRegister destination{XmmRegister::xmm0};
            bool wide{};
            bool emitted{};
        };
        std::vector<PendingXmmCopy> floating_pending;
        for (std::size_t index = 0; index < call_arguments.size(); ++index) {
            const auto placement = placements[index];
            if (placement.kind != AbiPlacement::Kind::xmm) continue;
            const auto source = call_arguments[index];
            const auto destination = static_cast<XmmRegister>(placement.index);
            const auto& source_location = allocation.location(source);
            if ((direct_call_float_next_call[source] && destination == XmmRegister::xmm0) ||
                (source_location.kind == machine::LocationKind::floating_register &&
                 floating_register(source_location.floating) == destination))
                continue;
            floating_pending.push_back({source, destination, function.register_widths[source] == 8U, false});
        }

        const auto floating_source_is_register = [&](const PendingXmmCopy& copy, XmmRegister reg) {
            if (direct_call_float_next_call[copy.source]) return reg == XmmRegister::xmm0;
            const auto& location = allocation.location(copy.source);
            return location.kind == machine::LocationKind::floating_register &&
                   floating_register(location.floating) == reg;
        };
        const auto emit_xmm_copy = [&](PendingXmmCopy& copy) {
            if (direct_call_float_next_call[copy.source])
                emit_sse_move(out, copy.destination, XmmRegister::xmm0, copy.wide);
            else
                emit_read_float_location(out, copy.destination, allocation.location(copy.source), copy.wide);
        };

        emit_acyclic_parallel_copies(
            floating_pending,
            [&](const std::vector<PendingXmmCopy>& copies, std::size_t copy_index) {
                const auto destination = copies[copy_index].destination;
                for (std::size_t other = 0; other < copies.size(); ++other) {
                    if (other == copy_index || copies[other].emitted) continue;
                    if (floating_source_is_register(copies[other], destination)) return true;
                }
                return false;
            },
            emit_xmm_copy);

        auto floating_cycles = unresolved_parallel_copy_cycles(
            floating_pending,
            [&](const PendingXmmCopy& candidate, const PendingXmmCopy& current) {
                return floating_source_is_register(current, candidate.destination);
            });
        for (auto& cycle : floating_cycles) {
            if (cycle.empty()) continue;
            const bool homogeneous_width = std::all_of(cycle.begin(), cycle.end(), [&](const PendingXmmCopy* copy) {
                return copy->wide == cycle.front()->wide;
            });
            const auto first_source_location = allocation.location(cycle.front()->source);
            const bool register_cycle = std::all_of(cycle.begin(), cycle.end(), [&](const PendingXmmCopy* copy) {
                return allocation.location(copy->source).kind == machine::LocationKind::floating_register;
            });
            if (homogeneous_width && register_cycle) {
                const auto first_source_register = floating_register(first_source_location.floating);
                emit_sse_move(out, XmmRegister::xmm0, first_source_register, cycle.front()->wide);
                auto hole = first_source_register;
                std::vector<bool> rotated(cycle.size(), false);
                rotated.front() = true;
                std::size_t rotated_count = 1U;
                while (rotated_count < cycle.size()) {
                    bool found = false;
                    for (std::size_t index = 1; index < cycle.size(); ++index) {
                        if (rotated[index] || cycle[index]->destination != hole) continue;
                        const auto source_register = floating_register(allocation.location(cycle[index]->source).floating);
                        emit_sse_move(out, cycle[index]->destination, source_register, cycle[index]->wide);
                        hole = source_register;
                        rotated[index] = true;
                        ++rotated_count;
                        found = true;
                        break;
                    }
                    if (!found) break;
                }
                if (rotated_count == cycle.size() && hole == cycle.front()->destination) {
                    emit_sse_move(out, cycle.front()->destination, XmmRegister::xmm0, cycle.front()->wide);
                    for (auto* copy : cycle) copy->emitted = true;
                    continue;
                }
            }

            const auto scratch_size = static_cast<std::uint32_t>(cycle.size() * 8U);
            emit_adjust_rsp(out, true, scratch_size);
            for (std::size_t index = 0; index < cycle.size(); ++index) {
                emit_read_float_location(out, XmmRegister::xmm0, allocation.location(cycle[index]->source), cycle[index]->wide);
                emit_sse_rsp_store(out, XmmRegister::xmm0, static_cast<std::int32_t>(index * 8U), cycle[index]->wide);
            }
            for (std::size_t index = 0; index < cycle.size(); ++index) {
                emit_sse_rsp_load(out, cycle[index]->destination, static_cast<std::int32_t>(index * 8U), cycle[index]->wide);
                cycle[index]->emitted = true;
            }
            emit_adjust_rsp(out, false, scratch_size);
        }

        struct PendingGprCopy {
            machine::VirtualRegister source{};
            Register destination{Register::eax};
            bool wide{};
            bool emitted{};
        };
        std::vector<PendingGprCopy> pending;
        for (std::size_t index = 0; index < call_arguments.size(); ++index) {
            const auto placement = placements[index];
            if (placement.kind != AbiPlacement::Kind::gpr) continue;
            const auto source = call_arguments[index];
            const auto destination = args[placement.index];
            const auto& source_location = allocation.location(source);
            if ((direct_call_integer_next_call[source] && destination == Register::eax) ||
                (source_location.kind == machine::LocationKind::physical_register &&
                 physical_register(source_location.physical) == destination))
                continue;
            pending.push_back({source, destination, function.register_widths[source] == 8U, false});
        }

        const auto source_is_register = [&](const PendingGprCopy& copy, Register reg) {
            if (direct_call_integer_next_call[copy.source]) return reg == Register::eax;
            const auto& location = allocation.location(copy.source);
            return location.kind == machine::LocationKind::physical_register &&
                   physical_register(location.physical) == reg;
        };
        const auto emit_gpr_copy = [&](PendingGprCopy& copy) {
            if (direct_call_integer_next_call[copy.source]) {
                if (copy.wide) emit_mov_register64(out, copy.destination, Register::eax);
                else emit_mov_register(out, copy.destination, Register::eax);
            } else if (copy.wide) emit_read_location64(out, copy.destination, allocation.location(copy.source));
            else emit_read_location(out, copy.destination, allocation.location(copy.source));
        };

        emit_acyclic_parallel_copies(
            pending,
            [&](const std::vector<PendingGprCopy>& copies, std::size_t copy_index) {
                const auto destination = copies[copy_index].destination;
                for (std::size_t other = 0; other < copies.size(); ++other) {
                    if (other == copy_index || copies[other].emitted) continue;
                    if (source_is_register(copies[other], destination)) return true;
                }
                return false;
            },
            emit_gpr_copy);

        auto cycles = unresolved_parallel_copy_cycles(
            pending,
            [&](const PendingGprCopy& candidate, const PendingGprCopy& current) {
                return source_is_register(current, candidate.destination);
            });
        for (auto& cycle : cycles) {
            if (cycle.empty()) continue;
            if (cycle.size() == 2U && cycle[0]->wide == cycle[1]->wide &&
                source_is_register(*cycle[0], cycle[1]->destination) &&
                source_is_register(*cycle[1], cycle[0]->destination)) {
                emit_xchg_registers(out, cycle[0]->destination, cycle[1]->destination, cycle[0]->wide);
                for (auto* copy : cycle) copy->emitted = true;
                continue;
            }

            const bool homogeneous_width = std::all_of(cycle.begin(), cycle.end(), [&](const PendingGprCopy* copy) {
                return copy->wide == cycle.front()->wide;
            });
            const auto& first_source_location = allocation.location(cycle.front()->source);
            if (homogeneous_width && first_source_location.kind == machine::LocationKind::physical_register) {
                const auto saved_source = physical_register(first_source_location.physical);
                if (cycle.front()->wide) emit_mov_register64(out, Register::eax, saved_source);
                else emit_mov_register(out, Register::eax, saved_source);

                auto hole = saved_source;
                std::vector<bool> rotated(cycle.size(), false);
                rotated.front() = true;
                std::size_t rotated_count = 1U;
                while (rotated_count < cycle.size()) {
                    bool found = false;
                    for (std::size_t index = 1; index < cycle.size(); ++index) {
                        if (rotated[index] || cycle[index]->destination != hole) continue;
                        emit_gpr_copy(*cycle[index]);
                        const auto& location = allocation.location(cycle[index]->source);
                        if (location.kind != machine::LocationKind::physical_register) break;
                        hole = physical_register(location.physical);
                        rotated[index] = true;
                        ++rotated_count;
                        found = true;
                        break;
                    }
                    if (!found) break;
                }
                if (rotated_count == cycle.size() && hole == cycle.front()->destination) {
                    if (cycle.front()->wide) emit_mov_register64(out, cycle.front()->destination, Register::eax);
                    else emit_mov_register(out, cycle.front()->destination, Register::eax);
                    for (auto* copy : cycle) copy->emitted = true;
                    continue;
                }
            }

            // Rare unresolved integer cycles retain a local snapshot fallback.
            const auto scratch_size = static_cast<std::uint32_t>(cycle.size() * 8U);
            emit_adjust_rsp(out, true, scratch_size);
            for (std::size_t index = 0; index < cycle.size(); ++index) {
                if (cycle[index]->wide) {
                    emit_read_location64(out, Register::eax, allocation.location(cycle[index]->source));
                    emit_store_rsp64(out, Register::eax, static_cast<std::int32_t>(index * 8U));
                } else {
                    emit_read_location(out, Register::eax, allocation.location(cycle[index]->source));
                    emit_store_rsp(out, Register::eax, static_cast<std::int32_t>(index * 8U));
                }
            }
            for (std::size_t index = 0; index < cycle.size(); ++index) {
                if (cycle[index]->wide) {
                    emit_load_rsp64(out, cycle[index]->destination, static_cast<std::int32_t>(index * 8U));
                } else {
                    emit_load_rsp(out, cycle[index]->destination, static_cast<std::int32_t>(index * 8U));
                }
                cycle[index]->emitted = true;
            }
            emit_adjust_rsp(out, false, scratch_size);
        }

        std::optional<std::int32_t> encoded_preserved_pointer_offset;
        if (has_preserved_pointer_offset)
            encoded_preserved_pointer_offset = static_cast<std::int32_t>(preserved_pointer_offset);
        return {call_area, encoded_preserved_pointer_offset};
    };
    if (!omit_frame_pointer) {
        out.byte(0x55);
        out.byte(0x48); out.byte(0x89); out.byte(0xE5);
    }
    if (encoded_frame_size != 0) {
        out.byte(0x48); out.byte(0x81); out.byte(0xEC); out.i32(static_cast<std::int32_t>(encoded_frame_size));
    }
    if (capture_r8) emit_store_stack64(out, Register::r8d, r8_capture_offset);
    if (capture_r9) emit_store_stack64(out, Register::r9d, r9_capture_offset);
    for (const auto reg : callee_saved) emit_push_physical(out, reg);

    const auto emit_epilogue = [&] {
        for (auto iterator = callee_saved.rbegin(); iterator != callee_saved.rend(); ++iterator)
            emit_pop_physical(out, *iterator);
        if (!omit_frame_pointer) out.byte(0xC9); // leave
        else ++encoded.leaf_frame_byte_avoided_count;
        out.byte(0xC3); // ret
    };

    std::unordered_map<std::string, std::size_t> labels;
    std::vector<JumpFixup> fixups;
    std::vector<LocalBranchFixup> local_branch_fixups;
    std::unordered_map<std::string, std::uint32_t> predecessor_counts;
    for (const auto& source_block : function.blocks)
        for (const auto& source_instruction : source_block.instructions)
            for (const auto& successor : source_instruction.successors)
                ++predecessor_counts[successor.block];
    bool preserve_cache_into_next_block = false;

    for (std::size_t block_index = 0; block_index < function.blocks.size(); ++block_index) {
        const auto& block = function.blocks[block_index];
        const std::string* next_block_name = block_index + 1U < function.blocks.size()
            ? &function.blocks[block_index + 1U].name : nullptr;
        if (!preserve_cache_into_next_block) invalidate_spill_caches();
        else ++encoded.spill_cache_edge_preservation_count;
        preserve_cache_into_next_block = false;
        labels.emplace(block.name, out.size());
        for (std::size_t instruction_index = 0; instruction_index < block.instructions.size(); ++instruction_index) {
            const auto& instruction = block.instructions[instruction_index];

            // Entry floating arguments are also a parallel copy from ABI XMM
            // or stack locations into allocated locations.  Resolving the whole
            // contiguous run avoids forcing every incoming floating value to a
            // stack slot merely to protect later argument loads.
            const bool floating_argument_load =
                instruction.opcode == machine::Opcode::load_argument_f32 ||
                instruction.opcode == machine::Opcode::load_argument_f64;
            if (block_index == 0U && floating_argument_load) {
                std::size_t run_end = instruction_index;
                while (run_end < block.instructions.size() &&
                       (block.instructions[run_end].opcode == machine::Opcode::load_argument_f32 ||
                        block.instructions[run_end].opcode == machine::Opcode::load_argument_f64))
                    ++run_end;

                struct FloatingEntryCopy {
                    std::optional<XmmRegister> source_register;
                    std::optional<std::int32_t> source_stack_offset;
                    machine::AllocationLocation destination;
                    bool wide{};
                    bool emitted{};
                };
                std::vector<FloatingEntryCopy> copies;
                copies.reserve(run_end - instruction_index);
                for (std::size_t index = instruction_index; index < run_end; ++index) {
                    const auto& load = block.instructions[index];
                    if (load.argument_index >= function.argument_count || load.result >= function.register_count) {
                        add_error(diagnostics, "invalid floating entry argument copy in @" + function.name);
                        return encoded;
                    }
                    const auto placement = argument_placements[load.argument_index];
                    FloatingEntryCopy copy;
                    copy.destination = allocation.location(load.result);
                    copy.wide = load.opcode == machine::Opcode::load_argument_f64;
                    if (placement.kind == AbiPlacement::Kind::xmm) {
                        copy.source_register = static_cast<XmmRegister>(placement.index);
                    } else if (placement.kind == AbiPlacement::Kind::stack) {
                        const auto base = abi == Abi::windows ? 48U : 16U;
                        copy.source_stack_offset = static_cast<std::int32_t>(base + placement.stack_index * 8U);
                    } else {
                        add_error(diagnostics, "floating argument ABI class mismatch in @" + function.name);
                        return encoded;
                    }
                    if (copy.source_register && copy.destination.kind == machine::LocationKind::floating_register &&
                        floating_register(copy.destination.floating) == *copy.source_register)
                        copy.emitted = true;
                    copies.push_back(copy);
                }

                const auto emit_floating_entry_copy = [&](FloatingEntryCopy& copy) {
                    if (copy.source_register) {
                        emit_write_float_location(out, copy.destination, *copy.source_register, copy.wide);
                    } else if (copy.destination.kind == machine::LocationKind::floating_register) {
                        emit_sse_stack_load(out, floating_register(copy.destination.floating),
                                            *copy.source_stack_offset, copy.wide);
                    } else {
                        emit_sse_stack_load(out, XmmRegister::xmm0, *copy.source_stack_offset, copy.wide);
                        emit_write_float_location(out, copy.destination, XmmRegister::xmm0, copy.wide);
                    }
                    copy.emitted = true;
                };

                emit_acyclic_parallel_copies(
                    copies,
                    [&](const std::vector<FloatingEntryCopy>& all_copies, std::size_t copy_index) {
                        const auto& copy = all_copies[copy_index];
                        if (copy.destination.kind == machine::LocationKind::floating_register) {
                            const auto destination = floating_register(copy.destination.floating);
                            for (std::size_t other = 0; other < all_copies.size(); ++other) {
                                if (other == copy_index || all_copies[other].emitted) continue;
                                if (all_copies[other].source_register && *all_copies[other].source_register == destination)
                                    return true;
                            }
                            return false;
                        }
                        // A stack destination needs xmm0 as a temporary. Do not
                        // clobber an unconsumed incoming xmm0 argument.
                        if (!copy.source_register) {
                            for (std::size_t other = 0; other < all_copies.size(); ++other) {
                                if (other == copy_index || all_copies[other].emitted) continue;
                                if (all_copies[other].source_register &&
                                    *all_copies[other].source_register == XmmRegister::xmm0)
                                    return true;
                            }
                        }
                        return false;
                    },
                    emit_floating_entry_copy);

                auto cycles = unresolved_parallel_copy_cycles(
                    copies,
                    [&](const FloatingEntryCopy& candidate, const FloatingEntryCopy& current) {
                        return candidate.destination.kind == machine::LocationKind::floating_register &&
                               current.source_register &&
                               floating_register(candidate.destination.floating) == *current.source_register;
                    });
                for (auto& cycle : cycles) {
                    if (cycle.empty()) continue;
                    // XMM has no direct exchange instruction and every incoming
                    // ABI XMM register can be live. Snapshot only this genuine
                    // cycle, leaving all acyclic entry values in registers.
                    const auto scratch_size = static_cast<std::uint32_t>(cycle.size() * 8U);
                    emit_adjust_rsp(out, true, scratch_size);
                    for (std::size_t index = 0; index < cycle.size(); ++index) {
                        if (cycle[index]->source_register)
                            emit_sse_rsp_store(out, *cycle[index]->source_register,
                                               static_cast<std::int32_t>(index * 8U), cycle[index]->wide);
                        else {
                            emit_sse_stack_load(out, XmmRegister::xmm0,
                                                *cycle[index]->source_stack_offset + static_cast<std::int32_t>(scratch_size),
                                                cycle[index]->wide);
                            emit_sse_rsp_store(out, XmmRegister::xmm0,
                                               static_cast<std::int32_t>(index * 8U), cycle[index]->wide);
                        }
                    }
                    for (std::size_t index = 0; index < cycle.size(); ++index) {
                        emit_sse_rsp_load(out, XmmRegister::xmm0,
                                          static_cast<std::int32_t>(index * 8U), cycle[index]->wide);
                        emit_write_float_location(out, cycle[index]->destination, XmmRegister::xmm0,
                                                  cycle[index]->wide);
                        cycle[index]->emitted = true;
                    }
                    emit_adjust_rsp(out, false, scratch_size);
                }

                encoded.machine_instruction_count += static_cast<std::uint32_t>(run_end - instruction_index);
                invalidate_spill_caches();
                instruction_index = run_end - 1U;
                continue;
            }

            // Entry integer arguments are a parallel copy from ABI locations into
            // allocated locations. Resolve a whole contiguous run at once so an
            // allocation such as r8<-r9, r9<-r8 becomes XCHG instead of requiring
            // an entry stack snapshot. Stack arguments are stable sources and are
            // emitted as soon as their destination is no longer a live ABI source.
            const bool integer_argument_load =
                instruction.opcode == machine::Opcode::load_argument ||
                instruction.opcode == machine::Opcode::load_argument_i64;
            if (block_index == 0U && integer_argument_load) {
                std::size_t run_end = instruction_index;
                while (run_end < block.instructions.size() &&
                       (block.instructions[run_end].opcode == machine::Opcode::load_argument ||
                        block.instructions[run_end].opcode == machine::Opcode::load_argument_i64))
                    ++run_end;

                struct EntryCopy {
                    std::optional<Register> source_register;
                    std::optional<std::int32_t> source_stack_offset;
                    machine::AllocationLocation destination;
                    bool wide{};
                    bool emitted{};
                };
                std::vector<EntryCopy> copies;
                copies.reserve(run_end - instruction_index);
                bool valid_run = true;
                for (std::size_t index = instruction_index; index < run_end; ++index) {
                    const auto& load = block.instructions[index];
                    if (load.argument_index >= function.argument_count || load.result >= function.register_count) {
                        add_error(diagnostics, "invalid entry argument copy in @" + function.name);
                        valid_run = false;
                        break;
                    }
                    const auto placement = argument_placements[load.argument_index];
                    EntryCopy copy;
                    copy.destination = allocation.location(load.result);
                    copy.wide = load.opcode == machine::Opcode::load_argument_i64;
                    if (placement.kind == AbiPlacement::Kind::gpr) {
                        const auto source = args[placement.index];
                        if (source == Register::r8d && capture_r8)
                            copy.source_stack_offset = r8_capture_offset;
                        else if (source == Register::r9d && capture_r9)
                            copy.source_stack_offset = r9_capture_offset;
                        else
                            copy.source_register = source;
                    } else if (placement.kind == AbiPlacement::Kind::stack) {
                        const auto base = abi == Abi::windows ? 48U : 16U;
                        copy.source_stack_offset = static_cast<std::int32_t>(base + placement.stack_index * 8U);
                    } else {
                        add_error(diagnostics, "integer argument ABI class mismatch in @" + function.name);
                        valid_run = false;
                        break;
                    }
                    if (copy.source_register && copy.destination.kind == machine::LocationKind::physical_register &&
                        physical_register(copy.destination.physical) == *copy.source_register) {
                        copy.emitted = true;
                    }
                    copies.push_back(copy);
                }
                if (!valid_run) return encoded;

                const auto emit_entry_copy = [&](EntryCopy& copy, Register scratch = Register::eax) {
                    if (copy.source_register) {
                        if (copy.wide) emit_write_location64(out, copy.destination, *copy.source_register);
                        else emit_write_location(out, copy.destination, *copy.source_register);
                    } else {
                        if (copy.wide) emit_load_stack64(out, scratch, *copy.source_stack_offset);
                        else emit_load_stack(out, scratch, *copy.source_stack_offset);
                        if (copy.wide) emit_write_location64(out, copy.destination, scratch);
                        else emit_write_location(out, copy.destination, scratch);
                    }
                    copy.emitted = true;
                };

                emit_acyclic_parallel_copies(
                    copies,
                    [&](const std::vector<EntryCopy>& all_copies, std::size_t copy_index) {
                        const auto& copy = all_copies[copy_index];
                        if (copy.destination.kind != machine::LocationKind::physical_register) return false;
                        const auto destination_register = physical_register(copy.destination.physical);
                        for (std::size_t other_index = 0; other_index < all_copies.size(); ++other_index) {
                            if (other_index == copy_index || all_copies[other_index].emitted) continue;
                            if (all_copies[other_index].source_register &&
                                *all_copies[other_index].source_register == destination_register)
                                return true;
                        }
                        return false;
                    },
                    [&](EntryCopy& copy) { emit_entry_copy(copy); });

                auto cycles = unresolved_parallel_copy_cycles(
                    copies,
                    [&](const EntryCopy& candidate, const EntryCopy& current) {
                        return candidate.destination.kind == machine::LocationKind::physical_register &&
                               current.source_register &&
                               physical_register(candidate.destination.physical) == *current.source_register;
                    });
                for (auto& cyclic : cycles) {
                    if (cyclic.empty()) continue;
                    if (cyclic.size() == 2U && cyclic[0]->source_register && cyclic[1]->source_register &&
                        cyclic[0]->destination.kind == machine::LocationKind::physical_register &&
                        cyclic[1]->destination.kind == machine::LocationKind::physical_register &&
                        physical_register(cyclic[0]->destination.physical) == *cyclic[1]->source_register &&
                        physical_register(cyclic[1]->destination.physical) == *cyclic[0]->source_register &&
                        cyclic[0]->wide == cyclic[1]->wide) {
                        emit_xchg_registers(out, *cyclic[0]->source_register, *cyclic[1]->source_register,
                                            cyclic[0]->wide);
                        cyclic[0]->emitted = cyclic[1]->emitted = true;
                        continue;
                    }

                    const bool homogeneous = std::all_of(cyclic.begin(), cyclic.end(), [&](const EntryCopy* copy) {
                        return copy->source_register &&
                               copy->destination.kind == machine::LocationKind::physical_register &&
                               copy->wide == cyclic.front()->wide;
                    });
                    if (homogeneous) {
                        const auto saved_source = *cyclic.front()->source_register;
                        const auto first_destination = physical_register(cyclic.front()->destination.physical);
                        if (cyclic.front()->wide) emit_mov_register64(out, Register::eax, saved_source);
                        else emit_mov_register(out, Register::eax, saved_source);
                        auto hole = saved_source;
                        std::vector<bool> rotated(cyclic.size(), false);
                        rotated.front() = true;
                        std::size_t rotated_count = 1U;
                        while (rotated_count < cyclic.size()) {
                            bool found = false;
                            for (std::size_t index = 1; index < cyclic.size(); ++index) {
                                if (rotated[index] ||
                                    physical_register(cyclic[index]->destination.physical) != hole) continue;
                                const auto source = *cyclic[index]->source_register;
                                if (cyclic[index]->wide) emit_mov_register64(out, hole, source);
                                else emit_mov_register(out, hole, source);
                                hole = source;
                                rotated[index] = true;
                                ++rotated_count;
                                found = true;
                                break;
                            }
                            if (!found) break;
                        }
                        if (rotated_count == cyclic.size() && hole == first_destination) {
                            if (cyclic.front()->wide) emit_mov_register64(out, first_destination, Register::eax);
                            else emit_mov_register(out, first_destination, Register::eax);
                            for (auto* copy : cyclic) copy->emitted = true;
                            continue;
                        }
                    }

                    // Snapshot only this unresolved component. Independent cycles
                    // remain in registers and do not inherit another cycle's fallback.
                    const auto scratch_size = static_cast<std::uint32_t>(cyclic.size() * 8U);
                    emit_adjust_rsp(out, true, scratch_size);
                    for (std::size_t index = 0; index < cyclic.size(); ++index) {
                        if (!cyclic[index]->source_register) {
                            emit_entry_copy(*cyclic[index]);
                            continue;
                        }
                        const auto source = *cyclic[index]->source_register;
                        if (cyclic[index]->wide)
                            emit_store_rsp64(out, source, static_cast<std::int32_t>(index * 8U));
                        else
                            emit_store_rsp(out, source, static_cast<std::int32_t>(index * 8U));
                    }
                    for (std::size_t index = 0; index < cyclic.size(); ++index) {
                        if (cyclic[index]->emitted) continue;
                        if (cyclic[index]->wide)
                            emit_load_rsp64(out, Register::eax, static_cast<std::int32_t>(index * 8U));
                        else
                            emit_load_rsp(out, Register::eax, static_cast<std::int32_t>(index * 8U));
                        if (cyclic[index]->wide)
                            emit_write_location64(out, cyclic[index]->destination, Register::eax);
                        else
                            emit_write_location(out, cyclic[index]->destination, Register::eax);
                        cyclic[index]->emitted = true;
                    }
                    emit_adjust_rsp(out, false, scratch_size);
                }


                encoded.machine_instruction_count += static_cast<std::uint32_t>(run_end - instruction_index);
                invalidate_spill_caches();
                instruction_index = run_end - 1U;
                continue;
            }

            ++encoded.machine_instruction_count;
            current_result_location.reset();
            current_result_is_dead = false;
            if (instruction.result < function.register_count) {
                current_result_location = allocation.location(instruction.result);
                instruction_result_owner = instruction.result;
                current_result_is_dead = result_use_counts[instruction.result] == 0;
            }
            const auto check_reg = [&](machine::VirtualRegister reg) {
                return reg < function.register_count;
            };
            if (instruction.opcode != machine::Opcode::return_i32 &&
                instruction.opcode != machine::Opcode::return_i64 &&
                instruction.opcode != machine::Opcode::return_f32 &&
                instruction.opcode != machine::Opcode::return_f64 &&
                instruction.opcode != machine::Opcode::return_void &&
                instruction.opcode != machine::Opcode::return_aggregate &&
                instruction.opcode != machine::Opcode::call_void &&
                instruction.opcode != machine::Opcode::call_indirect_void &&
                instruction.opcode != machine::Opcode::jump &&
                instruction.opcode != machine::Opcode::branch_i1 &&
                instruction.opcode != machine::Opcode::store_stack_i32 &&
                instruction.opcode != machine::Opcode::store_stack_i64 &&
                instruction.opcode != machine::Opcode::store_stack_f32 &&
                instruction.opcode != machine::Opcode::store_stack_f64 &&
                instruction.opcode != machine::Opcode::store_stack_v128 &&
                instruction.opcode != machine::Opcode::store_stack_v256 &&
                instruction.opcode != machine::Opcode::store_stack_v512 &&
                instruction.opcode != machine::Opcode::store_ptr_i32 &&
                instruction.opcode != machine::Opcode::store_ptr_i64 &&
                instruction.opcode != machine::Opcode::store_ptr_f32 &&
                instruction.opcode != machine::Opcode::store_ptr_f64 &&
                !check_reg(instruction.result)) {
                add_error(diagnostics, "invalid result virtual register in @" + function.name);
                return encoded;
            }
            for (const auto input : instruction.inputs) {
                if (!check_reg(input)) {
                    add_error(diagnostics, "invalid input virtual register in @" + function.name);
                    return encoded;
                }
            }

            const bool preserves_fallthrough_edge =
                instruction.opcode == machine::Opcode::jump && next_block_name != nullptr &&
                instruction.successors.size() == 1U && instruction.successors.front().block == *next_block_name &&
                instruction.successors.front().arguments.empty() && predecessor_counts[*next_block_name] == 1U;
            const bool preserves_spill_cache = preserves_fallthrough_edge ||
                instruction.opcode == machine::Opcode::add_i32 || instruction.opcode == machine::Opcode::sub_i32 ||
                instruction.opcode == machine::Opcode::mul_i32 || instruction.opcode == machine::Opcode::and_i32 ||
                instruction.opcode == machine::Opcode::or_i32 || instruction.opcode == machine::Opcode::xor_i32 ||
                instruction.opcode == machine::Opcode::add_i64 || instruction.opcode == machine::Opcode::sub_i64 ||
                instruction.opcode == machine::Opcode::mul_i64 || instruction.opcode == machine::Opcode::and_i64 ||
                instruction.opcode == machine::Opcode::or_i64 || instruction.opcode == machine::Opcode::xor_i64 ||
                instruction.opcode == machine::Opcode::add_f32 || instruction.opcode == machine::Opcode::sub_f32 ||
                instruction.opcode == machine::Opcode::mul_f32 || instruction.opcode == machine::Opcode::div_f32 ||
                instruction.opcode == machine::Opcode::add_f64 || instruction.opcode == machine::Opcode::sub_f64 ||
                instruction.opcode == machine::Opcode::mul_f64 || instruction.opcode == machine::Opcode::div_f64;
            if (!preserves_spill_cache) invalidate_spill_caches();
            else ++encoded.spill_cache_preserved_instruction_count;
            store_forwarding_enabled =
                instruction.opcode != machine::Opcode::load_argument &&
                instruction.opcode != machine::Opcode::load_argument_i64 &&
                instruction.opcode != machine::Opcode::load_argument_f32 &&
                instruction.opcode != machine::Opcode::load_argument_f64;

            switch (instruction.opcode) {
            case machine::Opcode::load_argument_f32:
            case machine::Opcode::load_argument_f64:
            case machine::Opcode::load_argument:
            case machine::Opcode::load_argument_i64: {
                if (instruction.argument_index >= function.argument_count) {
                    add_error(diagnostics, "invalid argument index in @" + function.name);
                    return encoded;
                }
                const auto placements = classify_arguments(function.argument_classes, abi);
                const auto placement = placements[instruction.argument_index];
                const bool floating = instruction.opcode == machine::Opcode::load_argument_f32 || instruction.opcode == machine::Opcode::load_argument_f64;
                const bool wide = instruction.opcode == machine::Opcode::load_argument_f64 || instruction.opcode == machine::Opcode::load_argument_i64;
                if (floating && placement.kind == AbiPlacement::Kind::xmm) {
                    write_floating_cached(allocation.location(instruction.result), static_cast<XmmRegister>(placement.index), wide);
                } else if (!floating && placement.kind == AbiPlacement::Kind::gpr) {
                    const auto source = args[placement.index];
                    if (source == Register::r8d && capture_r8) {
                        if (wide) emit_load_stack64(out, Register::eax, r8_capture_offset);
                        else emit_load_stack(out, Register::eax, r8_capture_offset);
                        if (wide) write_integer_cached64(allocation.location(instruction.result), Register::eax);
                        else write_integer_cached32(allocation.location(instruction.result), Register::eax);
                    } else if (source == Register::r9d && capture_r9) {
                        if (wide) emit_load_stack64(out, Register::eax, r9_capture_offset);
                        else emit_load_stack(out, Register::eax, r9_capture_offset);
                        if (wide) write_integer_cached64(allocation.location(instruction.result), Register::eax);
                        else write_integer_cached32(allocation.location(instruction.result), Register::eax);
                    } else if (wide) write_integer_cached64(allocation.location(instruction.result), source);
                    else write_integer_cached32(allocation.location(instruction.result), source);
                } else if (placement.kind == AbiPlacement::Kind::stack) {
                    const auto base = abi == Abi::windows ? 48U : 16U;
                    const auto offset = static_cast<std::int32_t>(base + placement.stack_index * 8U);
                    if (floating) {
                        emit_sse_stack_load(out, XmmRegister::xmm0, offset, wide);
                        write_floating_cached(allocation.location(instruction.result), XmmRegister::xmm0, wide);
                    } else if (wide) {
                        emit_load_stack64(out, Register::eax, offset);
                        write_integer_cached64(allocation.location(instruction.result), Register::eax);
                    } else {
                        emit_load_stack(out, Register::eax, offset);
                        write_integer_cached32(allocation.location(instruction.result), Register::eax);
                    }
                } else {
                    add_error(diagnostics, "argument ABI class mismatch in @" + function.name);
                    return encoded;
                }
                break;
            }
            case machine::Opcode::load_immediate_f32: {
                const auto& destination = allocation.location(instruction.result);
                if (destination.kind == machine::LocationKind::rematerialized_floating) { ++encoded.rematerialized_definition_count; break; }
                const auto target = destination.kind == machine::LocationKind::floating_register
                    ? floating_register(destination.floating) : XmmRegister::xmm0;
                if (instruction.immediate == 0) {
                    emit_sse_xor(out, target, target, false);
                    ++encoded.floating_zeroing_idiom_count;
                } else {
                    emit_mov_imm32(out, Register::eax, static_cast<std::int32_t>(instruction.immediate));
                    emit_mov_gpr_to_xmm(out, target, Register::eax, false);
                }
                if (destination.kind != machine::LocationKind::floating_register)
                    write_floating_cached(destination, target, false);
                break;
            }
            case machine::Opcode::load_immediate_f64: {
                const auto& destination = allocation.location(instruction.result);
                if (destination.kind == machine::LocationKind::rematerialized_floating) { ++encoded.rematerialized_definition_count; break; }
                const auto target = destination.kind == machine::LocationKind::floating_register
                    ? floating_register(destination.floating) : XmmRegister::xmm0;
                if (instruction.immediate == 0) {
                    emit_sse_xor(out, target, target, true);
                    ++encoded.floating_zeroing_idiom_count;
                } else {
                    emit_mov_imm64(out, Register::eax, instruction.immediate);
                    emit_mov_gpr_to_xmm(out, target, Register::eax, true);
                }
                if (destination.kind != machine::LocationKind::floating_register)
                    write_floating_cached(destination, target, true);
                break;
            }
            case machine::Opcode::load_immediate_i64: {
                const auto& destination = allocation.location(instruction.result);
                if (destination.kind == machine::LocationKind::rematerialized_integer) { ++encoded.rematerialized_definition_count; break; }
                if (destination.kind == machine::LocationKind::physical_register) {
                    emit_mov_imm64(out, physical_register(destination.physical), instruction.immediate);
                } else {
                    emit_mov_imm64(out, Register::eax, instruction.immediate);
                    write_integer_cached64(destination, Register::eax);
                }
                break;
            }
            case machine::Opcode::load_immediate:
                if (allocation.location(instruction.result).kind == machine::LocationKind::rematerialized_integer) { ++encoded.rematerialized_definition_count; break; }
                if (instruction.immediate < std::numeric_limits<std::int32_t>::min() ||
                    instruction.immediate > std::numeric_limits<std::int32_t>::max()) {
                    add_error(diagnostics, "i32 immediate out of range in @" + function.name);
                    return encoded;
                }
                emit_mov_imm32(out, Register::eax, static_cast<std::int32_t>(instruction.immediate));
                write_integer_cached32(allocation.location(instruction.result), Register::eax);
                break;
            case machine::Opcode::load_function_address: {
                if (instruction.symbol.empty()) {
                    add_error(diagnostics, "empty function address target in @" + function.name);
                    return encoded;
                }
                out.byte(0x48); out.byte(0x8D); out.byte(0x05); // lea rax, [rip + rel32]
                const auto displacement_offset = out.size();
                out.i32(0);
                encoded.addresses.push_back({displacement_offset, instruction.symbol});
                write_integer_cached64(allocation.location(instruction.result), Register::eax);
                break;
            }
            case machine::Opcode::load_global_address: {
                if (instruction.symbol.empty()) { add_error(diagnostics, "empty global address target in @" + function.name); return encoded; }
                out.byte(0x48); out.byte(0xB8); // mov rax, imm64
                const auto address_offset = out.size();
                out.i64(0);
                encoded.global_addresses.push_back({address_offset, instruction.symbol});
                write_integer_cached64(allocation.location(instruction.result), Register::eax);
                break;
            }
            case machine::Opcode::load_tls_address: {
                if (instruction.symbol.empty()) { add_error(diagnostics, "empty TLS address target in @" + function.name); return encoded; }
                // Reserve a fixed 32-byte object-rewrite slot. ELF x86-64 uses
                // initial-exec TLS; COFF x64 uses _tls_index + GS:[0x58].
                const auto address_offset = out.size();
                for (int index = 0; index < 32; ++index) out.byte(0x90);
                encoded.tls_addresses.push_back({address_offset, instruction.symbol});
                write_integer_cached64(allocation.location(instruction.result), Register::eax);
                break;
            }
            case machine::Opcode::load_stack_address:
                if (instruction.immediate >= 0 || -instruction.immediate > static_cast<std::int64_t>(function.local_stack_size)) {
                    add_error(diagnostics, "invalid stack address in @" + function.name); return encoded;
                }
                out.byte(0x48); out.byte(0x8D); out.byte(0x85); out.i32(static_cast<std::int32_t>(instruction.immediate));
                write_integer_cached64(allocation.location(instruction.result), Register::eax);
                break;
            case machine::Opcode::ptr_offset:
                if (instruction.inputs.size() != 1 || instruction.immediate < 0 || instruction.immediate > std::numeric_limits<std::int32_t>::max()) {
                    add_error(diagnostics, "malformed pointer offset in @" + function.name); return encoded;
                }
                emit_read_location64(out, Register::eax, allocation.location(instruction.inputs[0]));
                emit_add_imm64(out, Register::eax, static_cast<std::int32_t>(instruction.immediate));
                write_integer_cached64(allocation.location(instruction.result), Register::eax);
                break;
            case machine::Opcode::load_ptr_f32:
            case machine::Opcode::load_ptr_f64: {
                const bool wide = instruction.opcode == machine::Opcode::load_ptr_f64;
                if (instruction.inputs.size() != 1) { add_error(diagnostics, "malformed floating pointer load in @" + function.name); return encoded; }
                emit_read_location64(out, Register::ecx, allocation.location(instruction.inputs[0]));
                emit_sse_ptr_load(out, XmmRegister::xmm0, Register::ecx, wide, static_cast<std::int32_t>(instruction.immediate));
                write_floating_cached(allocation.location(instruction.result), XmmRegister::xmm0, wide);
                break;
            }
            case machine::Opcode::store_ptr_f32:
            case machine::Opcode::store_ptr_f64: {
                const bool wide = instruction.opcode == machine::Opcode::store_ptr_f64;
                if (instruction.inputs.size() != 2) { add_error(diagnostics, "malformed floating pointer store in @" + function.name); return encoded; }
                emit_read_float_location(out, XmmRegister::xmm0, allocation.location(instruction.inputs[0]), wide);
                read_integer_cached(Register::ecx, allocation.location(instruction.inputs[1]), true);
                emit_sse_ptr_store(out, Register::ecx, XmmRegister::xmm0, wide, static_cast<std::int32_t>(instruction.immediate));
                break;
            }
            case machine::Opcode::load_ptr_i8:
            case machine::Opcode::load_ptr_i16:
            case machine::Opcode::load_ptr_i32:
                if (instruction.inputs.size() != 1) { add_error(diagnostics, "malformed pointer load in @" + function.name); return encoded; }
                emit_read_location64(out, Register::ecx, allocation.location(instruction.inputs[0]));
                if (instruction.opcode == machine::Opcode::load_ptr_i8) emit_load_ptr_i8(out, Register::eax, Register::ecx, static_cast<std::int32_t>(instruction.immediate));
                else if (instruction.opcode == machine::Opcode::load_ptr_i16) emit_load_ptr_i16(out, Register::eax, Register::ecx, static_cast<std::int32_t>(instruction.immediate));
                else emit_load_ptr_i32(out, Register::eax, Register::ecx, static_cast<std::int32_t>(instruction.immediate));
                write_integer_cached32(allocation.location(instruction.result), Register::eax);
                break;
            case machine::Opcode::load_ptr_i64:
                if (instruction.inputs.size() != 1) { add_error(diagnostics, "malformed i64 pointer load in @" + function.name); return encoded; }
                emit_read_location64(out, Register::ecx, allocation.location(instruction.inputs[0]));
                emit_load_ptr_i64(out, Register::eax, Register::ecx, static_cast<std::int32_t>(instruction.immediate));
                write_integer_cached64(allocation.location(instruction.result), Register::eax);
                break;
            case machine::Opcode::store_ptr_i64:
                if (instruction.symbol == "$storeimm") {
                    if (instruction.inputs.size() != 1) { add_error(diagnostics, "malformed immediate i64 pointer store in @" + function.name); return encoded; }
                    read_integer_cached(Register::ecx, allocation.location(instruction.inputs[0]), true);
                    emit_store_ptr_immediate(out, Register::ecx, instruction.argument_index,
                                             static_cast<std::int32_t>(instruction.immediate), 8U);
                    break;
                }
                if (instruction.inputs.size() != 2) { add_error(diagnostics, "malformed i64 pointer store in @" + function.name); return encoded; }
                emit_read_location64(out, Register::eax, allocation.location(instruction.inputs[0]));
                read_integer_cached(Register::ecx, allocation.location(instruction.inputs[1]), true);
                emit_store_ptr_i64(out, Register::ecx, Register::eax, static_cast<std::int32_t>(instruction.immediate));
                break;
            case machine::Opcode::store_ptr_i8:
            case machine::Opcode::store_ptr_i16:
            case machine::Opcode::store_ptr_i32:
                if (instruction.symbol == "$storeimm") {
                    if (instruction.inputs.size() != 1) { add_error(diagnostics, "malformed immediate pointer store in @" + function.name); return encoded; }
                    read_integer_cached(Register::ecx, allocation.location(instruction.inputs[0]), true);
                    const auto width = instruction.opcode == machine::Opcode::store_ptr_i8 ? 1U :
                                       instruction.opcode == machine::Opcode::store_ptr_i16 ? 2U : 4U;
                    emit_store_ptr_immediate(out, Register::ecx, instruction.argument_index,
                                             static_cast<std::int32_t>(instruction.immediate), width);
                    break;
                }
                if (instruction.inputs.size() != 2) { add_error(diagnostics, "malformed pointer store in @" + function.name); return encoded; }
                emit_read_location(out, Register::eax, allocation.location(instruction.inputs[0]));
                read_integer_cached(Register::ecx, allocation.location(instruction.inputs[1]), true);
                if (instruction.opcode == machine::Opcode::store_ptr_i8) emit_store_ptr_i8(out, Register::ecx, Register::eax, static_cast<std::int32_t>(instruction.immediate));
                else if (instruction.opcode == machine::Opcode::store_ptr_i16) emit_store_ptr_i16(out, Register::ecx, Register::eax, static_cast<std::int32_t>(instruction.immediate));
                else emit_store_ptr_i32(out, Register::ecx, Register::eax, static_cast<std::int32_t>(instruction.immediate));
                break;
            case machine::Opcode::load_stack_f32:
            case machine::Opcode::load_stack_f64: {
                const bool wide = instruction.opcode == machine::Opcode::load_stack_f64;
                emit_sse_stack_load(out, XmmRegister::xmm0, static_cast<std::int32_t>(instruction.immediate), wide);
                write_floating_cached(allocation.location(instruction.result), XmmRegister::xmm0, wide);
                break;
            }
            case machine::Opcode::store_stack_f32:
            case machine::Opcode::store_stack_f64: {
                const bool wide = instruction.opcode == machine::Opcode::store_stack_f64;
                emit_read_float_location(out, XmmRegister::xmm0, allocation.location(instruction.inputs[0]), wide);
                emit_sse_stack_store(out, XmmRegister::xmm0, static_cast<std::int32_t>(instruction.immediate), wide);
                break;
            }
            case machine::Opcode::load_stack_v128:
            case machine::Opcode::load_stack_v256:
            case machine::Opcode::load_stack_v512: {
                const std::uint16_t bits = instruction.opcode == machine::Opcode::load_stack_v512 ? 512U
                    : instruction.opcode == machine::Opcode::load_stack_v256 ? 256U : 128U;
                emit_vector_stack_move(out, XmmRegister::xmm0,
                                       static_cast<std::int32_t>(instruction.immediate), bits, false);
                write_floating_cached(allocation.location(instruction.result), XmmRegister::xmm0, true);
                break;
            }
            case machine::Opcode::store_stack_v128:
            case machine::Opcode::store_stack_v256:
            case machine::Opcode::store_stack_v512: {
                const std::uint16_t bits = instruction.opcode == machine::Opcode::store_stack_v512 ? 512U
                    : instruction.opcode == machine::Opcode::store_stack_v256 ? 256U : 128U;
                emit_read_float_location(out, XmmRegister::xmm0,
                                         allocation.location(instruction.inputs[0]), true);
                emit_vector_stack_move(out, XmmRegister::xmm0,
                                       static_cast<std::int32_t>(instruction.immediate), bits, true);
                break;
            }
            case machine::Opcode::copy_f32:
            case machine::Opcode::copy_f64: {
                const auto& source = allocation.location(instruction.inputs[0]);
                const auto& destination = allocation.location(instruction.result);
                if (same_location(source, destination)) {
                    ++encoded.eliminated_copy_count;
                    break;
                }
                ++encoded.emitted_copy_count;
                const bool wide = instruction.opcode == machine::Opcode::copy_f64;
                emit_read_float_location(out, XmmRegister::xmm0, source, wide);
                write_floating_cached(destination, XmmRegister::xmm0, wide);
                break;
            }
            case machine::Opcode::add_f32: case machine::Opcode::add_f64:
            case machine::Opcode::sub_f32: case machine::Opcode::sub_f64:
            case machine::Opcode::mul_f32: case machine::Opcode::mul_f64:
            case machine::Opcode::div_f32: case machine::Opcode::div_f64: {
                const bool wide = instruction.opcode == machine::Opcode::add_f64 || instruction.opcode == machine::Opcode::sub_f64 || instruction.opcode == machine::Opcode::mul_f64 || instruction.opcode == machine::Opcode::div_f64;
                const auto& left = allocation.location(instruction.inputs[0]);
                const auto& right = allocation.location(instruction.inputs[1]);
                const auto& destination = allocation.location(instruction.result);
                const std::uint8_t op = (instruction.opcode == machine::Opcode::add_f32 || instruction.opcode == machine::Opcode::add_f64) ? 0x58 :
                                (instruction.opcode == machine::Opcode::sub_f32 || instruction.opcode == machine::Opcode::sub_f64) ? 0x5C :
                                (instruction.opcode == machine::Opcode::mul_f32 || instruction.opcode == machine::Opcode::mul_f64) ? 0x59 : 0x5E;
                const bool commutative = instruction.opcode == machine::Opcode::add_f32 ||
                                         instruction.opcode == machine::Opcode::add_f64 ||
                                         instruction.opcode == machine::Opcode::mul_f32 ||
                                         instruction.opcode == machine::Opcode::mul_f64;
                const bool forwarded_left = instruction.inputs[0] < direct_call_float_arithmetic.size() &&
                                            direct_call_float_arithmetic[instruction.inputs[0]];
                const bool forwarded_right = commutative &&
                                             instruction.inputs[1] < direct_call_float_arithmetic.size() &&
                                             direct_call_float_arithmetic[instruction.inputs[1]];
                if (forwarded_left || forwarded_right) {
                    const auto other = forwarded_left ? instruction.inputs[1] : instruction.inputs[0];
                    read_floating_cached(XmmRegister::xmm1, allocation.location(other), wide);
                    emit_sse_binary(out, XmmRegister::xmm0, XmmRegister::xmm1, wide, op);
                    write_floating_cached(destination, XmmRegister::xmm0, wide);
                    ++encoded.two_address_reuse_count;
                    break;
                }
                if (destination.kind == machine::LocationKind::floating_register && same_location(destination, left)) {
                    const auto destination_register = floating_register(destination.floating);
                    const auto source_register = right.kind == machine::LocationKind::floating_register
                        ? floating_register(right.floating) : XmmRegister::xmm0;
                    if (right.kind != machine::LocationKind::floating_register)
                        emit_read_float_location(out, source_register, right, wide);
                    emit_sse_binary(out, destination_register, source_register, wide, op);
                    ++encoded.two_address_reuse_count;
                    break;
                }
                if (commutative && destination.kind == machine::LocationKind::floating_register &&
                    same_location(destination, right)) {
                    const auto destination_register = floating_register(destination.floating);
                    const auto source_register = left.kind == machine::LocationKind::floating_register
                        ? floating_register(left.floating) : XmmRegister::xmm0;
                    if (left.kind != machine::LocationKind::floating_register)
                        emit_read_float_location(out, source_register, left, wide);
                    emit_sse_binary(out, destination_register, source_register, wide, op);
                    ++encoded.two_address_reuse_count;
                    break;
                }
                read_floating_cached(XmmRegister::xmm0, left, wide);
                if (instruction.inputs[0] == instruction.inputs[1] && left.kind == machine::LocationKind::stack_slot) {
                    emit_sse_move(out, XmmRegister::xmm1, XmmRegister::xmm0, wide);
                    ++encoded.redundant_spill_load_count;
                } else {
                    read_floating_cached(XmmRegister::xmm1, right, wide);
                }
                emit_sse_binary(out, XmmRegister::xmm0, XmmRegister::xmm1, wide, op);
                write_floating_cached(allocation.location(instruction.result), XmmRegister::xmm0, wide);
                break;
            }
            case machine::Opcode::neg_f32:
            case machine::Opcode::neg_f64: {
                if (instruction.inputs.size() != 1) {
                    add_error(diagnostics, "malformed floating negation in @" + function.name);
                    return encoded;
                }
                const bool wide = instruction.opcode == machine::Opcode::neg_f64;
                const auto& src = allocation.location(instruction.inputs[0]);
                const auto& dst = allocation.location(instruction.result);
                emit_mov_imm64(out, Register::eax, wide
                    ? static_cast<std::int64_t>(0x8000000000000000ULL)
                    : static_cast<std::int64_t>(0x80000000U));
                emit_mov_gpr_to_xmm(out, XmmRegister::xmm0, Register::eax, wide);
                if (dst.kind == machine::LocationKind::floating_register && same_location(dst, src)) {
                    emit_sse_xor(out, floating_register(dst.floating), XmmRegister::xmm0, wide);
                    ++encoded.unary_reuse_count;
                    break;
                }
                emit_read_float_location(out, XmmRegister::xmm1, src, wide);
                emit_sse_xor(out, XmmRegister::xmm1, XmmRegister::xmm0, wide);
                write_floating_cached(dst, XmmRegister::xmm1, wide);
                break;
            }
            case machine::Opcode::cmp_eq_f32: case machine::Opcode::cmp_ne_f32:
            case machine::Opcode::cmp_lt_f32: case machine::Opcode::cmp_le_f32:
            case machine::Opcode::cmp_gt_f32: case machine::Opcode::cmp_ge_f32:
            case machine::Opcode::cmp_eq_f64: case machine::Opcode::cmp_ne_f64:
            case machine::Opcode::cmp_lt_f64: case machine::Opcode::cmp_le_f64:
            case machine::Opcode::cmp_gt_f64: case machine::Opcode::cmp_ge_f64: {
                const bool wide = instruction.opcode >= machine::Opcode::cmp_eq_f64 && instruction.opcode <= machine::Opcode::cmp_ge_f64;
                emit_read_float_location(out, XmmRegister::xmm0, allocation.location(instruction.inputs[0]), wide);
                emit_read_float_location(out, XmmRegister::xmm1, allocation.location(instruction.inputs[1]), wide);
                emit_ucomi(out, XmmRegister::xmm0, XmmRegister::xmm1, wide);
                const bool needs_nan_guard =
                    instruction.opcode == machine::Opcode::cmp_eq_f32 || instruction.opcode == machine::Opcode::cmp_eq_f64 ||
                    instruction.opcode == machine::Opcode::cmp_ne_f32 || instruction.opcode == machine::Opcode::cmp_ne_f64 ||
                    instruction.opcode == machine::Opcode::cmp_lt_f32 || instruction.opcode == machine::Opcode::cmp_lt_f64 ||
                    instruction.opcode == machine::Opcode::cmp_le_f32 || instruction.opcode == machine::Opcode::cmp_le_f64;
                std::uint8_t cc = 0x94;
                if (instruction.opcode == machine::Opcode::cmp_ne_f32 || instruction.opcode == machine::Opcode::cmp_ne_f64) cc = 0x95;
                else if (instruction.opcode == machine::Opcode::cmp_lt_f32 || instruction.opcode == machine::Opcode::cmp_lt_f64) cc = 0x92;
                else if (instruction.opcode == machine::Opcode::cmp_le_f32 || instruction.opcode == machine::Opcode::cmp_le_f64) cc = 0x96;
                else if (instruction.opcode == machine::Opcode::cmp_gt_f32 || instruction.opcode == machine::Opcode::cmp_gt_f64) cc = 0x97;
                else if (instruction.opcode == machine::Opcode::cmp_ge_f32 || instruction.opcode == machine::Opcode::cmp_ge_f64) cc = 0x93;
                out.byte(0x0F); out.byte(cc); out.byte(0xC0); // setcc al
                if (needs_nan_guard) {
                    const bool unordered_is_true = instruction.opcode == machine::Opcode::cmp_ne_f32 ||
                                                   instruction.opcode == machine::Opcode::cmp_ne_f64;
                    out.byte(0x0F); out.byte(unordered_is_true ? 0x9A : 0x9B); out.byte(0xC1); // setp/setnp cl
                    out.byte(unordered_is_true ? 0x08 : 0x20); out.byte(0xC8); // or/and al, cl
                    ++encoded.floating_nan_guard_count;
                }
                out.byte(0x0F); out.byte(0xB6); out.byte(0xC0);
                write_integer_cached32(allocation.location(instruction.result), Register::eax);
                break;
            }
            case machine::Opcode::int_to_float_signed:
            case machine::Opcode::int_to_float_unsigned: {
                if (instruction.inputs.size() != 1) { add_error(diagnostics, "malformed integer-to-float cast in @" + function.name); return encoded; }
                const auto source_bits = static_cast<unsigned>((instruction.immediate >> 8U) & 0xffU);
                const auto result_bits = static_cast<unsigned>(instruction.immediate & 0xffU);
                if ((source_bits != 8U && source_bits != 16U && source_bits != 32U && source_bits != 64U) ||
                    (result_bits != 32U && result_bits != 64U) ||
                    (instruction.opcode == machine::Opcode::int_to_float_unsigned && source_bits == 64U)) {
                    add_error(diagnostics, "unsupported integer-to-float cast width in @" + function.name); return encoded;
                }
                if (source_bits == 64U) emit_read_location64(out, Register::eax, allocation.location(instruction.inputs[0]));
                else emit_read_location(out, Register::eax, allocation.location(instruction.inputs[0]));
                if (instruction.opcode == machine::Opcode::int_to_float_signed && source_bits < 32U) emit_sign_extend_eax(out, source_bits);
                const bool convert_wide_integer = source_bits == 64U || instruction.opcode == machine::Opcode::int_to_float_unsigned;
                emit_int_to_float(out, XmmRegister::xmm0, Register::eax, convert_wide_integer, result_bits == 64U);
                write_floating_cached(allocation.location(instruction.result), XmmRegister::xmm0, result_bits == 64U);
                break;
            }
            case machine::Opcode::float_to_int_signed:
            case machine::Opcode::float_to_int_unsigned: {
                if (instruction.inputs.size() != 1) { add_error(diagnostics, "malformed float-to-integer cast in @" + function.name); return encoded; }
                const auto source_bits = static_cast<unsigned>((instruction.immediate >> 8U) & 0xffU);
                const auto result_bits = static_cast<unsigned>(instruction.immediate & 0xffU);
                if ((source_bits != 32U && source_bits != 64U) ||
                    (result_bits != 8U && result_bits != 16U && result_bits != 32U && result_bits != 64U) ||
                    (instruction.opcode == machine::Opcode::float_to_int_unsigned && result_bits == 64U)) {
                    add_error(diagnostics, "unsupported float-to-integer cast width in @" + function.name); return encoded;
                }
                emit_read_float_location(out, XmmRegister::xmm0, allocation.location(instruction.inputs[0]), source_bits == 64U);
                const bool wide_result = result_bits == 64U || instruction.opcode == machine::Opcode::float_to_int_unsigned;
                emit_float_to_int(out, Register::eax, XmmRegister::xmm0, wide_result, source_bits == 64U);
                if (result_bits == 64U) write_integer_cached64(allocation.location(instruction.result), Register::eax);
                else write_integer_cached32(allocation.location(instruction.result), Register::eax);
                break;
            }
            case machine::Opcode::float_extend:
            case machine::Opcode::float_truncate: {
                if (instruction.inputs.size() != 1) { add_error(diagnostics, "malformed float-width cast in @" + function.name); return encoded; }
                const auto source_bits = static_cast<unsigned>((instruction.immediate >> 8U) & 0xffU);
                const auto result_bits = static_cast<unsigned>(instruction.immediate & 0xffU);
                if (!((source_bits == 32U && result_bits == 64U) || (source_bits == 64U && result_bits == 32U))) {
                    add_error(diagnostics, "invalid float-width cast in @" + function.name); return encoded;
                }
                emit_read_float_location(out, XmmRegister::xmm0, allocation.location(instruction.inputs[0]), source_bits == 64U);
                emit_float_convert(out, XmmRegister::xmm0, XmmRegister::xmm0, source_bits == 64U);
                write_floating_cached(allocation.location(instruction.result), XmmRegister::xmm0, result_bits == 64U);
                break;
            }
            case machine::Opcode::copy:
                if (instruction.inputs.size() != 1) {
                    add_error(diagnostics, "malformed copy in @" + function.name);
                    return encoded;
                }
                if (same_location(allocation.location(instruction.inputs[0]), allocation.location(instruction.result))) {
                    ++encoded.eliminated_copy_count;
                    break;
                }
                ++encoded.emitted_copy_count;
                if (function.register_widths[instruction.result] == 8) {
                    emit_read_location64(out, Register::eax, allocation.location(instruction.inputs[0]));
                    write_integer_cached64(allocation.location(instruction.result), Register::eax);
                } else {
                    emit_read_location(out, Register::eax, allocation.location(instruction.inputs[0]));
                    write_integer_cached32(allocation.location(instruction.result), Register::eax);
                }
                break;
            case machine::Opcode::zero_extend:
            case machine::Opcode::sign_extend:
            case machine::Opcode::truncate: {
                if (instruction.inputs.size() != 1) {
                    add_error(diagnostics, "malformed integer cast instruction in @" + function.name);
                    return encoded;
                }
                const auto source_bits = static_cast<unsigned>((instruction.immediate >> 8U) & 0xffU);
                const auto result_bits = static_cast<unsigned>(instruction.immediate & 0xffU);
                if (source_bits == 0 || result_bits == 0 || source_bits > 64 || result_bits > 64) {
                    add_error(diagnostics, "invalid integer cast widths in @" + function.name);
                    return encoded;
                }
                if (function.register_widths[instruction.inputs[0]] == 8)
                    emit_read_location64(out, Register::eax, allocation.location(instruction.inputs[0]));
                else
                    emit_read_location(out, Register::eax, allocation.location(instruction.inputs[0]));

                if (instruction.opcode == machine::Opcode::sign_extend) {
                    const auto lane_bits = result_bits > 32 ? 64U : 32U;
                    const auto shift = static_cast<std::uint8_t>(lane_bits - source_bits);
                    if (lane_bits == 64) out.byte(0x48);
                    out.byte(0xC1); out.byte(0xE0); out.byte(shift);
                    if (lane_bits == 64) out.byte(0x48);
                    out.byte(0xC1); out.byte(0xF8); out.byte(shift);
                } else {
                    const auto kept_bits = instruction.opcode == machine::Opcode::truncate ? result_bits : source_bits;
                    if (kept_bits < 32) {
                        out.byte(0x25);
                        out.i32(static_cast<std::int32_t>((std::uint32_t{1} << kept_bits) - 1U));
                    } else if (kept_bits == 32) {
                        out.byte(0x89); out.byte(0xC0);
                    }
                }
                if (function.register_widths[instruction.result] == 8)
                    write_integer_cached64(allocation.location(instruction.result), Register::eax);
                else
                    write_integer_cached32(allocation.location(instruction.result), Register::eax);
                break;
            }
            case machine::Opcode::load_stack_i8:
            case machine::Opcode::load_stack_i16:
            case machine::Opcode::load_stack_i32: {
                const auto size = instruction.opcode == machine::Opcode::load_stack_i8 ? 1 : instruction.opcode == machine::Opcode::load_stack_i16 ? 2 : 4;
                if (instruction.immediate > -size || -instruction.immediate > static_cast<std::int64_t>(function.local_stack_size)) { add_error(diagnostics, "invalid stack load offset in @" + function.name); return encoded; }
                if (size == 1) emit_load_stack_i8(out, Register::eax, static_cast<std::int32_t>(instruction.immediate));
                else if (size == 2) emit_load_stack_i16(out, Register::eax, static_cast<std::int32_t>(instruction.immediate));
                else emit_load_stack(out, Register::eax, static_cast<std::int32_t>(instruction.immediate));
                write_integer_cached32(allocation.location(instruction.result), Register::eax);
                break;
            }
            case machine::Opcode::load_stack_i64:
                if (instruction.immediate > -8 || -instruction.immediate > static_cast<std::int64_t>(function.local_stack_size)) { add_error(diagnostics, "invalid i64 stack load offset in @" + function.name); return encoded; }
                emit_load_stack64(out, Register::eax, static_cast<std::int32_t>(instruction.immediate));
                write_integer_cached64(allocation.location(instruction.result), Register::eax);
                break;
            case machine::Opcode::store_stack_i8:
            case machine::Opcode::store_stack_i16:
            case machine::Opcode::store_stack_i32: {
                const auto size = instruction.opcode == machine::Opcode::store_stack_i8 ? 1 : instruction.opcode == machine::Opcode::store_stack_i16 ? 2 : 4;
                const auto expected_inputs = instruction.symbol == "$storeimm" ? 0U : 1U;
                if (instruction.inputs.size() != expected_inputs || instruction.immediate > -size || -instruction.immediate > static_cast<std::int64_t>(function.local_stack_size)) { add_error(diagnostics, "invalid stack store in @" + function.name); return encoded; }
                if (instruction.symbol == "$storeimm") {
                    emit_store_stack_immediate(out, instruction.argument_index, static_cast<std::int32_t>(instruction.immediate), static_cast<unsigned>(size));
                    break;
                }
                emit_read_location(out, Register::eax, allocation.location(instruction.inputs[0]));
                if (size == 1) emit_store_stack_i8(out, Register::eax, static_cast<std::int32_t>(instruction.immediate));
                else if (size == 2) emit_store_stack_i16(out, Register::eax, static_cast<std::int32_t>(instruction.immediate));
                else emit_store_stack(out, Register::eax, static_cast<std::int32_t>(instruction.immediate));
                break;
            }
            case machine::Opcode::store_stack_i64:
                if (instruction.inputs.size() != (instruction.symbol == "$storeimm" ? 0U : 1U) || instruction.immediate > -8 || -instruction.immediate > static_cast<std::int64_t>(function.local_stack_size)) { add_error(diagnostics, "invalid i64 stack store in @" + function.name); return encoded; }
                if (instruction.symbol == "$storeimm") {
                    emit_store_stack_immediate(out, instruction.argument_index, static_cast<std::int32_t>(instruction.immediate), 8U);
                    break;
                }
                emit_read_location64(out, Register::eax, allocation.location(instruction.inputs[0]));
                emit_store_stack64(out, Register::eax, static_cast<std::int32_t>(instruction.immediate));
                break;
            case machine::Opcode::add_i64:
            case machine::Opcode::sub_i64:
            case machine::Opcode::mul_i64:
            case machine::Opcode::and_i64:
            case machine::Opcode::or_i64:
            case machine::Opcode::xor_i64: {
                if (instruction.symbol == "$memstack") {
                    if (instruction.inputs.size() != 1) { add_error(diagnostics, "malformed i64 memory arithmetic instruction in @" + function.name); return encoded; }
                    emit_read_location64(out, Register::eax, allocation.location(instruction.inputs[0]));
                    emit_integer_stack_binary(out, instruction.opcode, Register::eax, static_cast<std::int32_t>(instruction.immediate), true);
                    write_integer_cached64(allocation.location(instruction.result), Register::eax);
                    break;
                }
                if (instruction.symbol == "$imm") {
                    if (instruction.inputs.size() != 1 || instruction.immediate < std::numeric_limits<std::int32_t>::min() ||
                        instruction.immediate > std::numeric_limits<std::int32_t>::max()) {
                        add_error(diagnostics, "malformed i64 immediate arithmetic instruction in @" + function.name); return encoded;
                    }
                    const auto& source_location = allocation.location(instruction.inputs[0]);
                    const auto& result_location = allocation.location(instruction.result);
                    if (instruction.opcode == machine::Opcode::mul_i64 &&
                        source_location.kind == machine::LocationKind::physical_register &&
                        result_location.kind == machine::LocationKind::physical_register) {
                        const auto source = integer_register(source_location.physical);
                        const auto destination = integer_register(result_location.physical);
                        const auto multiplier = instruction.immediate;
                        if (multiplier == 1) {
                            if (!same_location(source_location, result_location))
                                emit_mov_register64(out, destination, source);
                            break;
                        }
                        if (multiplier > 0) {
                            unsigned shift = 0;
                            auto odd_factor = static_cast<unsigned long long>(multiplier);
                            while ((odd_factor & 1ULL) == 0ULL) { odd_factor >>= 1U; ++shift; }
                            if (odd_factor == 1ULL || odd_factor == 3ULL ||
                                odd_factor == 5ULL || odd_factor == 9ULL) {
                                if (odd_factor == 1ULL) {
                                    if (!same_location(source_location, result_location))
                                        emit_mov_register64(out, destination, source);
                                } else {
                                    const auto scale = odd_factor == 3ULL ? 1U : odd_factor == 5ULL ? 2U : 3U;
                                    emit_lea_scaled_self(out, destination, source, scale, true);
                                }
                                if (shift != 0U)
                                    emit_shift_immediate(out, machine::Opcode::shl_i64, destination,
                                                         static_cast<std::uint8_t>(shift), true);
                                break;
                            }
                        }
                    }
                    if (instruction.opcode == machine::Opcode::add_i64 &&
                        source_location.kind == machine::LocationKind::physical_register &&
                        result_location.kind == machine::LocationKind::physical_register &&
                        same_location(source_location, result_location)) {
                        emit_integer_immediate_binary(out, instruction.opcode,
                            integer_register(result_location.physical),
                            static_cast<std::int32_t>(instruction.immediate), true);
                        ++encoded.two_address_reuse_count;
                        break;
                    }
                    if (instruction.opcode == machine::Opcode::add_i64 &&
                        arithmetic_flag_results.find(instruction.result) == arithmetic_flag_results.end() &&
                        source_location.kind == machine::LocationKind::physical_register &&
                        result_location.kind == machine::LocationKind::physical_register &&
                        !same_location(source_location, result_location)) {
                        emit_lea_offset(out, integer_register(result_location.physical),
                                        integer_register(source_location.physical),
                                        static_cast<std::int32_t>(instruction.immediate), true);
                        break;
                    }
                    emit_read_location64(out, Register::eax, source_location);
                    emit_integer_immediate_binary(out, instruction.opcode, Register::eax,
                                                  static_cast<std::int32_t>(instruction.immediate), true);
                    write_integer_cached64(result_location, Register::eax);
                    break;
                }
                if (instruction.inputs.size() != 2) { add_error(diagnostics, "malformed i64 arithmetic instruction in @" + function.name); return encoded; }
                const auto& left = allocation.location(instruction.inputs[0]);
                const auto& destination = allocation.location(instruction.result);
                if (destination.kind == machine::LocationKind::physical_register && same_location(destination, left)) {
                    const auto& right = allocation.location(instruction.inputs[1]);
                    const auto source = right.kind == machine::LocationKind::physical_register
                        ? physical_register(right.physical) : Register::ecx;
                    if (right.kind != machine::LocationKind::physical_register)
                        read_integer_cached(Register::ecx, right, true);
                    emit_two_address_binary(out, instruction.opcode, integer_register(destination.physical), source, true);
                    ++encoded.two_address_reuse_count;
                    break;
                }
                const bool commutative = instruction.opcode != machine::Opcode::sub_i64;
                if (commutative && destination.kind == machine::LocationKind::physical_register &&
                    same_location(destination, allocation.location(instruction.inputs[1]))) {
                    const auto source = left.kind == machine::LocationKind::physical_register
                        ? physical_register(left.physical) : Register::ecx;
                    if (left.kind != machine::LocationKind::physical_register)
                        read_integer_cached(Register::ecx, left, true);
                    emit_two_address_binary(out, instruction.opcode, integer_register(destination.physical), source, true);
                    ++encoded.two_address_reuse_count;
                    break;
                }
                if (instruction.opcode == machine::Opcode::add_i64 &&
                    arithmetic_flag_results.find(instruction.result) == arithmetic_flag_results.end() &&
                    destination.kind == machine::LocationKind::physical_register &&
                    left.kind == machine::LocationKind::physical_register &&
                    allocation.location(instruction.inputs[1]).kind == machine::LocationKind::physical_register) {
                    emit_lea_sum(out, integer_register(destination.physical), integer_register(left.physical),
                                 integer_register(allocation.location(instruction.inputs[1]).physical), true);
                    break;
                }
                read_integer_cached(Register::eax, left, true);
                if (instruction.inputs[0] == instruction.inputs[1] && left.kind == machine::LocationKind::stack_slot) {
                    emit_mov_register64(out, Register::ecx, Register::eax);
                    ++encoded.redundant_spill_load_count;
                } else {
                    read_integer_cached(Register::ecx, allocation.location(instruction.inputs[1]), true);
                }
                out.byte(0x48);
                if (instruction.opcode == machine::Opcode::add_i64) { out.byte(0x01); out.byte(0xC8); }
                else if (instruction.opcode == machine::Opcode::sub_i64) { out.byte(0x29); out.byte(0xC8); }
                else if (instruction.opcode == machine::Opcode::mul_i64) { out.byte(0x0F); out.byte(0xAF); out.byte(0xC1); }
                else if (instruction.opcode == machine::Opcode::and_i64) { out.byte(0x21); out.byte(0xC8); }
                else if (instruction.opcode == machine::Opcode::or_i64) { out.byte(0x09); out.byte(0xC8); }
                else { out.byte(0x31); out.byte(0xC8); }
                write_integer_cached64(destination, Register::eax);
                break;
            }
            case machine::Opcode::add_i64_contiguous_inplace: {
                if (instruction.inputs.size() != 2U || instruction.immediate < 2 || instruction.immediate > 16 ||
                    (instruction.immediate & (instruction.immediate - 1)) != 0) {
                    add_error(diagnostics, "malformed contiguous i64 map-add in @" + function.name);
                    return encoded;
                }
                const auto& base_location = allocation.location(instruction.inputs[0]);
                const auto& delta_location = allocation.location(instruction.inputs[1]);
                Register base = Register::ecx;
                Register delta = Register::eax;
                if (base_location.kind == machine::LocationKind::physical_register) base = integer_register(base_location.physical);
                else emit_read_location64(out, base, base_location);
                if (delta_location.kind == machine::LocationKind::physical_register) delta = integer_register(delta_location.physical);
                else emit_read_location64(out, delta, delta_location);
                emit_movq_gpr_to_xmm(out, XmmRegister::xmm1, delta);
                emit_pshufd(out, XmmRegister::xmm1, XmmRegister::xmm1, 0x44);
                for (std::int32_t offset = 0; offset < instruction.immediate * 8; offset += 16) {
                    emit_xmm128_ptr_load(out, XmmRegister::xmm0, base, offset);
                    emit_paddq(out, XmmRegister::xmm0, XmmRegister::xmm1);
                    emit_xmm128_ptr_store(out, base, XmmRegister::xmm0, offset);
                }
                break;
            }
            case machine::Opcode::binary_i32_contiguous_inplace:
            case machine::Opcode::binary_i64_contiguous_inplace:
            case machine::Opcode::binary_i32_contiguous_map:
            case machine::Opcode::binary_i64_contiguous_map: {
                const bool wide = instruction.opcode == machine::Opcode::binary_i64_contiguous_inplace ||
                                  instruction.opcode == machine::Opcode::binary_i64_contiguous_map;
                const bool inplace = instruction.opcode == machine::Opcode::binary_i32_contiguous_inplace ||
                                     instruction.opcode == machine::Opcode::binary_i64_contiguous_inplace;
                const auto lane_bytes = wide ? 8 : 4;
                if (instruction.inputs.size() != (inplace ? 2U : 3U) || instruction.immediate < 2 || instruction.immediate > 16 ||
                    (instruction.immediate & (instruction.immediate - 1)) != 0) {
                    add_error(diagnostics, "malformed contiguous integer expression pack in @" + function.name);
                    return encoded;
                }
                const auto scalar_opcode = static_cast<machine::Opcode>(instruction.argument_index);
                const bool supported = wide
                    ? (scalar_opcode == machine::Opcode::add_i64 || scalar_opcode == machine::Opcode::sub_i64 ||
                       scalar_opcode == machine::Opcode::and_i64 || scalar_opcode == machine::Opcode::or_i64 ||
                       scalar_opcode == machine::Opcode::xor_i64)
                    : (scalar_opcode == machine::Opcode::add_i32 || scalar_opcode == machine::Opcode::sub_i32 ||
                       scalar_opcode == machine::Opcode::and_i32 || scalar_opcode == machine::Opcode::or_i32 ||
                       scalar_opcode == machine::Opcode::xor_i32);
                if (!supported) {
                    add_error(diagnostics, "unsupported packed integer operation in @" + function.name);
                    return encoded;
                }

                const auto source_reg = instruction.inputs[0];
                const auto destination_reg = inplace ? instruction.inputs[0] : instruction.inputs[1];
                const auto scalar_reg = inplace ? instruction.inputs[1] : instruction.inputs[2];
                const auto& source_location = allocation.location(source_reg);
                const auto& destination_location = allocation.location(destination_reg);
                const auto& scalar_location = allocation.location(scalar_reg);
                Register source = Register::ecx;
                Register destination = Register::edx;
                Register scalar = Register::eax;
                if (source_location.kind == machine::LocationKind::physical_register)
                    source = integer_register(source_location.physical);
                else
                    emit_read_location64(out, source, source_location);
                if (destination_location.kind == machine::LocationKind::physical_register)
                    destination = integer_register(destination_location.physical);
                else
                    emit_read_location64(out, destination, destination_location);
                if (scalar_location.kind == machine::LocationKind::physical_register)
                    scalar = integer_register(scalar_location.physical);
                else
                    emit_read_location64(out, scalar, scalar_location);

                const bool use_avx512 = instruction.vector_bits >= 512U && instruction.immediate * lane_bytes >= 64;
                const bool use_avx2 = !use_avx512 && instruction.vector_bits >= 256U && instruction.immediate * lane_bytes >= 32;
                if (use_avx512 || use_avx2) {
                    // Stay entirely in the VEX/EVEX domain.  Legacy SSE MOVQ /
                    // PSHUFD here creates an AVX<->SSE transition on every
                    // vector-loop iteration once vzeroupper is correctly moved
                    // to ABI boundaries.
                    emit_vex_mov_gpr_to_xmm(out, XmmRegister::xmm1, scalar, wide);
                } else if (wide) {
                    emit_movq_gpr_to_xmm(out, XmmRegister::xmm1, scalar);
                    emit_pshufd(out, XmmRegister::xmm1, XmmRegister::xmm1, 0x44);
                } else {
                    emit_mov_gpr_to_xmm(out, XmmRegister::xmm1, scalar, false);
                    emit_pshufd(out, XmmRegister::xmm1, XmmRegister::xmm1, 0x00);
                }
                MaskRegister zmm_mask = MaskRegister::k0;
                bool masked_zmm = false;
                if (instruction.vector_mask_lanes != 0U) {
                    const auto zmm_lanes = static_cast<std::uint8_t>(wide ? 8U : 16U);
                    if (!use_avx512 || instruction.vector_mask_lanes > zmm_lanes) {
                        add_error(diagnostics, "invalid AVX-512 packed lane mask in @" + function.name);
                        return encoded;
                    }
                    emit_mask_lane_count(out, MaskRegister::k1, instruction.vector_mask_lanes);
                    zmm_mask = MaskRegister::k1;
                    masked_zmm = true;
                }
                if (use_avx512) emit_avx512_broadcast(out, XmmRegister::xmm1, XmmRegister::xmm1, wide);
                else if (use_avx2) emit_avx2_broadcast(out, XmmRegister::xmm1, XmmRegister::xmm1, wide);

                std::uint8_t packed_opcode = 0;
                if (scalar_opcode == machine::Opcode::add_i32) packed_opcode = 0xFE;       // PADDD
                else if (scalar_opcode == machine::Opcode::add_i64) packed_opcode = 0xD4;  // PADDQ
                else if (scalar_opcode == machine::Opcode::sub_i32) packed_opcode = 0xFA;  // PSUBD
                else if (scalar_opcode == machine::Opcode::sub_i64) packed_opcode = 0xFB;  // PSUBQ
                else if (scalar_opcode == machine::Opcode::and_i32 || scalar_opcode == machine::Opcode::and_i64) packed_opcode = 0xDB; // PAND
                else if (scalar_opcode == machine::Opcode::or_i32 || scalar_opcode == machine::Opcode::or_i64) packed_opcode = 0xEB;   // POR
                else packed_opcode = 0xEF; // PXOR

                const auto total_bytes = static_cast<std::int32_t>(instruction.immediate * lane_bytes);
                std::int32_t offset = 0;
                // Schedule two independent vectors together when possible.
                // This exposes load/ALU overlap instead of creating one long
                // load -> op -> store chain, matching the shape modern x86
                // out-of-order cores (and LLVM) prefer for four i64/eight i32
                // lane packs.
                if (use_avx512) {
                    for (; offset + 64 <= total_bytes; offset += 64) {
                        emit_zmm512_ptr_load(out, XmmRegister::xmm0, source, offset, wide, zmm_mask, masked_zmm);
                        emit_avx512_integer_binary(out, XmmRegister::xmm0, XmmRegister::xmm0, XmmRegister::xmm1, packed_opcode, wide);
                        emit_zmm512_ptr_store(out, destination, XmmRegister::xmm0, offset, wide, zmm_mask);
                    }
                } else if (use_avx2) {
                    for (; offset + 32 <= total_bytes; offset += 32) {
                        emit_ymm256_ptr_load(out, XmmRegister::xmm0, source, offset);
                        emit_avx2_integer_binary(out, XmmRegister::xmm0, XmmRegister::xmm0, XmmRegister::xmm1, packed_opcode);
                        emit_ymm256_ptr_store(out, destination, XmmRegister::xmm0, offset);
                    }
                } else {
                    for (; offset + 32 <= total_bytes; offset += 32) {
                        emit_xmm128_ptr_load(out, XmmRegister::xmm0, source, offset);
                        emit_xmm128_ptr_load(out, XmmRegister::xmm2, source, offset + 16);
                        emit_sse2_integer_binary(out, XmmRegister::xmm0, XmmRegister::xmm1, packed_opcode);
                        emit_sse2_integer_binary(out, XmmRegister::xmm2, XmmRegister::xmm1, packed_opcode);
                        emit_xmm128_ptr_store(out, destination, XmmRegister::xmm0, offset);
                        emit_xmm128_ptr_store(out, destination, XmmRegister::xmm2, offset + 16);
                    }
                }
                if (offset < total_bytes) {
                    const auto chunk_bytes = std::min<std::int32_t>(16, total_bytes - offset);
                    emit_packed_ptr_load(out, XmmRegister::xmm0, source, offset, chunk_bytes);
                    emit_sse2_integer_binary(out, XmmRegister::xmm0, XmmRegister::xmm1, packed_opcode);
                    emit_packed_ptr_store(out, destination, XmmRegister::xmm0, offset, chunk_bytes);
                }
                break;
            }
            case machine::Opcode::binary_i32_contiguous_map2:
            case machine::Opcode::binary_i64_contiguous_map2: {
                const bool wide = instruction.opcode == machine::Opcode::binary_i64_contiguous_map2;
                const auto lane_bytes = wide ? 8 : 4;
                if (instruction.inputs.size() != 3U || instruction.immediate < 2 || instruction.immediate > 16 ||
                    (instruction.immediate & (instruction.immediate - 1)) != 0) {
                    add_error(diagnostics, "malformed vector-to-vector integer expression pack in @" + function.name);
                    return encoded;
                }
                const auto scalar_opcode = static_cast<machine::Opcode>(instruction.argument_index);
                const bool supported = wide
                    ? (scalar_opcode == machine::Opcode::add_i64 || scalar_opcode == machine::Opcode::sub_i64 ||
                       scalar_opcode == machine::Opcode::and_i64 || scalar_opcode == machine::Opcode::or_i64 ||
                       scalar_opcode == machine::Opcode::xor_i64)
                    : (scalar_opcode == machine::Opcode::add_i32 || scalar_opcode == machine::Opcode::sub_i32 ||
                       scalar_opcode == machine::Opcode::and_i32 || scalar_opcode == machine::Opcode::or_i32 ||
                       scalar_opcode == machine::Opcode::xor_i32);
                if (!supported) {
                    add_error(diagnostics, "unsupported vector-to-vector integer operation in @" + function.name);
                    return encoded;
                }
                Register source_a = Register::ecx, source_b = Register::edx, destination = Register::eax;
                const auto resolve_pointer = [&](machine::VirtualRegister reg, Register fallback) -> Register {
                    const auto& location = allocation.location(reg);
                    if (location.kind == machine::LocationKind::physical_register)
                        return integer_register(location.physical);
                    emit_read_location64(out, fallback, location);
                    return fallback;
                };
                source_a = resolve_pointer(instruction.inputs[0], Register::ecx);
                source_b = resolve_pointer(instruction.inputs[1], Register::edx);
                destination = resolve_pointer(instruction.inputs[2], Register::eax);
                std::uint8_t packed_opcode = 0;
                if (scalar_opcode == machine::Opcode::add_i32) packed_opcode = 0xFE;
                else if (scalar_opcode == machine::Opcode::add_i64) packed_opcode = 0xD4;
                else if (scalar_opcode == machine::Opcode::sub_i32) packed_opcode = 0xFA;
                else if (scalar_opcode == machine::Opcode::sub_i64) packed_opcode = 0xFB;
                else if (scalar_opcode == machine::Opcode::and_i32 || scalar_opcode == machine::Opcode::and_i64) packed_opcode = 0xDB;
                else if (scalar_opcode == machine::Opcode::or_i32 || scalar_opcode == machine::Opcode::or_i64) packed_opcode = 0xEB;
                else packed_opcode = 0xEF;
                const auto total_bytes = static_cast<std::int32_t>(instruction.immediate * lane_bytes);
                std::int32_t offset = 0;
                const auto vector_chunk_bytes = instruction.vector_bits >= 512U ? 64 :
                                                instruction.vector_bits >= 256U ? 32 : 16;
                for (; offset + vector_chunk_bytes <= total_bytes; offset += vector_chunk_bytes) {
                    emit_packed_ptr_load(out, XmmRegister::xmm0, source_a, offset, vector_chunk_bytes);
                    emit_packed_ptr_load(out, XmmRegister::xmm1, source_b, offset, vector_chunk_bytes);
                    emit_packed_integer_binary(out, XmmRegister::xmm0, XmmRegister::xmm0, XmmRegister::xmm1,
                                               packed_opcode, wide, vector_chunk_bytes);
                    emit_packed_ptr_store(out, destination, XmmRegister::xmm0, offset, vector_chunk_bytes);
                }
                if (offset < total_bytes) {
                    const auto chunk_bytes = std::min<std::int32_t>(16, total_bytes - offset);
                    emit_packed_ptr_load(out, XmmRegister::xmm0, source_a, offset, chunk_bytes);
                    emit_packed_ptr_load(out, XmmRegister::xmm1, source_b, offset, chunk_bytes);
                    emit_sse2_integer_binary(out, XmmRegister::xmm0, XmmRegister::xmm1, packed_opcode);
                    emit_packed_ptr_store(out, destination, XmmRegister::xmm0, offset, chunk_bytes);
                }
                break;
            }
            case machine::Opcode::binary_i32_contiguous_dag_reuse:
            case machine::Opcode::binary_i64_contiguous_dag_reuse: {
                const bool wide = instruction.opcode == machine::Opcode::binary_i64_contiguous_dag_reuse;
                const auto lane_bytes = wide ? 8 : 4;
                if (instruction.symbol.size() < 18U || (instruction.symbol.size() % 6U) != 0U ||
                    instruction.immediate < 2 || instruction.immediate > 16 ||
                    (instruction.immediate & (instruction.immediate - 1)) != 0) {
                    add_error(diagnostics, "malformed packed reusable integer DAG in @" + function.name);
                    return encoded;
                }
                struct DagNode { std::uint16_t tag{}, lhs{}, rhs{}; };
                const auto read16 = [&](std::size_t at) -> std::uint16_t {
                    return static_cast<std::uint16_t>(static_cast<unsigned char>(instruction.symbol[at])) |
                           (static_cast<std::uint16_t>(static_cast<unsigned char>(instruction.symbol[at + 1U])) << 8U);
                };
                std::vector<DagNode> nodes;
                nodes.reserve(instruction.symbol.size() / 6U);
                std::size_t max_source = 0U;
                bool saw_source = false;
                for (std::size_t at = 0; at < instruction.symbol.size(); at += 6U) {
                    DagNode node{read16(at), read16(at + 2U), read16(at + 4U)};
                    if ((node.tag & 0x8000U) != 0U) {
                        saw_source = true;
                        max_source = std::max(max_source, static_cast<std::size_t>(node.tag & 0x7fffU));
                    }
                    nodes.push_back(node);
                }
                const auto source_count = saw_source ? max_source + 1U : 0U;
                if (source_count == 0U || instruction.inputs.size() != source_count + 1U) {
                    add_error(diagnostics, "malformed packed reusable DAG source list in @" + function.name);
                    return encoded;
                }
                const auto packed_byte = [&](machine::Opcode op) -> std::uint8_t {
                    if (op == machine::Opcode::add_i32) return 0xFE;
                    if (op == machine::Opcode::add_i64) return 0xD4;
                    if (op == machine::Opcode::sub_i32) return 0xFA;
                    if (op == machine::Opcode::sub_i64) return 0xFB;
                    if (op == machine::Opcode::and_i32 || op == machine::Opcode::and_i64) return 0xDB;
                    if (op == machine::Opcode::or_i32 || op == machine::Opcode::or_i64) return 0xEB;
                    return 0xEF;
                };
                const auto resolve_pointer = [&](machine::VirtualRegister reg, Register fallback) -> Register {
                    const auto& location = allocation.location(reg);
                    if (location.kind == machine::LocationKind::physical_register) return integer_register(location.physical);
                    emit_read_location64(out, fallback, location);
                    return fallback;
                };
                std::vector<std::size_t> base_uses(nodes.size(), 0U);
                for (const auto& node : nodes) {
                    if ((node.tag & 0x8000U) == 0U) {
                        if (node.lhs >= nodes.size() || node.rhs >= nodes.size()) {
                            add_error(diagnostics, "packed reusable DAG node reference overflow in @" + function.name);
                            return encoded;
                        }
                        ++base_uses[node.lhs];
                        ++base_uses[node.rhs];
                    }
                }

                // Estimate the amount of packed work required to rematerialize
                // each node. Shared nodes with meaningful rematerialization cost
                // are cached; cheap nodes are allowed to recompute when keeping
                // all shared values live would create unnecessary XMM pressure.
                // Limit persistent cache entries so the recursive evaluator keeps
                // enough registers available for independent subtrees.
                std::vector<std::size_t> subtree_cost(nodes.size(), 1U);
                for (std::size_t id = 0; id < nodes.size(); ++id) {
                    const auto& node = nodes[id];
                    if ((node.tag & 0x8000U) == 0U) {
                        subtree_cost[id] = 1U + subtree_cost[node.lhs] + subtree_cost[node.rhs];
                    }
                }
                struct RetainCandidate { std::size_t id{}; std::size_t savings{}; std::size_t cost{}; };
                std::vector<RetainCandidate> retain_candidates;
                for (std::size_t id = 0; id < nodes.size(); ++id) {
                    if ((nodes[id].tag & 0x8000U) != 0U || base_uses[id] < 2U) continue;
                    const auto savings = (subtree_cost[id] > 1U ? subtree_cost[id] - 1U : 0U) * (base_uses[id] - 1U);
                    if (savings != 0U) retain_candidates.push_back({id, savings, subtree_cost[id]});
                }
                std::sort(retain_candidates.begin(), retain_candidates.end(), [](const auto& left, const auto& right) {
                    if (left.savings != right.savings) return left.savings > right.savings;
                    if (left.cost != right.cost) return left.cost > right.cost;
                    return left.id < right.id;
                });
                std::vector<bool> retain(nodes.size(), false);
                std::vector<std::size_t> retention_value(nodes.size(), 0U);
                for (const auto& candidate : retain_candidates) {
                    retain[candidate.id] = true;
                    retention_value[candidate.id] = candidate.savings;
                }

                const std::array<XmmRegister, 8> pool = {XmmRegister::xmm0, XmmRegister::xmm1, XmmRegister::xmm2, XmmRegister::xmm3,
                                                         XmmRegister::xmm4, XmmRegister::xmm5, XmmRegister::xmm6, XmmRegister::xmm7};
                const auto total_bytes = static_cast<std::int32_t>(instruction.immediate * lane_bytes);
                for (std::int32_t offset = 0; offset < total_bytes;) {
                    const auto remaining_bytes = total_bytes - offset;
                    const auto chunk_bytes = instruction.vector_bits >= 512U && remaining_bytes >= 64 ? 64 :
                                             instruction.vector_bits >= 256U && remaining_bytes >= 32 ? 32 :
                                             std::min<std::int32_t>(16, remaining_bytes);
                    std::vector<std::optional<XmmRegister>> cached(nodes.size());
                    auto remaining = base_uses;
                    std::array<bool, 8> busy{};
                    const auto is_protected = [](XmmRegister reg, const std::vector<XmmRegister>& protected_regs) {
                        return std::find(protected_regs.begin(), protected_regs.end(), reg) != protected_regs.end();
                    };
                    const auto allocate_xmm = [&](const std::vector<XmmRegister>& protected_regs) -> std::optional<XmmRegister> {
                        for (std::size_t i = 0; i < pool.size(); ++i) {
                            if (!busy[i]) {
                                busy[i] = true;
                                return pool[i];
                            }
                        }

                        std::optional<std::size_t> victim;
                        for (std::size_t id = 0; id < cached.size(); ++id) {
                            if (!cached[id] || is_protected(*cached[id], protected_regs)) continue;
                            if (!victim || retention_value[id] < retention_value[*victim] ||
                                (retention_value[id] == retention_value[*victim] && subtree_cost[id] < subtree_cost[*victim]))
                                victim = id;
                        }
                        if (!victim) return std::nullopt;
                        const auto reg = *cached[*victim];
                        cached[*victim].reset();
                        return reg;
                    };
                    const auto release_xmm = [&](XmmRegister reg) { busy[static_cast<std::size_t>(reg)] = false; };
                    struct Materialized { XmmRegister reg{}; bool disposable{}; };
                    std::function<std::optional<Materialized>(std::size_t, const std::vector<XmmRegister>&)> materialize;
                    materialize = [&](std::size_t id, const std::vector<XmmRegister>& protected_regs) -> std::optional<Materialized> {
                        if (id >= nodes.size()) return std::nullopt;
                        if (retain[id] && cached[id]) return Materialized{*cached[id], false};
                        const auto& node = nodes[id];
                        if ((node.tag & 0x8000U) != 0U) {
                            const auto reg = allocate_xmm(protected_regs);
                            if (!reg) return std::nullopt;
                            const auto source_index = static_cast<std::size_t>(node.tag & 0x7fffU);
                            const auto pointer = resolve_pointer(instruction.inputs[source_index], Register::ecx);
                            emit_packed_ptr_load(out, *reg, pointer, offset, chunk_bytes);
                            return Materialized{*reg, true};
                        }
                        auto lhs = materialize(node.lhs, protected_regs);
                        if (!lhs) return std::nullopt;
                        auto rhs_protected = protected_regs;
                        rhs_protected.push_back(lhs->reg);
                        auto rhs = materialize(node.rhs, rhs_protected);
                        if (!rhs) {
                            if (lhs->disposable) release_xmm(lhs->reg);
                            return std::nullopt;
                        }
                        XmmRegister destination_reg = lhs->reg;
                        if (!lhs->disposable) {
                            auto destination_protected = rhs_protected;
                            destination_protected.push_back(rhs->reg);
                            const auto fresh = allocate_xmm(destination_protected);
                            if (!fresh) {
                                if (rhs->disposable) release_xmm(rhs->reg);
                                return std::nullopt;
                            }
                            destination_reg = *fresh;
                            emit_packed_move(out, destination_reg, lhs->reg, chunk_bytes);
                        }
                        emit_packed_integer_binary(out, destination_reg, rhs->reg, packed_byte(static_cast<machine::Opcode>(node.tag)), chunk_bytes);
                        if (rhs->disposable && rhs->reg != destination_reg) release_xmm(rhs->reg);

                        if (remaining[node.lhs] != 0U) --remaining[node.lhs];
                        if (remaining[node.rhs] != 0U) --remaining[node.rhs];
                        const std::array<std::size_t, 2> consumed = {node.lhs, node.rhs};
                        for (std::size_t index = 0; index < consumed.size(); ++index) {
                            const auto child = consumed[index];
                            if (index != 0U && consumed[0] == child) continue;
                            if (retain[child] && remaining[child] == 0U && cached[child] && *cached[child] != destination_reg) {
                                release_xmm(*cached[child]);
                                cached[child].reset();
                            }
                        }

                        const bool keep = retain[id];
                        if (keep) cached[id] = destination_reg;
                        return Materialized{destination_reg, !keep};
                    };
                    const auto root = materialize(nodes.size() - 1U, {});
                    if (!root) {
                        add_error(diagnostics, "packed reusable DAG exceeded vector register budget in @" + function.name);
                        return encoded;
                    }
                    const auto destination = resolve_pointer(instruction.inputs[source_count], Register::ecx);
                    emit_packed_ptr_store(out, destination, root->reg, offset, chunk_bytes);
                    if (root->disposable) release_xmm(root->reg);
                    offset += chunk_bytes;
                }
                break;
            }
            case machine::Opcode::binary_i32_contiguous_dag:
            case machine::Opcode::binary_i64_contiguous_dag: {
                const bool wide = instruction.opcode == machine::Opcode::binary_i64_contiguous_dag;
                const auto lane_bytes = wide ? 8 : 4;
                if (instruction.symbol.size() < 10U || (instruction.symbol.size() & 1U) != 0U ||
                    instruction.immediate < 2 || instruction.immediate > 16 ||
                    (instruction.immediate & (instruction.immediate - 1)) != 0) {
                    add_error(diagnostics, "malformed packed integer DAG in @" + function.name);
                    return encoded;
                }
                const auto packed_byte = [&](machine::Opcode op) -> std::uint8_t {
                    if (op == machine::Opcode::add_i32) return 0xFE;
                    if (op == machine::Opcode::add_i64) return 0xD4;
                    if (op == machine::Opcode::sub_i32) return 0xFA;
                    if (op == machine::Opcode::sub_i64) return 0xFB;
                    if (op == machine::Opcode::and_i32 || op == machine::Opcode::and_i64) return 0xDB;
                    if (op == machine::Opcode::or_i32 || op == machine::Opcode::or_i64) return 0xEB;
                    return 0xEF;
                };
                const auto resolve_pointer = [&](machine::VirtualRegister reg, Register fallback) -> Register {
                    const auto& location = allocation.location(reg);
                    if (location.kind == machine::LocationKind::physical_register) return integer_register(location.physical);
                    emit_read_location64(out, fallback, location);
                    return fallback;
                };
                std::vector<std::uint16_t> program;
                program.reserve(instruction.symbol.size() / 2U);
                std::size_t max_source = 0U;
                bool saw_source = false;
                for (std::size_t metadata_offset = 0; metadata_offset + 1U < instruction.symbol.size(); metadata_offset += 2U) {
                    const auto token = static_cast<std::uint16_t>(
                        static_cast<std::uint16_t>(static_cast<unsigned char>(instruction.symbol[metadata_offset])) |
                        (static_cast<std::uint16_t>(static_cast<unsigned char>(instruction.symbol[metadata_offset + 1U])) << 8U));
                    program.push_back(token);
                    if ((token & 0x8000U) != 0U) {
                        saw_source = true;
                        max_source = std::max(max_source, static_cast<std::size_t>(token & 0x7fffU));
                    }
                }
                const auto source_count = saw_source ? max_source + 1U : 0U;
                if (source_count == 0U || instruction.inputs.size() != source_count + 1U) {
                    add_error(diagnostics, "malformed packed integer DAG source list in @" + function.name);
                    return encoded;
                }
                const std::array<XmmRegister, 8> xmm_stack_registers = {
                    XmmRegister::xmm0, XmmRegister::xmm1, XmmRegister::xmm2, XmmRegister::xmm3,
                    XmmRegister::xmm4, XmmRegister::xmm5, XmmRegister::xmm6, XmmRegister::xmm7};
                const auto total_bytes = static_cast<std::int32_t>(instruction.immediate * lane_bytes);
                for (std::int32_t offset = 0; offset < total_bytes;) {
                    const auto remaining_bytes = total_bytes - offset;
                    const auto chunk_bytes = instruction.vector_bits >= 512U && remaining_bytes >= 64 ? 64 :
                                             instruction.vector_bits >= 256U && remaining_bytes >= 32 ? 32 :
                                             std::min<std::int32_t>(16, remaining_bytes);
                    std::vector<XmmRegister> stack;
                    stack.reserve(8U);
                    for (const auto token : program) {
                        if ((token & 0x8000U) != 0U) {
                            const auto source_index = static_cast<std::size_t>(token & 0x7fffU);
                            if (source_index >= source_count || stack.size() >= xmm_stack_registers.size()) {
                                add_error(diagnostics, "packed integer DAG stack/source overflow in @" + function.name);
                                return encoded;
                            }
                            const auto target = xmm_stack_registers[stack.size()];
                            const auto pointer = resolve_pointer(instruction.inputs[source_index], Register::ecx);
                            emit_packed_ptr_load(out, target, pointer, offset, chunk_bytes);
                            stack.push_back(target);
                            continue;
                        }
                        if (stack.size() < 2U) {
                            add_error(diagnostics, "packed integer DAG stack underflow in @" + function.name);
                            return encoded;
                        }
                        const auto operation = static_cast<machine::Opcode>(token);
                        const auto right = stack.back();
                        stack.pop_back();
                        const auto left = stack.back();
                        emit_packed_integer_binary(out, left, right, packed_byte(operation), chunk_bytes);
                    }
                    if (stack.size() != 1U) {
                        add_error(diagnostics, "packed integer DAG stack imbalance in @" + function.name);
                        return encoded;
                    }
                    const auto destination = resolve_pointer(instruction.inputs[source_count], Register::ecx);
                    emit_packed_ptr_store(out, destination, stack.front(), offset, chunk_bytes);
                    offset += chunk_bytes;
                }
                break;
            }
            case machine::Opcode::binary_i32_contiguous_chain:
            case machine::Opcode::binary_i64_contiguous_chain: {
                const bool wide = instruction.opcode == machine::Opcode::binary_i64_contiguous_chain;
                const auto lane_bytes = wide ? 8 : 4;
                if (instruction.symbol.size() < 6U || (instruction.symbol.size() & 1U) != 0U ||
                    instruction.inputs.size() != instruction.symbol.size() / 2U + 2U ||
                    instruction.immediate < 2 || instruction.immediate > 16 ||
                    (instruction.immediate & (instruction.immediate - 1)) != 0) {
                    add_error(diagnostics, "malformed arbitrary-depth integer expression pack in @" + function.name);
                    return encoded;
                }
                const auto packed_byte = [&](machine::Opcode op) -> std::uint8_t {
                    if (op == machine::Opcode::add_i32) return 0xFE;
                    if (op == machine::Opcode::add_i64) return 0xD4;
                    if (op == machine::Opcode::sub_i32) return 0xFA;
                    if (op == machine::Opcode::sub_i64) return 0xFB;
                    if (op == machine::Opcode::and_i32 || op == machine::Opcode::and_i64) return 0xDB;
                    if (op == machine::Opcode::or_i32 || op == machine::Opcode::or_i64) return 0xEB;
                    return 0xEF;
                };
                const auto resolve_pointer = [&](machine::VirtualRegister reg, Register fallback) -> Register {
                    const auto& location = allocation.location(reg);
                    if (location.kind == machine::LocationKind::physical_register) return integer_register(location.physical);
                    emit_read_location64(out, fallback, location);
                    return fallback;
                };
                std::vector<machine::Opcode> chain_operations;
                chain_operations.reserve(instruction.symbol.size() / 2U);
                for (std::size_t metadata_offset = 0; metadata_offset + 1U < instruction.symbol.size(); metadata_offset += 2U) {
                    const auto encoded = static_cast<std::uint32_t>(static_cast<unsigned char>(instruction.symbol[metadata_offset])) |
                                         (static_cast<std::uint32_t>(static_cast<unsigned char>(instruction.symbol[metadata_offset + 1U])) << 8U);
                    chain_operations.push_back(static_cast<machine::Opcode>(encoded));
                }
                const auto source_count = chain_operations.size() + 1U;
                const auto total_bytes = static_cast<std::int32_t>(instruction.immediate * lane_bytes);
                std::int32_t offset = 0;
                const auto vector_chunk_bytes = instruction.vector_bits >= 512U ? 64 :
                                                instruction.vector_bits >= 256U ? 32 : 16;
                for (; offset + vector_chunk_bytes <= total_bytes; offset += vector_chunk_bytes) {
                    auto source = resolve_pointer(instruction.inputs[0], Register::ecx);
                    emit_packed_ptr_load(out, XmmRegister::xmm0, source, offset, vector_chunk_bytes);
                    for (std::size_t operation_index = 0; operation_index < chain_operations.size(); ++operation_index) {
                        source = resolve_pointer(instruction.inputs[operation_index + 1U], Register::ecx);
                        emit_packed_ptr_load(out, XmmRegister::xmm1, source, offset, vector_chunk_bytes);
                        emit_packed_integer_binary(out, XmmRegister::xmm0, XmmRegister::xmm1,
                                                   packed_byte(chain_operations[operation_index]), vector_chunk_bytes);
                    }
                    const auto destination = resolve_pointer(instruction.inputs[source_count], Register::ecx);
                    emit_packed_ptr_store(out, destination, XmmRegister::xmm0, offset, vector_chunk_bytes);
                }
                if (offset < total_bytes) {
                    const auto chunk_bytes = std::min<std::int32_t>(16, total_bytes - offset);
                    auto source = resolve_pointer(instruction.inputs[0], Register::ecx);
                    emit_packed_ptr_load(out, XmmRegister::xmm0, source, offset, chunk_bytes);
                    for (std::size_t operation_index = 0; operation_index < chain_operations.size(); ++operation_index) {
                        source = resolve_pointer(instruction.inputs[operation_index + 1U], Register::ecx);
                        emit_packed_ptr_load(out, XmmRegister::xmm1, source, offset, chunk_bytes);
                        emit_sse2_integer_binary(out, XmmRegister::xmm0, XmmRegister::xmm1,
                                                 packed_byte(chain_operations[operation_index]));
                    }
                    const auto destination = resolve_pointer(instruction.inputs[source_count], Register::ecx);
                    emit_packed_ptr_store(out, destination, XmmRegister::xmm0, offset, chunk_bytes);
                }
                break;
            }
            case machine::Opcode::binary_i32_contiguous_map3:
            case machine::Opcode::binary_i64_contiguous_map3: {
                const bool wide = instruction.opcode == machine::Opcode::binary_i64_contiguous_map3;
                const auto lane_bytes = wide ? 8 : 4;
                if (instruction.inputs.size() != 4U || instruction.immediate < 2 || instruction.immediate > 16 ||
                    (instruction.immediate & (instruction.immediate - 1)) != 0) {
                    add_error(diagnostics, "malformed chained integer expression pack in @" + function.name);
                    return encoded;
                }
                const auto op1 = static_cast<machine::Opcode>(instruction.argument_index & 0xffffU);
                const auto op2 = static_cast<machine::Opcode>((instruction.argument_index >> 16U) & 0xffffU);
                const auto packed_byte = [&](machine::Opcode op) -> std::uint8_t {
                    if (op == machine::Opcode::add_i32) return 0xFE;
                    if (op == machine::Opcode::add_i64) return 0xD4;
                    if (op == machine::Opcode::sub_i32) return 0xFA;
                    if (op == machine::Opcode::sub_i64) return 0xFB;
                    if (op == machine::Opcode::and_i32 || op == machine::Opcode::and_i64) return 0xDB;
                    if (op == machine::Opcode::or_i32 || op == machine::Opcode::or_i64) return 0xEB;
                    return 0xEF;
                };
                Register source_a = Register::ecx, source_b = Register::edx, source_c = Register::r8d, destination = Register::eax;
                const auto resolve_pointer = [&](machine::VirtualRegister reg, Register fallback) -> Register {
                    const auto& location = allocation.location(reg);
                    if (location.kind == machine::LocationKind::physical_register) return integer_register(location.physical);
                    emit_read_location64(out, fallback, location); return fallback;
                };
                source_a = resolve_pointer(instruction.inputs[0], Register::ecx);
                source_b = resolve_pointer(instruction.inputs[1], Register::edx);
                source_c = resolve_pointer(instruction.inputs[2], Register::r8d);
                destination = resolve_pointer(instruction.inputs[3], Register::eax);
                const auto total_bytes = static_cast<std::int32_t>(instruction.immediate * lane_bytes);
                std::int32_t offset = 0;
                const auto vector_chunk_bytes = instruction.vector_bits >= 512U ? 64 :
                                                instruction.vector_bits >= 256U ? 32 : 16;
                for (; offset + vector_chunk_bytes <= total_bytes; offset += vector_chunk_bytes) {
                    emit_packed_ptr_load(out, XmmRegister::xmm0, source_a, offset, vector_chunk_bytes);
                    emit_packed_ptr_load(out, XmmRegister::xmm1, source_b, offset, vector_chunk_bytes);
                    emit_packed_integer_binary(out, XmmRegister::xmm0, XmmRegister::xmm1, packed_byte(op1), vector_chunk_bytes);
                    emit_packed_ptr_load(out, XmmRegister::xmm1, source_c, offset, vector_chunk_bytes);
                    emit_packed_integer_binary(out, XmmRegister::xmm0, XmmRegister::xmm1, packed_byte(op2), vector_chunk_bytes);
                    emit_packed_ptr_store(out, destination, XmmRegister::xmm0, offset, vector_chunk_bytes);
                }
                if (offset < total_bytes) {
                    const auto chunk_bytes = std::min<std::int32_t>(16, total_bytes - offset);
                    emit_packed_ptr_load(out, XmmRegister::xmm0, source_a, offset, chunk_bytes);
                    emit_packed_ptr_load(out, XmmRegister::xmm1, source_b, offset, chunk_bytes);
                    emit_sse2_integer_binary(out, XmmRegister::xmm0, XmmRegister::xmm1, packed_byte(op1));
                    emit_packed_ptr_load(out, XmmRegister::xmm1, source_c, offset, chunk_bytes);
                    emit_sse2_integer_binary(out, XmmRegister::xmm0, XmmRegister::xmm1, packed_byte(op2));
                    emit_packed_ptr_store(out, destination, XmmRegister::xmm0, offset, chunk_bytes);
                }
                break;
            }
            case machine::Opcode::reduce_add_i32_contiguous: {
                if (instruction.inputs.size() != 1U || instruction.immediate < 4 || instruction.immediate > 32 ||
                    (instruction.immediate & (instruction.immediate - 1)) != 0) {
                    add_error(diagnostics, "malformed contiguous i32 reduction in @" + function.name);
                    return encoded;
                }
                const auto& pointer_location = allocation.location(instruction.inputs[0]);
                Register pointer = Register::ecx;
                if (pointer_location.kind == machine::LocationKind::physical_register)
                    pointer = integer_register(pointer_location.physical);
                else
                    emit_read_location64(out, pointer, pointer_location);
                emit_xmm128_ptr_load(out, XmmRegister::xmm0, pointer, 0);
                if (instruction.immediate == 4) {
                    // One 128-bit vector already contains all four lanes.
                } else {
                    const auto half_bytes = static_cast<std::int32_t>(instruction.immediate * 2);
                    emit_xmm128_ptr_load(out, XmmRegister::xmm1, pointer, half_bytes);
                    for (std::int32_t offset = 16; offset < half_bytes; offset += 16) {
                        emit_paddd_ptr(out, XmmRegister::xmm0, pointer, offset);
                        emit_paddd_ptr(out, XmmRegister::xmm1, pointer, half_bytes + offset);
                    }
                    emit_paddd(out, XmmRegister::xmm0, XmmRegister::xmm1);
                }
                // Horizontally reduce four packed dwords without requiring SSE3.
                emit_pshufd(out, XmmRegister::xmm1, XmmRegister::xmm0, 0x4E);
                emit_paddd(out, XmmRegister::xmm0, XmmRegister::xmm1);
                emit_pshufd(out, XmmRegister::xmm1, XmmRegister::xmm0, 0xB1);
                emit_paddd(out, XmmRegister::xmm0, XmmRegister::xmm1);
                const auto& destination = allocation.location(instruction.result);
                if (destination.kind == machine::LocationKind::physical_register) {
                    emit_movd_xmm_to_gpr(out, integer_register(destination.physical), XmmRegister::xmm0);
                } else {
                    emit_movd_xmm_to_gpr(out, Register::eax, XmmRegister::xmm0);
                    write_integer_cached32(destination, Register::eax);
                }
                break;
            }
            case machine::Opcode::reduce_add_i64_contiguous: {
                if (instruction.inputs.size() != 1U || instruction.immediate < 4 || instruction.immediate > 16 ||
                    (instruction.immediate & (instruction.immediate - 1)) != 0) {
                    add_error(diagnostics, "malformed contiguous i64 reduction in @" + function.name);
                    return encoded;
                }
                const auto& pointer_location = allocation.location(instruction.inputs[0]);
                Register pointer = Register::ecx;
                if (pointer_location.kind == machine::LocationKind::physical_register)
                    pointer = integer_register(pointer_location.physical);
                else
                    emit_read_location64(out, pointer, pointer_location);
                emit_xmm128_ptr_load(out, XmmRegister::xmm0, pointer, 0);
                if (instruction.immediate == 4) {
                    emit_paddq_ptr(out, XmmRegister::xmm0, pointer, 16);
                } else if (instruction.immediate == 8) {
                    // Four independent 128-bit loads expose memory-level
                    // parallelism before joining two add chains.
                    emit_xmm128_ptr_load(out, XmmRegister::xmm1, pointer, 16);
                    emit_xmm128_ptr_load(out, XmmRegister::xmm2, pointer, 32);
                    emit_paddq(out, XmmRegister::xmm2, XmmRegister::xmm0);
                    emit_xmm128_ptr_load(out, XmmRegister::xmm0, pointer, 48);
                    emit_paddq(out, XmmRegister::xmm0, XmmRegister::xmm1);
                    emit_paddq(out, XmmRegister::xmm0, XmmRegister::xmm2);
                } else {
                    const auto half_bytes = static_cast<std::int32_t>(instruction.immediate * 4);
                    emit_xmm128_ptr_load(out, XmmRegister::xmm1, pointer, half_bytes);
                    for (std::int32_t offset = 16; offset < half_bytes; offset += 16) {
                        emit_paddq_ptr(out, XmmRegister::xmm0, pointer, offset);
                        emit_paddq_ptr(out, XmmRegister::xmm1, pointer, half_bytes + offset);
                    }
                    emit_paddq(out, XmmRegister::xmm0, XmmRegister::xmm1);
                }
                emit_pshufd(out, XmmRegister::xmm1, XmmRegister::xmm0, 0xEE);
                emit_paddq(out, XmmRegister::xmm0, XmmRegister::xmm1);
                const auto& destination = allocation.location(instruction.result);
                if (destination.kind == machine::LocationKind::physical_register) {
                    emit_movq_xmm_to_gpr(out, integer_register(destination.physical), XmmRegister::xmm0);
                } else {
                    emit_movq_xmm_to_gpr(out, Register::eax, XmmRegister::xmm0);
                    write_integer_cached64(destination, Register::eax);
                }
                break;
            }
            case machine::Opcode::add_i32:
            case machine::Opcode::sub_i32:
            case machine::Opcode::mul_i32:
            case machine::Opcode::and_i32:
            case machine::Opcode::or_i32:
            case machine::Opcode::xor_i32: {
                if (instruction.symbol == "$memstack") {
                    if (instruction.inputs.size() != 1) { add_error(diagnostics, "malformed memory arithmetic instruction in @" + function.name); return encoded; }
                    emit_read_location(out, Register::eax, allocation.location(instruction.inputs[0]));
                    emit_integer_stack_binary(out, instruction.opcode, Register::eax, static_cast<std::int32_t>(instruction.immediate), false);
                    write_integer_cached32(allocation.location(instruction.result), Register::eax);
                    break;
                }
                if (instruction.symbol == "$imm") {
                    if (instruction.inputs.size() != 1 || instruction.immediate < std::numeric_limits<std::int32_t>::min() ||
                        instruction.immediate > std::numeric_limits<std::int32_t>::max()) {
                        add_error(diagnostics, "malformed immediate arithmetic instruction in @" + function.name); return encoded;
                    }
                    const auto& source_location = allocation.location(instruction.inputs[0]);
                    const auto& result_location = allocation.location(instruction.result);
                    if (instruction.opcode == machine::Opcode::mul_i32 &&
                        source_location.kind == machine::LocationKind::physical_register &&
                        result_location.kind == machine::LocationKind::physical_register) {
                        const auto source = integer_register(source_location.physical);
                        const auto destination = integer_register(result_location.physical);
                        const auto multiplier = instruction.immediate;
                        if (multiplier == 1) {
                            if (!same_location(source_location, result_location))
                                emit_mov_register(out, destination, source);
                            break;
                        }
                        if (multiplier > 0) {
                            unsigned shift = 0;
                            auto odd_factor = static_cast<unsigned long long>(multiplier);
                            while ((odd_factor & 1ULL) == 0ULL) { odd_factor >>= 1U; ++shift; }
                            if (odd_factor == 1ULL || odd_factor == 3ULL ||
                                odd_factor == 5ULL || odd_factor == 9ULL) {
                                if (odd_factor == 1ULL) {
                                    if (!same_location(source_location, result_location))
                                        emit_mov_register(out, destination, source);
                                } else {
                                    const auto scale = odd_factor == 3ULL ? 1U : odd_factor == 5ULL ? 2U : 3U;
                                    emit_lea_scaled_self(out, destination, source, scale, false);
                                }
                                if (shift != 0U)
                                    emit_shift_immediate(out, machine::Opcode::shl_i32, destination,
                                                         static_cast<std::uint8_t>(shift), false);
                                break;
                            }
                        }
                    }
                    if (instruction.opcode == machine::Opcode::add_i32 &&
                        source_location.kind == machine::LocationKind::physical_register &&
                        result_location.kind == machine::LocationKind::physical_register &&
                        same_location(source_location, result_location)) {
                        emit_integer_immediate_binary(out, instruction.opcode,
                            integer_register(result_location.physical),
                            static_cast<std::int32_t>(instruction.immediate), false);
                        ++encoded.two_address_reuse_count;
                        break;
                    }
                    if (instruction.opcode == machine::Opcode::add_i32 &&
                        arithmetic_flag_results.find(instruction.result) == arithmetic_flag_results.end() &&
                        source_location.kind == machine::LocationKind::physical_register &&
                        result_location.kind == machine::LocationKind::physical_register &&
                        !same_location(source_location, result_location)) {
                        emit_lea_offset(out, integer_register(result_location.physical),
                                        integer_register(source_location.physical),
                                        static_cast<std::int32_t>(instruction.immediate), false);
                        break;
                    }
                    emit_read_location(out, Register::eax, source_location);
                    emit_integer_immediate_binary(out, instruction.opcode, Register::eax,
                                                  static_cast<std::int32_t>(instruction.immediate), false);
                    write_integer_cached32(result_location, Register::eax);
                    break;
                }
                if (instruction.inputs.size() != 2) {
                    add_error(diagnostics, "malformed arithmetic instruction in @" + function.name);
                    return encoded;
                }
                const auto& left = allocation.location(instruction.inputs[0]);
                const auto& destination = allocation.location(instruction.result);
                if (destination.kind == machine::LocationKind::physical_register && same_location(destination, left)) {
                    const auto& right = allocation.location(instruction.inputs[1]);
                    const auto source = right.kind == machine::LocationKind::physical_register
                        ? physical_register(right.physical) : Register::ecx;
                    if (right.kind != machine::LocationKind::physical_register)
                        read_integer_cached(Register::ecx, right, false);
                    emit_two_address_binary(out, instruction.opcode, integer_register(destination.physical), source, false);
                    ++encoded.two_address_reuse_count;
                    break;
                }
                const bool commutative = instruction.opcode != machine::Opcode::sub_i32;
                if (commutative && destination.kind == machine::LocationKind::physical_register &&
                    same_location(destination, allocation.location(instruction.inputs[1]))) {
                    const auto source = left.kind == machine::LocationKind::physical_register
                        ? physical_register(left.physical) : Register::ecx;
                    if (left.kind != machine::LocationKind::physical_register)
                        read_integer_cached(Register::ecx, left, false);
                    emit_two_address_binary(out, instruction.opcode, integer_register(destination.physical), source, false);
                    ++encoded.two_address_reuse_count;
                    break;
                }
                if (instruction.opcode == machine::Opcode::add_i32 &&
                    arithmetic_flag_results.find(instruction.result) == arithmetic_flag_results.end() &&
                    destination.kind == machine::LocationKind::physical_register &&
                    left.kind == machine::LocationKind::physical_register &&
                    allocation.location(instruction.inputs[1]).kind == machine::LocationKind::physical_register) {
                    emit_lea_sum(out, integer_register(destination.physical), integer_register(left.physical),
                                 integer_register(allocation.location(instruction.inputs[1]).physical), false);
                    break;
                }
                read_integer_cached(Register::eax, left, false);
                if (instruction.inputs[0] == instruction.inputs[1] && left.kind == machine::LocationKind::stack_slot) {
                    emit_mov_register(out, Register::ecx, Register::eax);
                    ++encoded.redundant_spill_load_count;
                } else {
                    read_integer_cached(Register::ecx, allocation.location(instruction.inputs[1]), false);
                }
                emit_binary(out, instruction.opcode);
                write_integer_cached32(destination, Register::eax);
                break;
            }
            case machine::Opcode::neg_i32:
            case machine::Opcode::not_i32:
            case machine::Opcode::neg_i64:
            case machine::Opcode::not_i64: {
                if (instruction.inputs.size() != 1) {
                    add_error(diagnostics, "malformed unary instruction in @" + function.name);
                    return encoded;
                }
                const bool wide = instruction.opcode == machine::Opcode::neg_i64 || instruction.opcode == machine::Opcode::not_i64;
                const auto& source = allocation.location(instruction.inputs[0]);
                const auto& destination = allocation.location(instruction.result);
                if (destination.kind == machine::LocationKind::physical_register && same_location(destination, source)) {
                    const auto target = integer_register(destination.physical);
                    const auto raw = static_cast<std::uint8_t>(target);
                    if (wide) out.byte(static_cast<std::uint8_t>(0x48U | (raw >= 8 ? 0x01U : 0U)));
                    else if (raw >= 8) out.byte(0x41);
                    out.byte(0xF7);
                    emit_modrm(out, 3,
                        (instruction.opcode == machine::Opcode::neg_i32 || instruction.opcode == machine::Opcode::neg_i64) ? 3U : 2U, raw);
                    ++encoded.unary_reuse_count;
                    break;
                }
                if (wide) emit_read_location64(out, Register::eax, source);
                else emit_read_location(out, Register::eax, source);
                if (wide) out.byte(0x48);
                out.byte(0xF7);
                out.byte((instruction.opcode == machine::Opcode::neg_i32 || instruction.opcode == machine::Opcode::neg_i64) ? 0xD8 : 0xD0);
                if (wide) write_integer_cached64(destination, Register::eax);
                else write_integer_cached32(destination, Register::eax);
                break;
            }
            case machine::Opcode::shl_i32:
            case machine::Opcode::shr_s_i32:
            case machine::Opcode::shr_u_i32:
            case machine::Opcode::shl_i64:
            case machine::Opcode::shr_s_i64:
            case machine::Opcode::shr_u_i64: {
                const bool wide = instruction.opcode == machine::Opcode::shl_i64 ||
                                  instruction.opcode == machine::Opcode::shr_s_i64 ||
                                  instruction.opcode == machine::Opcode::shr_u_i64;
                if (instruction.symbol == "$imm") {
                    if (instruction.inputs.size() != 1 || instruction.immediate < 0 || instruction.immediate > 255) {
                        add_error(diagnostics, "malformed immediate shift instruction in @" + function.name); return encoded;
                    }
                    if (wide) emit_read_location64(out, Register::eax, allocation.location(instruction.inputs[0]));
                    else emit_read_location(out, Register::eax, allocation.location(instruction.inputs[0]));
                    emit_shift_immediate(out, instruction.opcode, Register::eax,
                                         static_cast<std::uint8_t>(instruction.immediate), wide);
                    if (wide) write_integer_cached64(allocation.location(instruction.result), Register::eax);
                    else write_integer_cached32(allocation.location(instruction.result), Register::eax);
                    break;
                }
                if (instruction.inputs.size() != 2) {
                    add_error(diagnostics, "malformed shift instruction in @" + function.name);
                    return encoded;
                }
                if (wide) emit_read_location64(out, Register::eax, allocation.location(instruction.inputs[0]));
                else emit_read_location(out, Register::eax, allocation.location(instruction.inputs[0]));
                read_integer_cached(Register::ecx, allocation.location(instruction.inputs[1]), false);
                if (wide) out.byte(0x48);
                out.byte(0xD3);
                out.byte((instruction.opcode == machine::Opcode::shl_i32 || instruction.opcode == machine::Opcode::shl_i64) ? 0xE0 :
                         (instruction.opcode == machine::Opcode::shr_u_i32 || instruction.opcode == machine::Opcode::shr_u_i64) ? 0xE8 : 0xF8);
                if (wide) write_integer_cached64(allocation.location(instruction.result), Register::eax);
                else write_integer_cached32(allocation.location(instruction.result), Register::eax);
                break;
            }
            case machine::Opcode::select_i32:
            case machine::Opcode::select_i64: {
                if (instruction.inputs.size() != 3U) {
                    add_error(diagnostics, "malformed select instruction in @" + function.name);
                    return encoded;
                }
                const bool wide = instruction.opcode == machine::Opcode::select_i64;
                const auto& destination_location = allocation.location(instruction.result);
                const bool direct_destination =
                    destination_location.kind == machine::LocationKind::physical_register;
                const auto destination_register = direct_destination
                    ? physical_register(destination_location.physical) : Register::eax;

                // Initialize the false-value destination first, then conditionally replace it
                // with the true value.  When the true value already occupies a different physical
                // register, CMOV can consume it directly and avoid the former copy through rcx.
                // If the destination aliases the true source, preserve it in rcx before writing
                // the false value.  Stack and rematerialized sources retain the proven scratch path.
                const auto& true_location = allocation.location(instruction.inputs[1]);
                const auto& false_location = allocation.location(instruction.inputs[2]);
                Register true_register = Register::ecx;
                const bool direct_true = true_location.kind == machine::LocationKind::physical_register &&
                    (!direct_destination || true_location.physical != destination_location.physical);
                if (!direct_true) {
                    if (wide) emit_read_location64(out, Register::ecx, true_location);
                    else emit_read_location(out, Register::ecx, true_location);
                } else {
                    true_register = physical_register(true_location.physical);
                }
                if (wide) emit_read_location64(out, destination_register, false_location);
                else emit_read_location(out, destination_register, false_location);
                if (instruction.symbol == "$testimm") {
                    const auto& source = allocation.location(instruction.inputs[0]);
                    const auto value = static_cast<std::int32_t>(instruction.immediate);
                    const bool source_wide = function.register_widths[instruction.inputs[0]] == 8U;
                    if (source.kind == machine::LocationKind::physical_register)
                        emit_test_register_immediate(out, physical_register(source.physical), value, source_wide);
                    else {
                        if (source_wide) emit_read_location64(out, Register::edx, source);
                        else emit_read_location(out, Register::edx, source);
                        emit_test_register_immediate(out, Register::edx, value, source_wide);
                    }
                } else {
                    emit_read_location(out, Register::edx, allocation.location(instruction.inputs[0]));
                    out.byte(0x85); out.byte(0xD2); // test edx, edx
                }
                const auto compare_opcode = instruction.symbol == "$testimm"
                    ? static_cast<machine::Opcode>(instruction.argument_index - 1U)
                    : machine::Opcode::cmp_ne_i32;
                const bool select_true_when_nonzero = compare_opcode == machine::Opcode::cmp_ne_i32 ||
                                                      compare_opcode == machine::Opcode::cmp_ne_i64;
                emit_cmov_register(out, destination_register, true_register, wide,
                                   select_true_when_nonzero);
                if (!direct_destination) {
                    if (wide) write_integer_cached64(destination_location, Register::eax);
                    else write_integer_cached32(destination_location, Register::eax);
                }
                break;
            }
            case machine::Opcode::div_s_i32:
            case machine::Opcode::div_u_i32:
            case machine::Opcode::rem_s_i32:
            case machine::Opcode::rem_u_i32:
            case machine::Opcode::div_s_i64:
            case machine::Opcode::div_u_i64:
            case machine::Opcode::rem_s_i64:
            case machine::Opcode::rem_u_i64: {
                if (instruction.inputs.size() != 2) {
                    add_error(diagnostics, "malformed division instruction in @" + function.name);
                    return encoded;
                }
                const bool wide = instruction.opcode == machine::Opcode::div_s_i64 ||
                                  instruction.opcode == machine::Opcode::div_u_i64 ||
                                  instruction.opcode == machine::Opcode::rem_s_i64 ||
                                  instruction.opcode == machine::Opcode::rem_u_i64;
                if (wide) {
                    emit_read_location64(out, Register::eax, allocation.location(instruction.inputs[0]));
                    read_integer_cached(Register::ecx, allocation.location(instruction.inputs[1]), true);
                } else {
                    emit_read_location(out, Register::eax, allocation.location(instruction.inputs[0]));
                    read_integer_cached(Register::ecx, allocation.location(instruction.inputs[1]), false);
                }
                const bool is_signed = instruction.opcode == machine::Opcode::div_s_i32 ||
                                       instruction.opcode == machine::Opcode::rem_s_i32 ||
                                       instruction.opcode == machine::Opcode::div_s_i64 ||
                                       instruction.opcode == machine::Opcode::rem_s_i64;
                if (wide) {
                    if (is_signed) { out.byte(0x48); out.byte(0x99); } // cqo
                    else { out.byte(0x31); out.byte(0xD2); } // xor edx, edx
                    out.byte(0x48); out.byte(0xF7); out.byte(is_signed ? 0xF9 : 0xF1); // idiv/div rcx
                } else {
                    if (is_signed) { out.byte(0x99); } // cdq
                    else { out.byte(0x31); out.byte(0xD2); } // xor edx, edx
                    out.byte(0xF7); out.byte(is_signed ? 0xF9 : 0xF1); // idiv/div ecx
                }
                const bool remainder = instruction.opcode == machine::Opcode::rem_s_i32 ||
                                       instruction.opcode == machine::Opcode::rem_u_i32 ||
                                       instruction.opcode == machine::Opcode::rem_s_i64 ||
                                       instruction.opcode == machine::Opcode::rem_u_i64;
                if (wide) write_integer_cached64(allocation.location(instruction.result), remainder ? Register::edx : Register::eax);
                else write_integer_cached32(allocation.location(instruction.result), remainder ? Register::edx : Register::eax);
                break;
            }
            case machine::Opcode::cmp_eq_i32:
            case machine::Opcode::cmp_ne_i32:
            case machine::Opcode::cmp_lt_i32:
            case machine::Opcode::cmp_le_i32:
            case machine::Opcode::cmp_gt_i32:
            case machine::Opcode::cmp_ge_i32:
            case machine::Opcode::cmp_ult_i32:
            case machine::Opcode::cmp_ule_i32:
            case machine::Opcode::cmp_ugt_i32:
            case machine::Opcode::cmp_uge_i32:
            case machine::Opcode::cmp_eq_i64:
            case machine::Opcode::cmp_ne_i64:
            case machine::Opcode::cmp_lt_i64:
            case machine::Opcode::cmp_le_i64:
            case machine::Opcode::cmp_gt_i64:
            case machine::Opcode::cmp_ge_i64:
            case machine::Opcode::cmp_ult_i64:
            case machine::Opcode::cmp_ule_i64:
            case machine::Opcode::cmp_ugt_i64:
            case machine::Opcode::cmp_uge_i64: {
                const bool immediate = instruction.symbol == "$cmpimm" && instruction.inputs.size() == 1U;
                if ((!immediate && instruction.inputs.size() != 2U) || instruction.inputs.empty()) {
                    add_error(diagnostics, "malformed compare instruction in @" + function.name);
                    return encoded;
                }
                const bool wide_compare = function.register_widths[instruction.inputs[0]] == 8;
                if (immediate) {
                    const auto& source = allocation.location(instruction.inputs[0]);
                    const auto value = static_cast<std::int32_t>(instruction.immediate);
                    if (source.kind == machine::LocationKind::physical_register)
                        emit_cmp_register_immediate(out, physical_register(source.physical), value, wide_compare);
                    else {
                        if (wide_compare) emit_read_location64(out, Register::eax, source);
                        else emit_read_location(out, Register::eax, source);
                        emit_cmp_register_immediate(out, Register::eax, value, wide_compare);
                    }
                } else {
                    if (wide_compare) { emit_read_location64(out, Register::eax, allocation.location(instruction.inputs[0])); read_integer_cached(Register::ecx, allocation.location(instruction.inputs[1]), true); out.byte(0x48); }
                    else { emit_read_location(out, Register::eax, allocation.location(instruction.inputs[0])); read_integer_cached(Register::ecx, allocation.location(instruction.inputs[1]), false); }
                    out.byte(0x39); out.byte(0xC8); // cmp rax/eax, rcx/ecx
                }
                out.byte(0x0F); out.byte(condition_code(instruction.opcode)); out.byte(0xC0); // setcc al
                out.byte(0x0F); out.byte(0xB6); out.byte(0xC0); // movzx eax, al
                write_integer_cached32(allocation.location(instruction.result), Register::eax);
                break;
            }
            case machine::Opcode::call_indirect_i32:
            case machine::Opcode::call_indirect_i64:
            case machine::Opcode::call_indirect_f32:
            case machine::Opcode::call_indirect_f64:
            case machine::Opcode::call_indirect_void: {
                if (instruction.inputs.empty() || instruction.inputs.size() > 65) {
                    add_error(diagnostics, "unsupported indirect call signature in @" + function.name);
                    return encoded;
                }
                const std::span call_args(instruction.inputs.data() + 1, instruction.inputs.size() - 1);
                const auto prepared_call = prepare_call_arguments(call_args);
                emit_read_location64(out, Register::eax, allocation.location(instruction.inputs[0]));
                out.byte(0xFF); out.byte(0xD0);
                emit_adjust_rsp(out, false, prepared_call.area);
                if (!direct_call_return[instruction.result] &&
                    !direct_call_float_arithmetic[instruction.result] &&
                    !direct_call_float_next_call[instruction.result] &&
                    !direct_call_integer_next_call[instruction.result]) {
                    if (instruction.opcode == machine::Opcode::call_indirect_i64) write_integer_cached64(allocation.location(instruction.result), Register::eax);
                    else if (instruction.opcode == machine::Opcode::call_indirect_i32) write_integer_cached32(allocation.location(instruction.result), Register::eax);
                    else if (instruction.opcode == machine::Opcode::call_indirect_f32) write_floating_cached(allocation.location(instruction.result), XmmRegister::xmm0, false);
                    else if (instruction.opcode == machine::Opcode::call_indirect_f64) write_floating_cached(allocation.location(instruction.result), XmmRegister::xmm0, true);
                }
                break;
            }
            case machine::Opcode::call_i32:
            case machine::Opcode::call_i64:
            case machine::Opcode::call_f32:
            case machine::Opcode::call_f64:
            case machine::Opcode::call_void:
            case machine::Opcode::call_aggregate: {
                const bool aggregate_call = instruction.opcode == machine::Opcode::call_aggregate;
                if (instruction.symbol.empty() || instruction.inputs.size() > 65 ||
                    (aggregate_call && instruction.inputs.empty())) {
                    add_error(diagnostics, "unsupported call signature in @" + function.name);
                    return encoded;
                }
                const std::span call_args = aggregate_call
                    ? std::span(instruction.inputs.data() + 1, instruction.inputs.size() - 1)
                    : std::span(instruction.inputs);
                const auto prepared_call = prepare_call_arguments(call_args, aggregate_call
                    ? std::optional<machine::VirtualRegister>(instruction.inputs[0]) : std::nullopt);
                out.byte(0xE8);
                const auto call_offset = out.size();
                out.i32(0);
                encoded.calls.push_back({call_offset, instruction.symbol});
                if (aggregate_call) {
                    if (!prepared_call.preserved_pointer_offset) {
                        add_error(diagnostics, "missing aggregate return destination in @" + function.name);
                        return encoded;
                    }
                    emit_load_rsp64(out, Register::r11d, *prepared_call.preserved_pointer_offset);
                    const auto piece_count = instruction.argument_index & 0xFFU;
                    std::uint32_t integer_index = 0;
                    std::uint32_t floating_index = 0;
                    for (std::uint32_t piece = 0; piece < piece_count; ++piece) {
                        const bool floating = (instruction.argument_index & (1U << (8U + piece))) != 0;
                        const auto displacement = static_cast<std::int32_t>(piece * 8U);
                        if (floating) {
                            const auto source = static_cast<XmmRegister>(floating_index++);
                            emit_sse_ptr_store(out, Register::r11d, source, true, displacement);
                        } else {
                            const auto source = integer_index++ == 0U ? Register::eax : Register::edx;
                            emit_store_ptr_i64(out, Register::r11d, source, displacement);
                        }
                    }
                }
                emit_adjust_rsp(out, false, prepared_call.area);
                if (instruction.result >= direct_call_return.size() ||
                    (!direct_call_return[instruction.result] &&
                     !direct_call_float_arithmetic[instruction.result] &&
                     !direct_call_float_next_call[instruction.result] &&
                     !direct_call_integer_next_call[instruction.result])) {
                    if (instruction.opcode == machine::Opcode::call_i64) write_integer_cached64(allocation.location(instruction.result), Register::eax);
                    else if (instruction.opcode == machine::Opcode::call_i32) write_integer_cached32(allocation.location(instruction.result), Register::eax);
                    else if (instruction.opcode == machine::Opcode::call_f32) write_floating_cached(allocation.location(instruction.result), XmmRegister::xmm0, false);
                    else if (instruction.opcode == machine::Opcode::call_f64) write_floating_cached(allocation.location(instruction.result), XmmRegister::xmm0, true);
                }
                break;
            }
            case machine::Opcode::jump: {
                if (instruction.successors.size() != 1) {
                    add_error(diagnostics, "malformed jump in @" + function.name);
                    return encoded;
                }
                const auto target = blocks.find(instruction.successors[0].block);
                if (target == blocks.end()) {
                    add_error(diagnostics, "unknown machine block " + instruction.successors[0].block);
                    return encoded;
                }
                emit_parallel_copies(out, instruction.successors[0], *target->second, allocation, function, diagnostics, function.name);
                if (!diagnostics.empty()) return encoded;
                if (next_block_name != nullptr && instruction.successors[0].block == *next_block_name) {
                    ++encoded.fallthrough_jump_removed_count;
                    encoded.layout_byte_avoided_count += 2U;
                    preserve_cache_into_next_block = preserves_fallthrough_edge;
                } else {
                    emit_jump(out, instruction.successors[0].block, fixups);
                }
                break;
            }
            case machine::Opcode::branch_i1: {
                const bool flags_compare = instruction.symbol == "$flags" && instruction.inputs.size() == 1U;
                const bool immediate_compare = instruction.symbol == "$cmpimm" && instruction.inputs.size() == 1U;
                const bool test_immediate = instruction.symbol == "$testimm" && instruction.inputs.size() == 1U;
                const bool floating_compare = instruction.symbol == "$fcmp" && instruction.inputs.size() == 2U;
                const bool fused_compare = flags_compare || floating_compare || immediate_compare || test_immediate || (instruction.immediate != 0 && instruction.inputs.size() == 2U);
                if ((!fused_compare && instruction.inputs.size() != 1U) || instruction.successors.size() != 2U) {
                    add_error(diagnostics, "malformed conditional branch in @" + function.name);
                    return encoded;
                }
                const auto true_target = blocks.find(instruction.successors[0].block);
                const auto false_target = blocks.find(instruction.successors[1].block);
                if (true_target == blocks.end() || false_target == blocks.end()) {
                    add_error(diagnostics, "conditional branch references unknown block in @" + function.name);
                    return encoded;
                }
                std::uint8_t false_condition = 0x84;
                if (fused_compare) {
                    const auto compare_opcode = (flags_compare || immediate_compare || test_immediate || floating_compare)
                        ? static_cast<machine::Opcode>(instruction.argument_index - 1U)
                        : static_cast<machine::Opcode>(instruction.immediate - 1);
                    if (floating_compare) {
                        const bool wide_compare = compare_opcode >= machine::Opcode::cmp_eq_f64 &&
                                                  compare_opcode <= machine::Opcode::cmp_ge_f64;
                        const bool swap = compare_opcode == machine::Opcode::cmp_lt_f32 ||
                                          compare_opcode == machine::Opcode::cmp_le_f32 ||
                                          compare_opcode == machine::Opcode::cmp_lt_f64 ||
                                          compare_opcode == machine::Opcode::cmp_le_f64;
                        emit_read_float_location(out, XmmRegister::xmm0, allocation.location(instruction.inputs[swap ? 1U : 0U]), wide_compare);
                        emit_read_float_location(out, XmmRegister::xmm1, allocation.location(instruction.inputs[swap ? 0U : 1U]), wide_compare);
                        emit_ucomi(out, XmmRegister::xmm0, XmmRegister::xmm1, wide_compare);
                        const bool strict = compare_opcode == machine::Opcode::cmp_lt_f32 || compare_opcode == machine::Opcode::cmp_gt_f32 ||
                                            compare_opcode == machine::Opcode::cmp_lt_f64 || compare_opcode == machine::Opcode::cmp_gt_f64;
                        false_condition = strict ? 0x86 : 0x82; // jbe/jb; unordered is false
                    } else {
                        const auto set_condition = condition_code(compare_opcode);
                        if (set_condition == 0U) {
                            add_error(diagnostics, "invalid fused comparison in @" + function.name);
                            return encoded;
                        }
                        const bool wide_compare = function.register_widths[instruction.inputs[0]] == 8U;
                        if (flags_compare) {
                            // The immediately preceding integer arithmetic instruction already
                            // produced the zero flag consumed by eq/ne zero.
                        } else if (test_immediate) {
                            const auto& source = allocation.location(instruction.inputs[0]);
                            const auto value = static_cast<std::int32_t>(instruction.immediate);
                            if (source.kind == machine::LocationKind::physical_register) {
                                emit_test_register_immediate(out, physical_register(source.physical), value, wide_compare);
                            } else {
                                if (wide_compare) emit_read_location64(out, Register::eax, source);
                                else emit_read_location(out, Register::eax, source);
                                emit_test_register_immediate(out, Register::eax, value, wide_compare);
                            }
                        } else if (immediate_compare) {
                            const auto& left = allocation.location(instruction.inputs[0]);
                            const auto value = static_cast<std::int32_t>(instruction.immediate);
                            if (left.kind == machine::LocationKind::physical_register) {
                                emit_cmp_register_immediate(out, physical_register(left.physical), value, wide_compare);
                            } else {
                                if (wide_compare) emit_read_location64(out, Register::eax, left);
                                else emit_read_location(out, Register::eax, left);
                                emit_cmp_register_immediate(out, Register::eax, value, wide_compare);
                            }
                        } else {
                            const auto& left = allocation.location(instruction.inputs[0]);
                            const auto& right = allocation.location(instruction.inputs[1]);
                            if (left.kind == machine::LocationKind::physical_register &&
                                right.kind == machine::LocationKind::physical_register) {
                                emit_cmp_registers(out, physical_register(left.physical),
                                                   physical_register(right.physical), wide_compare);
                            } else {
                                if (wide_compare) {
                                    emit_read_location64(out, Register::eax, left);
                                    read_integer_cached(Register::ecx, right, true);
                                } else {
                                    emit_read_location(out, Register::eax, left);
                                    read_integer_cached(Register::ecx, right, false);
                                }
                                emit_cmp_registers(out, Register::eax, Register::ecx, wide_compare);
                            }
                        }
                        false_condition = static_cast<std::uint8_t>(((set_condition - 0x10U) ^ 0x01U));
                    }
                } else {
                    emit_read_location(out, Register::eax, allocation.location(instruction.inputs[0]));
                    out.byte(0x85); out.byte(0xC0); // test eax, eax
                }
                const bool true_fallthrough = next_block_name != nullptr &&
                    instruction.successors[0].block == *next_block_name;
                const bool false_fallthrough = next_block_name != nullptr &&
                    instruction.successors[1].block == *next_block_name;
                const bool false_edge_direct = edge_copies_are_noop(instruction.successors[1], *false_target->second, allocation);
                const bool true_edge_direct = edge_copies_are_noop(instruction.successors[0], *true_target->second, allocation);
                if ((true_fallthrough && false_edge_direct) || (false_fallthrough && true_edge_direct)) {
                    const auto direct_condition = true_fallthrough
                        ? false_condition
                        : static_cast<std::uint8_t>(false_condition ^ 0x01U);
                    const auto& target_successor = true_fallthrough
                        ? instruction.successors[1] : instruction.successors[0];
                    const auto conditional_offset = out.size();
                    out.byte(0x0F); out.byte(direct_condition);
                    const auto displacement_offset = out.size();
                    out.i32(0);
                    local_branch_fixups.push_back({conditional_offset, displacement_offset, 0U,
                                                   target_successor.block, direct_condition});
                    const auto& fallthrough_successor = true_fallthrough
                        ? instruction.successors[0] : instruction.successors[1];
                    const auto& fallthrough_target = true_fallthrough
                        ? *true_target->second : *false_target->second;
                    emit_parallel_copies(out, fallthrough_successor, fallthrough_target,
                                         allocation, function, diagnostics, function.name);
                    if (!diagnostics.empty()) return encoded;
                    ++encoded.fallthrough_jump_removed_count;
                    encoded.layout_byte_avoided_count += 4U;
                    if (false_fallthrough) ++encoded.branch_inverted_count;
                } else {
                    const auto conditional_offset = out.size();
                    out.byte(0x0F); out.byte(false_condition); // provisional jcc rel32 to false edge stub
                    const auto false_offset = out.size();
                    out.i32(0);
                    emit_parallel_copies(out, instruction.successors[0], *true_target->second, allocation, function, diagnostics, function.name);
                    if (!diagnostics.empty()) return encoded;
                    emit_jump(out, instruction.successors[0].block, fixups);
                    const auto false_stub = out.size();
                    local_branch_fixups.push_back({conditional_offset, false_offset, false_stub, {}, false_condition});
                    emit_parallel_copies(out, instruction.successors[1], *false_target->second, allocation, function, diagnostics, function.name);
                    if (!diagnostics.empty()) return encoded;
                    if (next_block_name != nullptr && instruction.successors[1].block == *next_block_name) {
                        ++encoded.fallthrough_jump_removed_count;
                        encoded.layout_byte_avoided_count += 2U;
                    } else {
                        emit_jump(out, instruction.successors[1].block, fixups);
                    }
                }
                break;
            }
            case machine::Opcode::return_aggregate: {
                if (instruction.inputs.size() != 1U) {
                    add_error(diagnostics, "malformed aggregate return in @" + function.name);
                    return encoded;
                }
                emit_read_location64(out, Register::r11d, allocation.location(instruction.inputs[0]));
                const auto piece_count = instruction.argument_index & 0xFFU;
                std::uint32_t integer_index = 0;
                std::uint32_t floating_index = 0;
                for (std::uint32_t piece = 0; piece < piece_count; ++piece) {
                    const bool floating = (instruction.argument_index & (1U << (8U + piece))) != 0;
                    const auto displacement = static_cast<std::int32_t>(piece * 8U);
                    if (floating) {
                        const auto destination = static_cast<XmmRegister>(floating_index++);
                        emit_sse_ptr_load(out, destination, Register::r11d, true, displacement);
                    } else {
                        const auto destination = integer_index++ == 0U ? Register::eax : Register::edx;
                        emit_load_ptr_i64(out, destination, Register::r11d, displacement);
                    }
                }
                emit_epilogue();
                break;
            }
            case machine::Opcode::return_f32:
            case machine::Opcode::return_f64: {
                const bool wide = instruction.opcode == machine::Opcode::return_f64;
                if (instruction.inputs.size() != 1) { add_error(diagnostics, "malformed floating return in @" + function.name); return encoded; }
                if (instruction.inputs[0] >= direct_call_return.size() || !direct_call_return[instruction.inputs[0]])
                    emit_read_float_location(out, XmmRegister::xmm0, allocation.location(instruction.inputs[0]), wide);
                emit_epilogue();
                break;
            }
            case machine::Opcode::return_i64:
                if (instruction.symbol == "$retloadstack") {
                    if (!instruction.inputs.empty() || instruction.argument_index != 8U ||
                        instruction.immediate > -8 || -instruction.immediate > static_cast<std::int64_t>(function.local_stack_size)) {
                        add_error(diagnostics, "malformed folded i64 stack-load return in @" + function.name); return encoded;
                    }
                    emit_load_stack64(out, Register::eax, static_cast<std::int32_t>(instruction.immediate));
                    emit_epilogue();
                    break;
                }
                if (instruction.symbol == "$retimm") {
                    if (!instruction.inputs.empty()) { add_error(diagnostics, "malformed direct i64 return in @" + function.name); return encoded; }
                    if (instruction.immediate == 0) { out.byte(0x31); out.byte(0xC0); }
                    else if (instruction.immediate > 0 &&
                             static_cast<std::uint64_t>(instruction.immediate) <= std::numeric_limits<std::uint32_t>::max()) {
                        out.byte(0xB8); out.i32(static_cast<std::int32_t>(instruction.immediate));
                    } else if (instruction.immediate >= std::numeric_limits<std::int32_t>::min() &&
                               instruction.immediate <= std::numeric_limits<std::int32_t>::max()) {
                        out.byte(0x48); out.byte(0xC7); out.byte(0xC0);
                        out.i32(static_cast<std::int32_t>(instruction.immediate));
                    } else { out.byte(0x48); out.byte(0xB8); out.i64(instruction.immediate); }
                    emit_epilogue();
                    break;
                }
                if (instruction.inputs.size() != 1) { add_error(diagnostics, "malformed i64 return in @" + function.name); return encoded; }
                if (instruction.inputs[0] >= direct_call_return.size() || !direct_call_return[instruction.inputs[0]])
                    emit_read_location64(out, Register::eax, allocation.location(instruction.inputs[0]));
                emit_epilogue();
                break;
            case machine::Opcode::return_i32:
                if (instruction.symbol == "$retloadstack") {
                    if (!instruction.inputs.empty() || instruction.immediate >= 0 ||
                        -instruction.immediate > static_cast<std::int64_t>(function.local_stack_size)) {
                        add_error(diagnostics, "malformed folded stack-load return in @" + function.name); return encoded;
                    }
                    if (instruction.argument_index == 1U) emit_load_stack_i8(out, Register::eax, static_cast<std::int32_t>(instruction.immediate));
                    else if (instruction.argument_index == 2U) emit_load_stack_i16(out, Register::eax, static_cast<std::int32_t>(instruction.immediate));
                    else if (instruction.argument_index == 4U) emit_load_stack(out, Register::eax, static_cast<std::int32_t>(instruction.immediate));
                    else { add_error(diagnostics, "invalid folded stack-load return width in @" + function.name); return encoded; }
                    emit_epilogue();
                    break;
                }
                if (instruction.symbol == "$retimm") {
                    if (!instruction.inputs.empty()) { add_error(diagnostics, "malformed direct return in @" + function.name); return encoded; }
                    if (instruction.immediate == 0) { out.byte(0x31); out.byte(0xC0); }
                    else { out.byte(0xB8); out.i32(static_cast<std::int32_t>(instruction.immediate)); }
                    emit_epilogue();
                    break;
                }
                if (instruction.inputs.size() != 1) {
                    add_error(diagnostics, "malformed return in @" + function.name);
                    return encoded;
                }
                if (instruction.inputs[0] >= direct_call_return.size() || !direct_call_return[instruction.inputs[0]])
                    emit_read_location(out, Register::eax, allocation.location(instruction.inputs[0]));
                emit_epilogue();
                break;
            case machine::Opcode::return_void:
                if (!instruction.inputs.empty()) {
                    add_error(diagnostics, "malformed void return in @" + function.name);
                    return encoded;
                }
                emit_epilogue();
                break;
            }
            for (const auto input : instruction.inputs)
                if (input < result_use_counts.size() && result_use_counts[input] > 0) --result_use_counts[input];
            for (const auto& successor : instruction.successors)
                for (const auto argument : successor.arguments)
                    if (argument < result_use_counts.size() && result_use_counts[argument] > 0) --result_use_counts[argument];
        }
    }

    invalidate_spill_caches();

    // Solve unconditional and conditional branch sizes together. Both branch kinds
    // participate in one fixed-point layout because shrinking an earlier edge can
    // bring a later edge into rel8 range, and shrinking a branch inside a conditional
    // stub changes the local jcc displacement that reaches that stub.
    struct RelaxationCandidate {
        enum class Kind : std::uint8_t { jump, conditional } kind{Kind::jump};
        std::size_t instruction_offset{};
        std::size_t original_size{};
        std::size_t target_offset{};
        std::size_t source_index{};
        std::uint8_t condition{};
        bool shortened{};
    };

    std::vector<RelaxationCandidate> candidates;
    candidates.reserve(fixups.size() + local_branch_fixups.size());
    for (std::size_t index = 0; index < fixups.size(); ++index) {
        const auto target = labels.find(fixups[index].target);
        if (target == labels.end()) {
            add_error(diagnostics, "unresolved machine block " + fixups[index].target + " in @" + function.name);
            return encoded;
        }
        candidates.push_back({RelaxationCandidate::Kind::jump, fixups[index].instruction_offset,
                              5U, target->second, index, 0U, false});
    }
    for (std::size_t index = 0; index < local_branch_fixups.size(); ++index) {
        const auto& fixup = local_branch_fixups[index];
        std::size_t target_offset = fixup.target_offset;
        if (!fixup.target_label.empty()) {
            const auto target = labels.find(fixup.target_label);
            if (target == labels.end()) {
                add_error(diagnostics, "unresolved machine block " + fixup.target_label + " in @" + function.name);
                return encoded;
            }
            target_offset = target->second;
        }
        candidates.push_back({RelaxationCandidate::Kind::conditional, fixup.instruction_offset,
                              6U, target_offset, index, fixup.condition, false});
    }
    std::sort(candidates.begin(), candidates.end(), [](const auto& left, const auto& right) {
        return left.instruction_offset < right.instruction_offset;
    });

    const auto adjusted_offset = [&](std::size_t original) {
        std::size_t removed = 0;
        for (const auto& candidate : candidates) {
            if (!candidate.shortened || candidate.instruction_offset >= original) continue;
            removed += candidate.kind == RelaxationCandidate::Kind::jump ? 3U : 4U;
        }
        return original - removed;
    };

    bool changed = true;
    while (changed) {
        changed = false;
        for (auto& candidate : candidates) {
            if (candidate.shortened) continue;
            const auto source = adjusted_offset(candidate.instruction_offset);
            const auto destination = adjusted_offset(candidate.target_offset);
            const auto delta = static_cast<std::int64_t>(destination) - static_cast<std::int64_t>(source + 2U);
            if (delta >= std::numeric_limits<std::int8_t>::min() &&
                delta <= std::numeric_limits<std::int8_t>::max()) {
                candidate.shortened = true;
                changed = true;
            }
        }
    }

    const auto original_code = out.bytes();
    std::vector<std::byte> relaxed_code;
    relaxed_code.reserve(original_code.size());
    std::size_t cursor = 0;
    for (const auto& candidate : candidates) {
        relaxed_code.insert(relaxed_code.end(), original_code.begin() + static_cast<std::ptrdiff_t>(cursor),
                            original_code.begin() + static_cast<std::ptrdiff_t>(candidate.instruction_offset));
        const auto source = adjusted_offset(candidate.instruction_offset);
        const auto destination = adjusted_offset(candidate.target_offset);
        if (candidate.shortened) {
            const auto delta = static_cast<std::int64_t>(destination) - static_cast<std::int64_t>(source + 2U);
            const auto encoded_delta = static_cast<std::byte>(static_cast<std::uint8_t>(static_cast<std::int8_t>(delta)));
            if (candidate.kind == RelaxationCandidate::Kind::jump) {
                relaxed_code.push_back(static_cast<std::byte>(0xEB));
                relaxed_code.push_back(encoded_delta);
                ++encoded.short_branch_count;
                if (candidate.target_offset > candidate.instruction_offset) ++encoded.forward_short_branch_count;
                encoded.short_branch_byte_avoided_count += 3U;
            } else {
                relaxed_code.push_back(static_cast<std::byte>(static_cast<std::uint8_t>(candidate.condition - 0x10U)));
                relaxed_code.push_back(encoded_delta);
                ++encoded.short_conditional_branch_count;
                encoded.short_conditional_branch_byte_avoided_count += 4U;
            }
        } else if (candidate.kind == RelaxationCandidate::Kind::jump) {
            const auto delta = static_cast<std::int64_t>(destination) - static_cast<std::int64_t>(source + 5U);
            if (delta < std::numeric_limits<std::int32_t>::min() || delta > std::numeric_limits<std::int32_t>::max()) {
                add_error(diagnostics, "x86-64 branch displacement out of range in @" + function.name);
                return encoded;
            }
            relaxed_code.push_back(static_cast<std::byte>(0xE9));
            const auto bits = static_cast<std::uint32_t>(static_cast<std::int32_t>(delta));
            for (unsigned shift = 0; shift < 32; shift += 8)
                relaxed_code.push_back(static_cast<std::byte>(static_cast<std::uint8_t>(bits >> shift)));
        } else {
            const auto delta = static_cast<std::int64_t>(destination) - static_cast<std::int64_t>(source + 6U);
            if (delta < std::numeric_limits<std::int32_t>::min() || delta > std::numeric_limits<std::int32_t>::max()) {
                add_error(diagnostics, "x86-64 conditional branch displacement out of range in @" + function.name);
                return encoded;
            }
            relaxed_code.push_back(static_cast<std::byte>(0x0F));
            relaxed_code.push_back(static_cast<std::byte>(candidate.condition));
            const auto bits = static_cast<std::uint32_t>(static_cast<std::int32_t>(delta));
            for (unsigned shift = 0; shift < 32; shift += 8)
                relaxed_code.push_back(static_cast<std::byte>(static_cast<std::uint8_t>(bits >> shift)));
        }
        cursor = candidate.instruction_offset + candidate.original_size;
    }
    relaxed_code.insert(relaxed_code.end(), original_code.begin() + static_cast<std::ptrdiff_t>(cursor), original_code.end());

    const auto remap_offset = [&](std::size_t original) { return adjusted_offset(original); };
    for (auto& call : encoded.calls) call.displacement_offset = remap_offset(call.displacement_offset);
    for (auto& address : encoded.addresses) address.displacement_offset = remap_offset(address.displacement_offset);
    for (auto& address : encoded.global_addresses) address.address_offset = remap_offset(address.address_offset);
    out.replace(std::move(relaxed_code));

    encoded.encoded_byte_count = static_cast<std::uint32_t>(out.size());
    if (encoded.machine_instruction_count_before_optimization == 0U)
        encoded.machine_instruction_count_before_optimization = encoded.machine_instruction_count;
    encoded.machine_instruction_eliminated_count =
        encoded.machine_instruction_count_before_optimization - encoded.machine_instruction_count;
    encoded.pre_optimization_encoded_byte_count = encoded.encoded_byte_count + encoded.eliminated_encoded_byte_count;
    encoded.spill_load_count = out.spill_load_count();
    encoded.spill_store_count = out.spill_store_count();
    encoded.code = out.take();
    return encoded;
}
}

EncodeResult encode(const machine::Module& module, Abi abi) {
    EncodeResult result;
    result.diagnostics = machine::verify_module(module);
    if (!result.diagnostics.empty()) return result;
    for (const auto& function : module.functions) {
        auto encoded = encode_function(function, abi, result.diagnostics);
        if (!encoded.calls.empty() || !encoded.addresses.empty() || !encoded.global_addresses.empty() || !encoded.tls_addresses.empty()) {
            add_error(result.diagnostics, "standalone function encoding cannot resolve internal calls or addresses; use encode_image");
            return result;
        }
        if (!encoded.code.empty()) result.functions.push_back(std::move(encoded));
    }
    return result;
}

ImageEncodeResult assemble_image(std::vector<EncodedFunction> functions,
                                 std::span<const machine::Global> globals) {
    ImageEncodeResult result;
    std::unordered_map<std::string, std::size_t> entries;
    struct GlobalLocation { DataSection section; std::size_t offset; };
    std::unordered_map<std::string, GlobalLocation> global_offsets;
    std::unordered_set<std::string> external_globals;
    std::unordered_set<std::string> external_tls;

    for (const auto& global : globals) {
        if (global.is_external) {
            if (global.is_thread_local) external_tls.insert(global.name);
            else external_globals.insert(global.name);
            continue;
        }
        const auto alignment = std::max<std::size_t>(1, global.alignment);
        const auto section = global.is_thread_local ? DataSection::tls :
            (global.is_constant ? DataSection::read_only : DataSection::writable);
        auto& bytes = global.is_thread_local ? result.image.thread_local_data :
            (global.is_constant ? result.image.read_only_data : result.image.writable_data);
        while ((bytes.size() & (alignment - 1U)) != 0) bytes.push_back(std::byte{0});
        const auto offset = bytes.size();
        global_offsets.emplace(global.name, GlobalLocation{section, offset});
        result.image.globals.push_back({global.name, section, offset});
        for (const auto byte : global.initializer) bytes.push_back(static_cast<std::byte>(byte));
    }

    for (const auto& function : functions) {
        if (!entries.emplace(function.name, result.image.code.size()).second) {
            add_error(result.diagnostics, "duplicate encoded function @" + function.name);
            return result;
        }
        result.image.entries.emplace_back(function.name, result.image.code.size());
        result.image.code.insert(result.image.code.end(), function.code.begin(), function.code.end());
    }
    result.functions = functions;

    std::size_t base = 0;
    for (const auto& function : functions) {
        for (const auto& call : function.calls) {
            auto target = entries.find(call.target);
            if (target == entries.end()) {
                const auto existing = std::find_if(result.image.externals.begin(), result.image.externals.end(),
                    [&](const ExternalRelocation& relocation) { return relocation.symbol == call.target; });
                if (existing == result.image.externals.end()) {
                    const auto thunk_offset = result.image.code.size();
                    result.image.code.push_back(std::byte{0x48});
                    result.image.code.push_back(std::byte{0xB8});
                    const auto address_offset = result.image.code.size();
                    for (int i = 0; i < 8; ++i) result.image.code.push_back(std::byte{0});
                    result.image.code.push_back(std::byte{0xFF});
                    result.image.code.push_back(std::byte{0xE0});
                    result.image.externals.push_back({call.target, address_offset});
                    entries.emplace(call.target, thunk_offset);
                }
                target = entries.find(call.target);
            }
            const auto absolute_offset = base + call.displacement_offset;
            const auto delta = static_cast<std::int64_t>(target->second) - static_cast<std::int64_t>(absolute_offset + 4U);
            if (delta < std::numeric_limits<std::int32_t>::min() || delta > std::numeric_limits<std::int32_t>::max()) {
                add_error(result.diagnostics, "internal call displacement out of range");
                return result;
            }
            const auto bits = static_cast<std::uint32_t>(static_cast<std::int32_t>(delta));
            for (unsigned shift = 0; shift < 32; shift += 8)
                result.image.code.at(absolute_offset + shift / 8U) = static_cast<std::byte>(static_cast<std::uint8_t>(bits >> shift));
        }
        for (const auto& address : function.addresses) {
            auto target = entries.find(address.target);
            if (target == entries.end()) {
                const auto existing = std::find_if(result.image.externals.begin(), result.image.externals.end(),
                    [&](const ExternalRelocation& relocation) { return relocation.symbol == address.target; });
                if (existing == result.image.externals.end()) {
                    const auto thunk_offset = result.image.code.size();
                    result.image.code.push_back(std::byte{0x48});
                    result.image.code.push_back(std::byte{0xB8});
                    const auto address_offset = result.image.code.size();
                    for (int i = 0; i < 8; ++i) result.image.code.push_back(std::byte{0});
                    result.image.code.push_back(std::byte{0xFF});
                    result.image.code.push_back(std::byte{0xE0});
                    result.image.externals.push_back({address.target, address_offset});
                    entries.emplace(address.target, thunk_offset);
                }
                target = entries.find(address.target);
            }
            const auto absolute_offset = base + address.displacement_offset;
            const auto delta = static_cast<std::int64_t>(target->second) - static_cast<std::int64_t>(absolute_offset + 4U);
            if (delta < std::numeric_limits<std::int32_t>::min() || delta > std::numeric_limits<std::int32_t>::max()) {
                add_error(result.diagnostics, "function address displacement out of range");
                return result;
            }
            const auto bits = static_cast<std::uint32_t>(static_cast<std::int32_t>(delta));
            for (unsigned shift = 0; shift < 32; shift += 8)
                result.image.code.at(absolute_offset + shift / 8U) = static_cast<std::byte>(static_cast<std::uint8_t>(bits >> shift));
        }
        for (const auto& address : function.global_addresses) {
            const auto absolute_offset = base + address.address_offset;
            const auto target = global_offsets.find(address.target);
            if (target != global_offsets.end()) {
                result.image.global_relocations.push_back({absolute_offset, target->second.section, target->second.offset});
            } else if (external_globals.contains(address.target)) {
                result.image.external_globals.push_back({address.target, absolute_offset});
            } else {
                add_error(result.diagnostics, "unknown global address @" + address.target);
                return result;
            }
        }
        for (const auto& address : function.tls_addresses) {
            const auto absolute_offset = base + address.address_offset;
            const auto target = global_offsets.find(address.target);
            if (target != global_offsets.end() && target->second.section == DataSection::tls) {
                result.image.tls_relocations.push_back({absolute_offset, target->second.section, target->second.offset});
            } else if (external_tls.contains(address.target)) {
                result.image.external_tls.push_back({address.target, absolute_offset});
            } else {
                add_error(result.diagnostics, "unknown TLS address @" + address.target);
                return result;
            }
        }
        base += function.code.size();
    }
    return result;
}

ImageEncodeResult encode_image(const machine::Module& module, Abi abi) {
    ImageEncodeResult result;
    result.diagnostics = machine::verify_module(module);
    if (!result.diagnostics.empty()) return result;

    const auto function_count = module.functions.size();
    std::vector<EncodedFunction> functions(function_count);
    std::vector<Diagnostics> function_diagnostics(function_count);

    // Function encoding is independent until image assembly resolves calls and
    // addresses. Encode compiler-sized modules in parallel while retaining the
    // source function order so object output stays deterministic.
    const auto hardware_threads = std::max(1U, std::thread::hardware_concurrency());
    // Each worker owns a full machine-function copy plus liveness/allocation
    // scratch. On high-core-count hosts, matching every reported logical CPU
    // can create hundreds of heavyweight workers and lose throughput to cache
    // and allocator contention. Keep compiler-sized module encoding bounded.
    constexpr std::size_t max_encode_workers = 16U;
    const auto worker_count = std::min<std::size_t>(
        function_count, std::min<std::size_t>(hardware_threads, max_encode_workers));
    std::atomic_size_t next_function{0};
    auto encode_worker = [&]() {
        for (;;) {
            const auto index = next_function.fetch_add(1, std::memory_order_relaxed);
            if (index >= function_count) return;
            functions[index] = encode_function(module.functions[index], abi, function_diagnostics[index]);
        }
    };

    if (worker_count <= 1) {
        encode_worker();
    } else {
        std::vector<std::thread> workers;
        workers.reserve(worker_count);
        for (std::size_t index = 0; index < worker_count; ++index) workers.emplace_back(encode_worker);
        for (auto& worker : workers) worker.join();
    }

    for (std::size_t index = 0; index < function_count; ++index) {
        result.diagnostics.insert(result.diagnostics.end(),
                                  function_diagnostics[index].begin(),
                                  function_diagnostics[index].end());
        if (functions[index].code.empty()) return result;
    }

    if (!result.diagnostics.empty()) return result;
    return assemble_image(std::move(functions), module.globals);
}

Diagnostics resolve_externals(EncodedModuleImage& image, const ExternalResolver& resolver) {
    Diagnostics diagnostics;
    if (!image.tls_relocations.empty() || !image.external_tls.empty()) {
        add_error(diagnostics, "JIT/external address resolution does not support TLS; emit a native object instead");
        return diagnostics;
    }

    for (const auto& relocation : image.externals) {
        const auto address = resolver(relocation.symbol);
        if (!address) {
            add_error(diagnostics, "unresolved external symbol @" + relocation.symbol);
            continue;
        }
        if (relocation.address_offset + 8U > image.code.size()) {
            add_error(diagnostics, "invalid external relocation for @" + relocation.symbol);
            continue;
        }
        const auto bits = static_cast<std::uint64_t>(*address);
        for (unsigned shift = 0; shift < 64; shift += 8)
            image.code[relocation.address_offset + shift / 8U] = static_cast<std::byte>(static_cast<std::uint8_t>(bits >> shift));
    }
    return diagnostics;
}

std::string format_hex(std::span<const std::byte> code) {
    std::ostringstream out;
    out << std::hex << std::setfill('0');
    for (std::size_t index = 0; index < code.size(); ++index) {
        if (index != 0) out << ' ';
        out << std::setw(2) << std::to_integer<unsigned>(code[index]);
    }
    return out.str();
}

} // namespace forge::codegen::x86_64
