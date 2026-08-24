// Copyright 2026 Mario Vinciguerra
// SPDX-License-Identifier: Apache-2.0

#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <thread>

#include "forge/codegen/aarch64/encoder.hpp"
#include "forge/ir/parser.hpp"
#include "forge/ir/verifier.hpp"
#include "forge/jit/engine.hpp"
#include "forge/jit/invoke.hpp"
#include "forge/machine/lower.hpp"

namespace {

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

std::uint32_t word_at(const std::vector<std::byte>& bytes, std::size_t offset) {
    std::uint32_t value = 0U;
    for (unsigned index = 0; index < 4U; ++index)
        value |= static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(bytes.at(offset + index))) << (index * 8U);
    return value;
}

constexpr auto host_aarch64_abi() {
#if defined(__APPLE__)
    return forge::codegen::aarch64::Abi::darwin;
#else
    return forge::codegen::aarch64::Abi::aapcs64;
#endif
}

}

int main() {
    try {
        constexpr auto source = R"(module @aarch64_jit {
global @counter: i64 = 40
func @add_two(%value: i64) -> i64 {
entry:
  %two = const i64 2
  %sum = add i64 %value %two
  return %sum
}
func @entry() -> i64 {
entry:
  %address = global.address ptr @counter
  %value = load i64 %address
  %result = call i64 @add_two(%value)
  store i64 %result %address
  return %result
}
})";
        auto parsed = forge::ir::parse_module(source);
        require(parsed.ok(), "AArch64 JIT fixture did not parse");
        require(forge::ir::verify_module(*parsed.module).empty(), "AArch64 JIT fixture did not verify");
        auto lowered = forge::machine::lower_module(
            *parsed.module, {forge::machine::TargetArchitecture::aarch64});
        require(lowered.ok(), "AArch64 JIT fixture did not lower");

        auto image = forge::codegen::aarch64::encode_image(*lowered.module, forge::codegen::aarch64::Abi::aapcs64);
        require(image.ok(), "AArch64 JIT fixture did not encode");
        require(!image.image.relocations.empty(), "AArch64 JIT fixture emitted no relocations");

        constexpr std::uintptr_t code_base = 0x0000001000000000ULL;
        constexpr std::uintptr_t rodata_base = code_base + 0x10000ULL;
        constexpr std::uintptr_t data_base = code_base + 0x12234ULL;
        auto resolved = image.image;
        const auto diagnostics = forge::codegen::aarch64::resolve_jit_relocations(
            resolved, code_base, rodata_base, data_base);
        require(diagnostics.empty(), "AArch64 JIT internal relocation resolution failed");

        bool saw_call = false;
        bool saw_page = false;
        bool saw_low12 = false;
        for (const auto& relocation : image.image.relocations) {
            const auto before = word_at(image.image.code, relocation.offset);
            const auto after = word_at(resolved.code, relocation.offset);
            switch (relocation.kind) {
            case forge::codegen::aarch64::RelocationKind::call26:
                saw_call = true;
                require(after != before, "AArch64 JIT internal call relocation was not patched");
                break;
            case forge::codegen::aarch64::RelocationKind::adr_prel_pg_hi21:
                saw_page = true;
                require(after != before, "AArch64 JIT ADRP relocation was not patched");
                break;
            case forge::codegen::aarch64::RelocationKind::add_abs_lo12_nc:
                saw_low12 = true;
                require(((after >> 10U) & 0xFFFU) == (data_base & 0xFFFU),
                        "AArch64 JIT low-12 relocation encoded the wrong data address");
                break;
            default:
                break;
            }
        }
        require(saw_call && saw_page && saw_low12, "AArch64 JIT fixture did not cover call/global relocation forms");

        constexpr auto external_source = R"(module @aarch64_jit_external {
