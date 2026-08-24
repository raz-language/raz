// Copyright 2026 Mario Vinciguerra
// SPDX-License-Identifier: Apache-2.0

#include "forge/jit/engine.hpp"

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstring>
#include <cstdlib>
#include <limits>
#include <new>
#include <unordered_map>
#include <unordered_set>

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

struct JitTlsState {
    std::uint64_t id{};
    std::vector<std::byte> initializer;
    std::size_t alignment{alignof(std::max_align_t)};
    codegen::aarch64::ExternalResolver external_resolver;
};

struct JitTlsDescriptor {
    JitTlsState* state{};
    std::size_t data_offset{};
    bool external{};
    std::string symbol;
};

namespace {
[[maybe_unused]] std::atomic<std::uint64_t> next_tls_state_id{1U};

struct AlignedDelete {
    std::size_t alignment{alignof(std::max_align_t)};
    void operator()(std::byte* pointer) const noexcept {
        if (pointer) ::operator delete(pointer, std::align_val_t(alignment));
    }
};

struct ThreadTlsBlock {
    std::unique_ptr<std::byte, AlignedDelete> bytes;
    std::size_t size{};
};

thread_local std::unordered_map<std::uint64_t, ThreadTlsBlock> jit_tls_blocks;

void* forge_aarch64_jit_tls_address(const JitTlsDescriptor* descriptor) noexcept {
    if (!descriptor || !descriptor->state) std::abort();
    auto& state = *descriptor->state;
    if (descriptor->external) {
        if (!state.external_resolver) std::abort();
        const auto address = state.external_resolver(descriptor->symbol);
        if (!address || *address == 0U) std::abort();
        return reinterpret_cast<void*>(*address);
    }

    try {
        auto found = jit_tls_blocks.find(state.id);
        if (found == jit_tls_blocks.end()) {
            const auto bytes = std::max<std::size_t>(state.initializer.size(), 1U);
            const auto alignment = std::max<std::size_t>(state.alignment, alignof(std::max_align_t));
            auto* raw = static_cast<std::byte*>(::operator new(bytes, std::align_val_t(alignment)));
            std::memset(raw, 0, bytes);
            if (!state.initializer.empty()) std::memcpy(raw, state.initializer.data(), state.initializer.size());
            ThreadTlsBlock block;
            block.bytes = std::unique_ptr<std::byte, AlignedDelete>(raw, AlignedDelete{alignment});
            block.size = bytes;
            found = jit_tls_blocks.emplace(state.id, std::move(block)).first;
        }
        if (descriptor->data_offset > found->second.size) std::abort();
        return found->second.bytes.get() + descriptor->data_offset;
    } catch (...) {
        std::abort();
    }
}

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

#if defined(__aarch64__) || defined(_M_ARM64)
void append_aarch64_word(std::vector<std::byte>& code, std::uint32_t word) {
    for (unsigned index = 0; index < 4U; ++index)
        code.push_back(static_cast<std::byte>(static_cast<std::uint8_t>(word >> (index * 8U))));
}

void append_aarch64_veneer(std::vector<std::byte>& code, std::uintptr_t address) {
    const auto bits = static_cast<std::uint64_t>(address);
    const auto half = [&](unsigned shift) {
        return static_cast<std::uint32_t>((bits >> shift) & 0xFFFFU);
    };
    // x16 is IP0 in AAPCS64 and is reserved for linker/JIT veneers.
    append_aarch64_word(code, 0xD2800000U | (half(0U) << 5U) | 16U);  // movz x16, #imm
    append_aarch64_word(code, 0xF2A00000U | (half(16U) << 5U) | 16U); // movk x16, #imm, lsl #16
    append_aarch64_word(code, 0xF2C00000U | (half(32U) << 5U) | 16U); // movk x16, #imm, lsl #32
    append_aarch64_word(code, 0xF2E00000U | (half(48U) << 5U) | 16U); // movk x16, #imm, lsl #48
    append_aarch64_word(code, 0xD61F0200U);                            // br x16
}
#endif
}

Engine::~Engine() {
    if (tls_state_) jit_tls_blocks.erase(tls_state_->id);
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
        const auto base = entry.read_only ? read_only_data_offset_ : writable_data_offset_;
        return static_cast<std::byte*>(memory_) + base + entry.data_offset;
    }
    for (const auto& descriptor : tls_descriptors_) {
        if (!descriptor || descriptor->external || descriptor->symbol != name) continue;
        return forge_aarch64_jit_tls_address(descriptor.get());
    }
    return nullptr;
}

bool Engine::is_global_read_only(std::string_view name) const noexcept {
    for (const auto& entry : globals_) {
        if (entry.name == name) return entry.read_only;
    }
    return false;
}

