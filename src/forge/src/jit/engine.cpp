// Copyright 2026 Mario Vinciguerra
// SPDX-License-Identifier: Apache-2.0

#include "forge/jit/engine.hpp"

#include <cstring>
#include <cstdlib>
#include <limits>

#if defined(_WIN32)
#ifndef NOMINMAX
#ifndef NOMINMAX
#define NOMINMAX 1
#endif
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN 1
#endif
#include <windows.h>
#elif defined(__unix__) || defined(__APPLE__)
#include <sys/mman.h>
#include <unistd.h>
#endif

namespace forge::jit {
namespace {
void error(Diagnostics& diagnostics, std::string message) {
    diagnostics.push_back({DiagnosticSeverity::error, std::move(message), {}});
}

std::size_t align_up(std::size_t value, std::size_t alignment) {
    return ((value + alignment - 1U) / alignment) * alignment;
}

void* forge_memmove(void* destination, const void* source, std::size_t count) {
    return std::memmove(destination, source, count);
}

void* forge_memset(void* destination, int value, std::size_t count) {
    return std::memset(destination, value, count);
}

void* forge_callback_element_address(void* base, std::uint64_t index, std::uint64_t count) {
    const auto address = reinterpret_cast<std::uintptr_t>(base);
    if (index >= count || index > (std::numeric_limits<std::uintptr_t>::max() - address) / sizeof(std::uintptr_t)) std::abort();
    return reinterpret_cast<void*>(address + static_cast<std::uintptr_t>(index * sizeof(std::uintptr_t)));
}

std::optional<std::uintptr_t> builtin_symbol(std::string_view name) {
    if (name == "__forge_memmove") return reinterpret_cast<std::uintptr_t>(&forge_memmove);
    if (name == "__forge_memset") return reinterpret_cast<std::uintptr_t>(&forge_memset);
    if (name == "__forge_callback_element_address") return reinterpret_cast<std::uintptr_t>(&forge_callback_element_address);
    return std::nullopt;
}
}

Engine::~Engine() {
    if (!memory_) return;
#if defined(_WIN32)
    ::VirtualFree(memory_, 0, MEM_RELEASE);
#elif defined(__unix__) || defined(__APPLE__)
    ::munmap(memory_, allocation_size_);
#endif
}

void* Engine::lookup(std::string_view name) const noexcept {
    if (!memory_) return nullptr;
    for (const auto& entry : entries_) {
        if (entry.first == name) return static_cast<std::byte*>(memory_) + entry.second;
    }
    return nullptr;
}

void* Engine::lookup_global(std::string_view name) const noexcept {
    if (!memory_) return nullptr;
    for (const auto& entry : globals_) {
        if (entry.name != name) continue;
        const auto base = entry.section == codegen::x86_64::DataSection::read_only
            ? read_only_data_offset_ : writable_data_offset_;
        return static_cast<std::byte*>(memory_) + base + entry.data_offset;
    }
    return nullptr;
}

bool Engine::is_global_read_only(std::string_view name) const noexcept {
    for (const auto& entry : globals_) {
        if (entry.name == name) return entry.section == codegen::x86_64::DataSection::read_only;
    }
    return false;
}

LoadResult load(const machine::Module& module, codegen::x86_64::Abi abi,
                const codegen::x86_64::ExternalResolver& resolver,
                const codegen::x86_64::ExternalResolver& global_resolver) {
    LoadResult result;
    auto encoded = codegen::x86_64::encode_image(module, abi);
    if (!encoded.ok()) {
        result.diagnostics = std::move(encoded.diagnostics);
        return result;
    }

    if (!encoded.image.externals.empty()) {
        const codegen::x86_64::ExternalResolver combined = [&](std::string_view name) -> std::optional<std::uintptr_t> {
            if (const auto builtin = builtin_symbol(name)) return builtin;
            return resolver ? resolver(name) : std::nullopt;
        };
        result.diagnostics = codegen::x86_64::resolve_externals(encoded.image, combined);
        if (!result.diagnostics.empty()) return result;
    }

    if (!encoded.image.external_globals.empty()) {
        if (!global_resolver) {
            error(result.diagnostics, "JIT module has unresolved external globals and no resolver");
            return result;
        }
        for (const auto& relocation : encoded.image.external_globals) {
            const auto address = global_resolver(relocation.symbol);
            if (!address) {
                error(result.diagnostics, "unresolved external global @" + relocation.symbol);
                continue;
            }
            const auto bits = static_cast<std::uint64_t>(*address);
            for (unsigned shift = 0; shift < 64; shift += 8)
                encoded.image.code.at(relocation.address_offset + shift / 8U) =
                    static_cast<std::byte>(static_cast<std::uint8_t>(bits >> shift));
        }
        if (!result.diagnostics.empty()) return result;
    }

    if (encoded.image.code.empty()) {
        error(result.diagnostics, "cannot load an empty JIT image");
        return result;
    }

    auto engine = std::unique_ptr<Engine>(new Engine());
    engine->code_size_ = encoded.image.code.size();
    engine->entries_ = std::move(encoded.image.entries);
    engine->globals_ = std::move(encoded.image.globals);

#if defined(_WIN32)
    SYSTEM_INFO info{};
    ::GetSystemInfo(&info);
    const auto page = static_cast<std::size_t>(info.dwPageSize);
#elif defined(__unix__) || defined(__APPLE__)
    const auto page_value = ::sysconf(_SC_PAGESIZE);
    if (page_value <= 0) {
        error(result.diagnostics, "could not determine JIT page size");
        return result;
    }
    const auto page = static_cast<std::size_t>(page_value);
#else
    error(result.diagnostics, "JIT executable memory is unsupported on this platform");
    return result;
#endif

    engine->read_only_data_offset_ = align_up(engine->code_size_, page);
    engine->writable_data_offset_ = engine->read_only_data_offset_ + align_up(encoded.image.read_only_data.size(), page);
    engine->allocation_size_ = engine->writable_data_offset_ + align_up(encoded.image.writable_data.size(), page);
    if (engine->allocation_size_ == 0) engine->allocation_size_ = page;

#if defined(_WIN32)
    engine->memory_ = ::VirtualAlloc(nullptr, engine->allocation_size_, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    if (!engine->memory_) {
        error(result.diagnostics, "VirtualAlloc failed for JIT image");
        return result;
    }
#elif defined(__unix__) || defined(__APPLE__)
    engine->memory_ = ::mmap(nullptr, engine->allocation_size_, PROT_READ | PROT_WRITE,
                             MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (engine->memory_ == MAP_FAILED) {
        engine->memory_ = nullptr;
        error(result.diagnostics, "mmap failed for JIT image");
        return result;
    }
#endif

    for (const auto& relocation : encoded.image.global_relocations) {
        const auto base = relocation.section == codegen::x86_64::DataSection::read_only
            ? engine->read_only_data_offset_ : engine->writable_data_offset_;
        const auto bits = reinterpret_cast<std::uintptr_t>(
            static_cast<std::byte*>(engine->memory_) + base + relocation.data_offset);
        for (unsigned shift = 0; shift < 64; shift += 8)
            encoded.image.code.at(relocation.address_offset + shift / 8U) =
                static_cast<std::byte>(static_cast<std::uint8_t>(bits >> shift));
    }

    std::memcpy(engine->memory_, encoded.image.code.data(), engine->code_size_);
    if (!encoded.image.read_only_data.empty()) {
        std::memcpy(static_cast<std::byte*>(engine->memory_) + engine->read_only_data_offset_,
                    encoded.image.read_only_data.data(), encoded.image.read_only_data.size());
    }

    if (!encoded.image.writable_data.empty()) {
        std::memcpy(static_cast<std::byte*>(engine->memory_) + engine->writable_data_offset_,
                    encoded.image.writable_data.data(), encoded.image.writable_data.size());
    }

#if defined(_WIN32)
    DWORD old_protection{};
    if (!::VirtualProtect(engine->memory_, engine->read_only_data_offset_, PAGE_EXECUTE_READ, &old_protection)) {
        error(result.diagnostics, "VirtualProtect failed for JIT code");
        return result;
    }

    if (!encoded.image.read_only_data.empty() &&
        !::VirtualProtect(static_cast<std::byte*>(engine->memory_) + engine->read_only_data_offset_,
                          align_up(encoded.image.read_only_data.size(), page), PAGE_READONLY, &old_protection)) {
        error(result.diagnostics, "VirtualProtect failed for JIT read-only data");
        return result;
    }
    ::FlushInstructionCache(::GetCurrentProcess(), engine->memory_, engine->code_size_);
#elif defined(__unix__) || defined(__APPLE__)
    if (::mprotect(engine->memory_, engine->read_only_data_offset_, PROT_READ | PROT_EXEC) != 0) {
        error(result.diagnostics, "mprotect failed for JIT code");
        return result;
    }

    if (!encoded.image.read_only_data.empty() &&
        ::mprotect(static_cast<std::byte*>(engine->memory_) + engine->read_only_data_offset_,
                   align_up(encoded.image.read_only_data.size(), page), PROT_READ) != 0) {
        error(result.diagnostics, "mprotect failed for JIT read-only data");
        return result;
    }
    auto* begin = static_cast<char*>(engine->memory_);
    __builtin___clear_cache(begin, begin + engine->code_size_);
#endif

    result.engine = std::move(engine);
    return result;
}

} // namespace forge::jit