extern c func @host_value() -> i64
func @entry() -> i64 {
entry:
  %value = call i64 @host_value()
  return %value
}
})";
        auto external_parsed = forge::ir::parse_module(external_source);
        require(external_parsed.ok(), "AArch64 JIT external fixture did not parse");
        require(forge::ir::verify_module(*external_parsed.module).empty(), "AArch64 JIT external fixture did not verify");
        auto external_lowered = forge::machine::lower_module(
            *external_parsed.module, {forge::machine::TargetArchitecture::aarch64});
        require(external_lowered.ok(), "AArch64 JIT external fixture did not lower");
        auto external_image = forge::codegen::aarch64::encode_image(
            *external_lowered.module, forge::codegen::aarch64::Abi::aapcs64);
        require(external_image.ok(), "AArch64 JIT external fixture did not encode");
        const auto external_diagnostics = forge::codegen::aarch64::resolve_jit_relocations(
            external_image.image, code_base, rodata_base, data_base,
            [&](std::string_view name) -> std::optional<std::uintptr_t> {
                if (name == "host_value") return code_base + 0x400ULL;
                return std::nullopt;
            });
        require(external_diagnostics.empty(), "AArch64 JIT in-range external call did not resolve");

        auto unresolved_image = forge::codegen::aarch64::encode_image(
            *external_lowered.module, forge::codegen::aarch64::Abi::aapcs64);
        require(unresolved_image.ok(), "AArch64 JIT unresolved fixture did not encode");
        const auto unresolved = forge::codegen::aarch64::resolve_jit_relocations(
            unresolved_image.image, code_base, rodata_base, data_base);
        require(!unresolved.empty(), "AArch64 JIT accepted an unresolved external symbol");

        auto overflowing_image = forge::codegen::aarch64::encode_image(
            *external_lowered.module, forge::codegen::aarch64::Abi::aapcs64);
        require(overflowing_image.ok() && !overflowing_image.image.relocations.empty(),
                "AArch64 JIT overflow fixture did not encode a relocation");
        overflowing_image.image.relocations.front().addend = 8;
        const auto overflowing = forge::codegen::aarch64::resolve_jit_relocations(
            overflowing_image.image, code_base, rodata_base, data_base,
            [&](std::string_view) -> std::optional<std::uintptr_t> {
                return std::numeric_limits<std::uintptr_t>::max() - 3U;
            });
        require(!overflowing.empty(), "AArch64 JIT accepted a relocation whose target+addend overflows");

        constexpr auto external_global_source = R"(module @aarch64_jit_external_global {