bool Engine::is_global_thread_local(std::string_view name) const noexcept {
    for (const auto& descriptor : tls_descriptors_) {
        if (descriptor && descriptor->symbol == name) return true;
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
    engine->globals_.reserve(encoded.image.globals.size());
    for (const auto& global : encoded.image.globals) {
        if (global.section == codegen::x86_64::DataSection::tls) continue;
        engine->globals_.push_back({global.name, global.data_offset,
                                    global.section == codegen::x86_64::DataSection::read_only});
    }

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

LoadResult load(const machine::Module& module, codegen::aarch64::Abi abi,
                const codegen::aarch64::ExternalResolver& resolver,
                const codegen::aarch64::ExternalResolver& global_resolver,
                const codegen::aarch64::ExternalResolver& tls_resolver) {
    LoadResult result;
    auto encoded = codegen::aarch64::encode_image(module, abi);
    if (!encoded.ok()) {
        result.diagnostics = std::move(encoded.diagnostics);
        return result;
    }
    if (encoded.image.code.empty()) {
        error(result.diagnostics, "cannot load an empty AArch64 JIT image");
        return result;
    }
    if (!encoded.image.external_globals.empty()) {
        result.diagnostics = codegen::aarch64::materialize_jit_external_globals(encoded.image, global_resolver);
        if (!result.diagnostics.empty()) return result;
    }

#if !(defined(__aarch64__) || defined(_M_ARM64))
    (void)resolver;
    (void)global_resolver;
    (void)tls_resolver;
    error(result.diagnostics, "AArch64 JIT loading requires an AArch64 host");
    return result;
#else
    auto engine = std::unique_ptr<Engine>(new Engine());
    if (!encoded.image.external_tls.empty() && !tls_resolver) {
        error(result.diagnostics, "AArch64 JIT module has external TLS and no thread-local resolver");
        return result;
    }
    if (!encoded.image.thread_local_data.empty() || !encoded.image.external_tls.empty()) {
        engine->tls_state_ = std::make_unique<JitTlsState>();
        auto& state = *engine->tls_state_;
        state.id = next_tls_state_id.fetch_add(1U, std::memory_order_relaxed);
        if (state.id == 0U) {
            error(result.diagnostics, "AArch64 JIT TLS state identifier space was exhausted");
            return result;
        }
        state.initializer = encoded.image.thread_local_data;
        state.external_resolver = tls_resolver;
        for (const auto& global : module.globals) {
            if (!global.is_thread_local || global.is_external) continue;
            state.alignment = std::max<std::size_t>(state.alignment, std::max<std::uint32_t>(1U, global.alignment));
        }
        if ((state.alignment & (state.alignment - 1U)) != 0U) {
            error(result.diagnostics, "AArch64 JIT TLS alignment is not a power of two");
            return result;
        }

        std::unordered_map<std::string, JitTlsDescriptor*> descriptors;
        for (const auto& global : encoded.image.globals) {
            if (global.section != codegen::aarch64::DataSection::tls) continue;
            auto descriptor = std::make_unique<JitTlsDescriptor>();
            descriptor->state = &state;
            descriptor->data_offset = global.data_offset;
            descriptor->symbol = global.name;
            descriptors.emplace(descriptor->symbol, descriptor.get());
            engine->tls_descriptors_.push_back(std::move(descriptor));
        }
        for (const auto& symbol : encoded.image.external_tls) {
            auto descriptor = std::make_unique<JitTlsDescriptor>();
            descriptor->state = &state;
            descriptor->external = true;
            descriptor->symbol = symbol;
            descriptors.emplace(descriptor->symbol, descriptor.get());
            engine->tls_descriptors_.push_back(std::move(descriptor));
        }
        result.diagnostics = codegen::aarch64::materialize_jit_tls(
            encoded.image,
            [&](std::string_view name) -> std::optional<std::uintptr_t> {
                const auto found = descriptors.find(std::string(name));
                if (found == descriptors.end()) return std::nullopt;
                return reinterpret_cast<std::uintptr_t>(found->second);
            },
            reinterpret_cast<std::uintptr_t>(&forge_aarch64_jit_tls_address));
        if (!result.diagnostics.empty()) return result;
    }

    std::unordered_set<std::string> defined;
    for (const auto& [name, offset] : encoded.image.entries) {
        (void)offset;
        defined.insert(name);
    }
    for (const auto& global : encoded.image.globals) defined.insert(global.name);
    std::unordered_set<std::string> external_globals(
        encoded.image.external_globals.begin(), encoded.image.external_globals.end());

    // Keep external branch targets within BL/ADRP reach by routing every host
    // function through a tiny absolute-address veneer inside the JIT code
    // image. Function pointers intentionally resolve to the same veneer.
    std::unordered_map<std::string, std::size_t> veneers;
    const codegen::aarch64::ExternalResolver combined = [&](std::string_view name) -> std::optional<std::uintptr_t> {
        if (const auto builtin = builtin_symbol(name)) return builtin;
        return resolver ? resolver(name) : std::nullopt;
    };
    for (const auto& relocation : encoded.image.relocations) {
        if (defined.contains(relocation.symbol) || external_globals.contains(relocation.symbol)) continue;
        if (relocation.kind == codegen::aarch64::RelocationKind::tlsie_adr_gottprel_page21 ||
            relocation.kind == codegen::aarch64::RelocationKind::tlsie_ld64_gottprel_lo12_nc ||
            relocation.kind == codegen::aarch64::RelocationKind::tlvp_load_page21 ||
            relocation.kind == codegen::aarch64::RelocationKind::tlvp_load_pageoff12) continue;
        if (veneers.contains(relocation.symbol)) continue;
        const auto address = combined(relocation.symbol);
        if (!address) {
            error(result.diagnostics, "unresolved AArch64 JIT external @" + relocation.symbol);
            continue;
        }
        const auto offset = encoded.image.code.size();
        veneers.emplace(relocation.symbol, offset);
        append_aarch64_veneer(encoded.image.code, *address);
    }
    if (!result.diagnostics.empty()) return result;

    engine->code_size_ = encoded.image.code.size();
    engine->entries_ = encoded.image.entries;
    engine->globals_.reserve(encoded.image.globals.size());
    for (const auto& global : encoded.image.globals) {
        if (global.section == codegen::aarch64::DataSection::tls) continue;
        engine->globals_.push_back({global.name, global.data_offset,
                                    global.section == codegen::aarch64::DataSection::read_only});
    }

#if defined(_WIN32)
    SYSTEM_INFO info{};
    ::GetSystemInfo(&info);
    const auto page = static_cast<std::size_t>(info.dwPageSize);
#elif defined(__unix__) || defined(__APPLE__)
    const auto page_value = ::sysconf(_SC_PAGESIZE);
    if (page_value <= 0) {
        error(result.diagnostics, "could not determine AArch64 JIT page size");
        return result;
    }
    const auto page = static_cast<std::size_t>(page_value);
#else
    error(result.diagnostics, "AArch64 JIT executable memory is unsupported on this platform");
    return result;
#endif

    engine->read_only_data_offset_ = align_up(engine->code_size_, page);
    engine->writable_data_offset_ = engine->read_only_data_offset_ + align_up(encoded.image.read_only_data.size(), page);
    engine->allocation_size_ = engine->writable_data_offset_ + align_up(encoded.image.writable_data.size(), page);
    if (engine->allocation_size_ == 0U) engine->allocation_size_ = page;

#if defined(_WIN32)
    engine->memory_ = ::VirtualAlloc(nullptr, engine->allocation_size_, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    if (!engine->memory_) {
        error(result.diagnostics, "VirtualAlloc failed for AArch64 JIT image");
        return result;
    }
#elif defined(__unix__) || defined(__APPLE__)
    engine->memory_ = ::mmap(nullptr, engine->allocation_size_, PROT_READ | PROT_WRITE,
                             MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (engine->memory_ == MAP_FAILED) {
        engine->memory_ = nullptr;
        error(result.diagnostics, "mmap failed for AArch64 JIT image");
        return result;
    }
#endif

    const auto code_base = reinterpret_cast<std::uintptr_t>(engine->memory_);
    const auto rodata_base = code_base + engine->read_only_data_offset_;
    const auto data_base = code_base + engine->writable_data_offset_;
    const codegen::aarch64::ExternalResolver veneer_resolver = [&](std::string_view name) -> std::optional<std::uintptr_t> {
        const auto found = veneers.find(std::string(name));
        if (found == veneers.end()) return std::nullopt;
        return code_base + found->second;
    };
    result.diagnostics = codegen::aarch64::resolve_jit_relocations(
        encoded.image, code_base, rodata_base, data_base, veneer_resolver, global_resolver);
    if (!result.diagnostics.empty()) return result;

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
        error(result.diagnostics, "VirtualProtect failed for AArch64 JIT code");
        return result;
    }
    if (!encoded.image.read_only_data.empty() &&
        !::VirtualProtect(static_cast<std::byte*>(engine->memory_) + engine->read_only_data_offset_,
                          align_up(encoded.image.read_only_data.size(), page), PAGE_READONLY, &old_protection)) {
        error(result.diagnostics, "VirtualProtect failed for AArch64 JIT read-only data");
        return result;
    }
    ::FlushInstructionCache(::GetCurrentProcess(), engine->memory_, engine->code_size_);
#elif defined(__unix__) || defined(__APPLE__)
    if (::mprotect(engine->memory_, engine->read_only_data_offset_, PROT_READ | PROT_EXEC) != 0) {
        error(result.diagnostics, "mprotect failed for AArch64 JIT code");
        return result;
    }
    if (!encoded.image.read_only_data.empty() &&
        ::mprotect(static_cast<std::byte*>(engine->memory_) + engine->read_only_data_offset_,
                   align_up(encoded.image.read_only_data.size(), page), PROT_READ) != 0) {
        error(result.diagnostics, "mprotect failed for AArch64 JIT read-only data");
        return result;
    }
    auto* begin = static_cast<char*>(engine->memory_);
    __builtin___clear_cache(begin, begin + engine->code_size_);
#endif

    result.engine = std::move(engine);
    return result;
#endif
}

} // namespace forge::jit
