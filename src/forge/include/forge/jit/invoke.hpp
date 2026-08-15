// Copyright 2026 Mario Vinciguerra
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdint>
#include <span>

#include "forge/diagnostics/diagnostic.hpp"

namespace forge::jit {

struct InvokeResult {
    std::uint64_t bits{};
    Diagnostics diagnostics;
    [[nodiscard]] bool ok() const noexcept { return diagnostics.empty(); }
    [[nodiscard]] void* pointer() const noexcept { return reinterpret_cast<void*>(static_cast<std::uintptr_t>(bits)); }
};

// Invoke an x86-64 host entry point using the platform C ABI. Integer values are
// carried in full 64-bit argument slots; Forge's logical result width is applied
// by the caller. Up to eight arguments are supported, covering both register and
// stack-passed ABI paths on Windows x64 and System V.
[[nodiscard]] InvokeResult invoke_integer(
    void* address,
    std::span<const std::uint64_t> arguments,
    bool returns_void = false);

// Invoke an entry point that returns a host pointer. Pointer arguments are passed
// through the same full-width ABI slots as integer arguments.
[[nodiscard]] InvokeResult invoke_pointer(
    void* address,
    std::span<const std::uint64_t> arguments);

} // namespace forge::jit