extern global @host_counter: i64
func @entry() -> i64 {
entry:
  %address = global.address ptr @host_counter
  %value = load i64 %address
  return %value
}
})";
        auto external_global_parsed = forge::ir::parse_module(external_global_source);
        require(external_global_parsed.ok(), "AArch64 JIT external-global fixture did not parse");
        require(forge::ir::verify_module(*external_global_parsed.module).empty(),
                "AArch64 JIT external-global fixture did not verify");
        auto external_global_lowered = forge::machine::lower_module(
            *external_global_parsed.module, {forge::machine::TargetArchitecture::aarch64});
        require(external_global_lowered.ok(), "AArch64 JIT external-global fixture did not lower");
        auto external_global_image = forge::codegen::aarch64::encode_image(
            *external_global_lowered.module, forge::codegen::aarch64::Abi::aapcs64);
        require(external_global_image.ok(), "AArch64 JIT external-global fixture did not encode");

        // Deliberately place the host object far beyond ADRP range. The JIT
        // must materialize that absolute address in a nearby pointer slot and
        // turn the normal ADRP+ADD address sequence into ADRP+LDR.
        constexpr std::uintptr_t far_global = 0x00007FFF12345000ULL;
        const auto materialize_diagnostics = forge::codegen::aarch64::materialize_jit_external_globals(
            external_global_image.image,
            [&](std::string_view name) -> std::optional<std::uintptr_t> {
                if (name == "host_counter") return far_global;
                return std::nullopt;
            });
        require(materialize_diagnostics.empty(), "AArch64 JIT external-global pointer table did not materialize");
        require(external_global_image.image.jit_external_global_slots.size() == 1U,
                "AArch64 JIT did not create exactly one external-global pointer slot");
        const auto slot = external_global_image.image.jit_external_global_slots.front();
        require(slot.symbol == "host_counter", "AArch64 JIT external-global pointer slot lost its symbol");
        require(slot.data_offset + 8U <= external_global_image.image.read_only_data.size(),
                "AArch64 JIT external-global pointer slot exceeds read-only data");
        std::uint64_t slot_bits = 0U;
        for (unsigned index = 0U; index < 8U; ++index)
            slot_bits |= static_cast<std::uint64_t>(std::to_integer<std::uint8_t>(
                external_global_image.image.read_only_data.at(slot.data_offset + index))) << (index * 8U);
        require(slot_bits == far_global, "AArch64 JIT external-global pointer slot contains the wrong address");
        const auto materialized_rodata_size = external_global_image.image.read_only_data.size();
        const auto duplicate_materialize = forge::codegen::aarch64::materialize_jit_external_globals(
            external_global_image.image,
            [&](std::string_view) -> std::optional<std::uintptr_t> { return far_global; });
        require(!duplicate_materialize.empty(), "AArch64 JIT accepted duplicate external-global materialization");
        require(external_global_image.image.read_only_data.size() == materialized_rodata_size,
                "duplicate AArch64 JIT external-global materialization mutated read-only data");

        const auto far_global_diagnostics = forge::codegen::aarch64::resolve_jit_relocations(
            external_global_image.image, code_base, rodata_base, data_base);
        require(far_global_diagnostics.empty(), "AArch64 JIT long-range external global did not resolve through its slot");
        bool saw_external_page = false;
        bool saw_external_load = false;
        for (const auto& relocation : external_global_image.image.relocations) {
            if (relocation.symbol != "host_counter") continue;
            const auto word = word_at(external_global_image.image.code, relocation.offset);
            if (relocation.kind == forge::codegen::aarch64::RelocationKind::adr_prel_pg_hi21)
                saw_external_page = true;
            if (relocation.kind == forge::codegen::aarch64::RelocationKind::add_abs_lo12_nc) {
                saw_external_load = true;
                require((word & 0xFFC00000U) == 0xF9400000U,
                        "AArch64 JIT external-global low12 instruction was not rewritten to LDR");
            }
        }
        require(saw_external_page && saw_external_load,
                "AArch64 JIT external-global fixture did not exercise ADRP+LDR indirection");

        auto missing_global_image = forge::codegen::aarch64::encode_image(
            *external_global_lowered.module, forge::codegen::aarch64::Abi::aapcs64);
        require(missing_global_image.ok(), "AArch64 JIT missing-global fixture did not encode");
        const auto missing_global = forge::codegen::aarch64::materialize_jit_external_globals(
            missing_global_image.image, {});
        require(!missing_global.empty(), "AArch64 JIT accepted external globals without a global resolver");


        constexpr auto tls_source = R"(module @aarch64_jit_tls {
