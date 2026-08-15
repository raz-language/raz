// Copyright 2026 Mario Vinciguerra
// SPDX-License-Identifier: Apache-2.0

#include "forge/jit/invoke.hpp"

#include <string>

namespace forge::jit {
namespace {

void add_error(Diagnostics& diagnostics, std::string message) {
    diagnostics.push_back({DiagnosticSeverity::error, std::move(message), {}});
}

template <typename Return>
Return invoke_value(void* address, std::span<const std::uint64_t> arguments) {
    using U = std::uint64_t;
    switch (arguments.size()) {
        case 0: return reinterpret_cast<Return (*)()>(address)();
        case 1: return reinterpret_cast<Return (*)(U)>(address)(arguments[0]);
        case 2: return reinterpret_cast<Return (*)(U, U)>(address)(arguments[0], arguments[1]);
        case 3: return reinterpret_cast<Return (*)(U, U, U)>(address)(arguments[0], arguments[1], arguments[2]);
        case 4: return reinterpret_cast<Return (*)(U, U, U, U)>(address)(arguments[0], arguments[1], arguments[2], arguments[3]);
        case 5: return reinterpret_cast<Return (*)(U, U, U, U, U)>(address)(arguments[0], arguments[1], arguments[2], arguments[3], arguments[4]);
        case 6: return reinterpret_cast<Return (*)(U, U, U, U, U, U)>(address)(arguments[0], arguments[1], arguments[2], arguments[3], arguments[4], arguments[5]);
        case 7: return reinterpret_cast<Return (*)(U, U, U, U, U, U, U)>(address)(arguments[0], arguments[1], arguments[2], arguments[3], arguments[4], arguments[5], arguments[6]);
        case 8: return reinterpret_cast<Return (*)(U, U, U, U, U, U, U, U)>(address)(arguments[0], arguments[1], arguments[2], arguments[3], arguments[4], arguments[5], arguments[6], arguments[7]);
        default: return Return{};
    }
}

void invoke_void(void* address, std::span<const std::uint64_t> arguments) {
    using U = std::uint64_t;
    switch (arguments.size()) {
        case 0: reinterpret_cast<void (*)()>(address)(); break;
        case 1: reinterpret_cast<void (*)(U)>(address)(arguments[0]); break;
        case 2: reinterpret_cast<void (*)(U, U)>(address)(arguments[0], arguments[1]); break;
        case 3: reinterpret_cast<void (*)(U, U, U)>(address)(arguments[0], arguments[1], arguments[2]); break;
        case 4: reinterpret_cast<void (*)(U, U, U, U)>(address)(arguments[0], arguments[1], arguments[2], arguments[3]); break;
        case 5: reinterpret_cast<void (*)(U, U, U, U, U)>(address)(arguments[0], arguments[1], arguments[2], arguments[3], arguments[4]); break;
        case 6: reinterpret_cast<void (*)(U, U, U, U, U, U)>(address)(arguments[0], arguments[1], arguments[2], arguments[3], arguments[4], arguments[5]); break;
        case 7: reinterpret_cast<void (*)(U, U, U, U, U, U, U)>(address)(arguments[0], arguments[1], arguments[2], arguments[3], arguments[4], arguments[5], arguments[6]); break;
        case 8: reinterpret_cast<void (*)(U, U, U, U, U, U, U, U)>(address)(arguments[0], arguments[1], arguments[2], arguments[3], arguments[4], arguments[5], arguments[6], arguments[7]); break;
        default: break;
    }
}

} // namespace

InvokeResult invoke_integer(void* address, std::span<const std::uint64_t> arguments, bool returns_void) {
    InvokeResult result;
    if (address == nullptr) {
        add_error(result.diagnostics, "cannot invoke a null JIT entry point");
        return result;
    }

    if (arguments.size() > 8) {
        add_error(result.diagnostics, "host JIT invocation supports at most eight integer arguments");
        return result;
    }

    if (returns_void) {
        invoke_void(address, arguments);
        return result;
    }
    result.bits = invoke_value<std::uint64_t>(address, arguments);
    return result;
}

InvokeResult invoke_pointer(void* address, std::span<const std::uint64_t> arguments) {
    return invoke_integer(address, arguments, false);
}

} // namespace forge::jit