thread_local global @tls_counter: i64 = 9
extern thread_local global @host_tls: i64
func @tls_entry() -> i64 {
entry:
  %local_address = tls.address ptr @tls_counter
  %local = load i64 %local_address
  %host_address = tls.address ptr @host_tls
  %host = load i64 %host_address
  %sum = add i64 %local %host
  return %sum
}
})";
        auto tls_parsed = forge::ir::parse_module(tls_source);
        require(tls_parsed.ok(), "AArch64 JIT TLS fixture did not parse");
        require(forge::ir::verify_module(*tls_parsed.module).empty(), "AArch64 JIT TLS fixture did not verify");
        auto tls_lowered = forge::machine::lower_module(
            *tls_parsed.module, {forge::machine::TargetArchitecture::aarch64});
        require(tls_lowered.ok(), "AArch64 JIT TLS fixture did not lower");
        auto tls_image = forge::codegen::aarch64::encode_image(
            *tls_lowered.module, forge::codegen::aarch64::Abi::aapcs64);
        require(tls_image.ok(), "AArch64 JIT TLS fixture did not encode");
        std::size_t tls_relocation_count = 0U;
        std::size_t first_tls_site = 0U;
        bool have_first_tls_site = false;
        for (const auto& relocation : tls_image.image.relocations) {
            const bool tls_relocation =
                relocation.kind == forge::codegen::aarch64::RelocationKind::tlsie_adr_gottprel_page21 ||
                relocation.kind == forge::codegen::aarch64::RelocationKind::tlsie_ld64_gottprel_lo12_nc;
            if (!tls_relocation) continue;
            ++tls_relocation_count;
            if (!have_first_tls_site &&
                relocation.kind == forge::codegen::aarch64::RelocationKind::tlsie_adr_gottprel_page21) {
                first_tls_site = relocation.offset;
                have_first_tls_site = true;
            }
        }
        require(tls_relocation_count == 4U && have_first_tls_site,
                "AArch64 JIT TLS fixture did not produce two native TLS relocation pairs");
        const auto original_tls_code_size = tls_image.image.code.size();
        const auto original_tls_image = tls_image.image;
        auto unmaterialized_tls_image = original_tls_image;
        const auto unmaterialized_tls = forge::codegen::aarch64::resolve_jit_relocations(
            unmaterialized_tls_image, code_base, rodata_base, data_base);
        require(!unmaterialized_tls.empty(),
                "AArch64 JIT generic relocation resolver accepted native TLS relocations without thunk materialization");
        const auto tls_materialized = forge::codegen::aarch64::materialize_jit_tls(
            tls_image.image,
            [&](std::string_view name) -> std::optional<std::uintptr_t> {
                if (name == "tls_counter") return 0x1111222233334444ULL;
                if (name == "host_tls") return 0x5555666677778888ULL;
                return std::nullopt;
            },
            0x9999AAAABBBBCCCCULL);
        require(tls_materialized.empty(), "AArch64 JIT TLS thunk materialization failed");
        require(tls_image.image.jit_tls_thunks.size() == 2U,
                "AArch64 JIT TLS did not create one thunk per referenced symbol");
        require(tls_image.image.code.size() == original_tls_code_size + 72U,
                "AArch64 JIT TLS thunk size is not deterministic");
        for (const auto& relocation : tls_image.image.relocations) {
            require(relocation.kind != forge::codegen::aarch64::RelocationKind::tlsie_adr_gottprel_page21 &&
                    relocation.kind != forge::codegen::aarch64::RelocationKind::tlsie_ld64_gottprel_lo12_nc &&
                    relocation.kind != forge::codegen::aarch64::RelocationKind::tlvp_load_page21 &&
                    relocation.kind != forge::codegen::aarch64::RelocationKind::tlvp_load_pageoff12,
                    "AArch64 JIT TLS materialization left native TLS relocations behind");
        }
        require((word_at(tls_image.image.code, first_tls_site) & 0xFC000000U) == 0x94000000U,
                "AArch64 JIT TLS site was not rewritten to BL thunk");
        require(word_at(tls_image.image.code, first_tls_site + 8U) == 0xD503201FU &&
                word_at(tls_image.image.code, first_tls_site + 12U) == 0xD503201FU,
                "AArch64 JIT TLS rewrite did not preserve the fixed four-instruction footprint");
        auto tls_resolved_image = tls_image.image;
        const auto tls_resolved = forge::codegen::aarch64::resolve_jit_relocations(
            tls_resolved_image, code_base, rodata_base, data_base);
        require(tls_resolved.empty(), "AArch64 JIT TLS image failed generic relocation resolution after thunk rewrite");
        const auto duplicate_tls = forge::codegen::aarch64::materialize_jit_tls(
            tls_image.image,
            [&](std::string_view) -> std::optional<std::uintptr_t> { return 0x1111222233334444ULL; },
            0x9999AAAABBBBCCCCULL);
        require(!duplicate_tls.empty(), "AArch64 JIT accepted duplicate TLS thunk materialization");

        auto missing_tls_image = original_tls_image;
        const auto missing_tls = forge::codegen::aarch64::materialize_jit_tls(
            missing_tls_image,
            [&](std::string_view name) -> std::optional<std::uintptr_t> {
                if (name == "tls_counter") return 0x1111222233334444ULL;
                return std::nullopt;
            },
            0x9999AAAABBBBCCCCULL);
        require(!missing_tls.empty(), "AArch64 JIT accepted a missing TLS descriptor");
        require(missing_tls_image.code == original_tls_image.code &&
                missing_tls_image.relocations.size() == original_tls_image.relocations.size() &&
                missing_tls_image.jit_tls_thunks.empty(),
                "failed AArch64 JIT TLS materialization mutated the image");

        auto darwin_tls_image = forge::codegen::aarch64::encode_image(
            *tls_lowered.module, forge::codegen::aarch64::Abi::darwin);
        require(darwin_tls_image.ok(), "Darwin arm64 JIT TLS fixture did not encode");
        const auto darwin_tls_materialized = forge::codegen::aarch64::materialize_jit_tls(
            darwin_tls_image.image,
            [&](std::string_view name) -> std::optional<std::uintptr_t> {
                if (name == "tls_counter") return 0x1111222233334444ULL;
                if (name == "host_tls") return 0x5555666677778888ULL;
                return std::nullopt;
            },
            0x9999AAAABBBBCCCCULL);
        require(darwin_tls_materialized.empty(), "Darwin arm64 JIT TLV rewrite failed");
        require(darwin_tls_image.image.jit_tls_thunks.size() == 2U,
                "Darwin arm64 JIT TLS did not create deterministic thunks");

#if defined(__aarch64__) || defined(_M_ARM64)
        thread_local std::uint64_t host_tls_value = 100U;
        auto tls_loaded = forge::jit::load(
            *tls_lowered.module, host_aarch64_abi(), {}, {},
            [&](std::string_view name) -> std::optional<std::uintptr_t> {
                if (name == "host_tls") return reinterpret_cast<std::uintptr_t>(&host_tls_value);
                return std::nullopt;
            });
        require(tls_loaded.ok(), "AArch64 host JIT failed to load TLS module");
        require(tls_loaded.engine->is_global_thread_local("tls_counter"),
                "AArch64 JIT did not expose internal TLS metadata");
        auto* main_tls_address = static_cast<std::uint64_t*>(tls_loaded.engine->lookup_global("tls_counter"));
        require(main_tls_address != nullptr && *main_tls_address == 9U,
                "AArch64 JIT internal TLS initializer is wrong on the loading thread");
        const auto tls_main = forge::jit::invoke_integer(tls_loaded.engine->lookup("tls_entry"), {});
        require(tls_main.ok() && tls_main.bits == 109U,
                "AArch64 JIT TLS invocation returned the wrong loading-thread value");
        std::uintptr_t worker_tls_address = 0U;
        std::uint64_t worker_result = 0U;
        std::thread worker([&] {
            host_tls_value = 200U;
            auto* worker_tls = static_cast<std::uint64_t*>(tls_loaded.engine->lookup_global("tls_counter"));
            worker_tls_address = reinterpret_cast<std::uintptr_t>(worker_tls);
            const auto invoked = forge::jit::invoke_integer(tls_loaded.engine->lookup("tls_entry"), {});
            if (invoked.ok()) worker_result = invoked.bits;
        });
        worker.join();
        require(worker_result == 209U, "AArch64 JIT TLS did not resolve against the worker thread");
        require(worker_tls_address != reinterpret_cast<std::uintptr_t>(main_tls_address),
                "AArch64 JIT internal TLS reused the same storage across host threads");
        require(*main_tls_address == 9U,
                "AArch64 JIT worker-thread TLS access mutated the loading thread's storage");

        auto loaded = forge::jit::load(*lowered.module, host_aarch64_abi());
        require(loaded.ok(), "AArch64 host JIT failed to load an executable module");
        require(loaded.engine->lookup("entry") != nullptr, "AArch64 host JIT lost the entry point");
        require(loaded.engine->lookup_global("counter") != nullptr, "AArch64 host JIT lost the writable global");
        const auto invocation = forge::jit::invoke_integer(loaded.engine->lookup("entry"), {});
        require(invocation.ok(), "AArch64 host JIT invocation failed");
        require(invocation.bits == 42U, "AArch64 host JIT returned the wrong value");
        require(*static_cast<std::uint64_t*>(loaded.engine->lookup_global("counter")) == 42U,
                "AArch64 host JIT did not preserve writable global state");
#else
        auto non_native = forge::jit::load(*lowered.module, forge::codegen::aarch64::Abi::aapcs64);
        require(!non_native.ok(), "non-AArch64 host unexpectedly accepted AArch64 JIT execution");
#endif

        std::cout << "Forge AArch64 JIT tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "test failure: " << error.what() << '\n';
        return 1;
    }
}
