// Copyright 2026 Mario Vinciguerra
// SPDX-License-Identifier: Apache-2.0

#include "forge/object/incremental.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
#include <set>
#include <stdexcept>
#include <string_view>
#include <type_traits>

#include "forge/object/coff.hpp"

namespace forge::object {
namespace {

constexpr std::array<std::uint8_t, 8> artifact_magic{'F','R','G','N','A','T','1','\0'};

void add_error(Diagnostics& diagnostics, std::string message) {
    diagnostics.push_back({DiagnosticSeverity::error, std::move(message)});
}

class Writer {
public:
    template <typename T> void integer(T value) {
        using U = std::make_unsigned_t<T>;
        const auto bits = static_cast<U>(value);
        for (std::size_t index = 0; index < sizeof(T); ++index)
            bytes_.push_back(static_cast<std::uint8_t>(bits >> (index * 8U)));
    }
    void string(std::string_view value) {
        integer<std::uint64_t>(value.size());
        bytes_.insert(bytes_.end(), value.begin(), value.end());
    }
    void raw(std::span<const std::byte> bytes) {
        for (const auto value : bytes) bytes_.push_back(std::to_integer<std::uint8_t>(value));
    }
    [[nodiscard]] std::vector<std::uint8_t> take() && { return std::move(bytes_); }
private:
    std::vector<std::uint8_t> bytes_;
};

class Reader {
public:
    explicit Reader(std::span<const std::uint8_t> bytes) : bytes_(bytes) {}
    template <typename T> bool integer(T& value) {
        if (remaining() < sizeof(T)) return false;
        using U = std::make_unsigned_t<T>;
        U bits{};
        for (std::size_t index = 0; index < sizeof(T); ++index)
            bits |= static_cast<U>(bytes_[offset_++]) << (index * 8U);
        value = static_cast<T>(bits);
        return true;
    }
    bool string(std::string& value) {
        std::uint64_t size{};
        if (!integer(size) || size > remaining()) return false;
        value.assign(reinterpret_cast<const char*>(bytes_.data() + offset_), static_cast<std::size_t>(size));
        offset_ += static_cast<std::size_t>(size);
        return true;
    }
    bool code(std::vector<std::byte>& value) {
        std::uint64_t size{};
        if (!integer(size) || size > remaining()) return false;
        value.resize(static_cast<std::size_t>(size));
        for (auto& byte : value) byte = static_cast<std::byte>(bytes_[offset_++]);
        return true;
    }
    [[nodiscard]] std::size_t remaining() const { return bytes_.size() - offset_; }
private:
    std::span<const std::uint8_t> bytes_;
    std::size_t offset_{};
};

template <typename Fixup, typename OffsetMember>
void write_fixups(Writer& writer, const std::vector<Fixup>& fixups, OffsetMember member) {
    writer.integer<std::uint64_t>(fixups.size());
    for (const auto& fixup : fixups) {
        writer.integer<std::uint64_t>(fixup.*member);
        writer.string(fixup.target);
    }
}

template <typename Fixup, typename OffsetMember>
bool read_fixups(Reader& reader, std::vector<Fixup>& fixups, OffsetMember member) {
    std::uint64_t count{};
    if (!reader.integer(count) || count > 1'000'000U) return false;
    fixups.resize(static_cast<std::size_t>(count));
    for (auto& fixup : fixups) {
        std::uint64_t offset{};
        if (!reader.integer(offset) || !reader.string(fixup.target)) return false;
        fixup.*member = static_cast<std::size_t>(offset);
    }
    return true;
}

std::vector<std::uint8_t> serialize(const codegen::x86_64::EncodedFunction& function,
                                    codegen::x86_64::Abi abi) {
    Writer writer;
    for (const auto byte : artifact_magic) writer.integer(byte);
    writer.integer<std::uint32_t>(2);
    writer.integer<std::uint8_t>(static_cast<std::uint8_t>(abi));
    writer.string(function.name);
    writer.integer<std::uint64_t>(function.code.size());
    writer.raw(function.code);
    write_fixups(writer, function.calls, &codegen::x86_64::CallFixup::displacement_offset);
    write_fixups(writer, function.addresses, &codegen::x86_64::AddressFixup::displacement_offset);
    write_fixups(writer, function.global_addresses, &codegen::x86_64::GlobalAddressFixup::address_offset);
    write_fixups(writer, function.tls_addresses, &codegen::x86_64::TlsAddressFixup::address_offset);
    return std::move(writer).take();
}

bool deserialize(std::span<const std::uint8_t> bytes,
                 codegen::x86_64::EncodedFunction& function,
                 codegen::x86_64::Abi& abi,
                 Diagnostics& diagnostics) {
    Reader reader(bytes);
    for (const auto expected : artifact_magic) {
        std::uint8_t actual{};
        if (!reader.integer(actual) || actual != expected) {
            add_error(diagnostics, "invalid native function artifact magic");
            return false;
        }
    }
    std::uint32_t version{};
    std::uint8_t abi_value{};
    if (!reader.integer(version) || (version != 1 && version != 2) || !reader.integer(abi_value) || abi_value > 1) {
        add_error(diagnostics, "unsupported native function artifact version or ABI");
        return false;
    }
    abi = static_cast<codegen::x86_64::Abi>(abi_value);
    if (!reader.string(function.name) || !reader.code(function.code)
        || !read_fixups(reader, function.calls, &codegen::x86_64::CallFixup::displacement_offset)
        || !read_fixups(reader, function.addresses, &codegen::x86_64::AddressFixup::displacement_offset)
        || !read_fixups(reader, function.global_addresses, &codegen::x86_64::GlobalAddressFixup::address_offset)
        || (version >= 2 && !read_fixups(reader, function.tls_addresses, &codegen::x86_64::TlsAddressFixup::address_offset))
        || reader.remaining() != 0) {
        add_error(diagnostics, "malformed native function artifact");
        return false;
    }
    return true;
}

} // namespace

NativeFunctionArtifactResult compile_native_function_artifact(const machine::Function& function,
                                                               codegen::x86_64::Abi abi) {
    NativeFunctionArtifactResult result;
    machine::Module module;
    module.name = "incremental-function";
    module.functions.push_back(function);
    std::set<std::string> globals;
    std::set<std::string> tls_globals;
    for (const auto& block : function.blocks) {
        for (const auto& instruction : block.instructions) {
            if (instruction.opcode == machine::Opcode::load_global_address && !instruction.symbol.empty())
                globals.insert(instruction.symbol);
            if (instruction.opcode == machine::Opcode::load_tls_address && !instruction.symbol.empty())
                tls_globals.insert(instruction.symbol);
        }
    }

    for (const auto& name : globals)
        module.globals.push_back({name, 0, 1, false, true, false, false, {}});
    for (const auto& name : tls_globals)
        module.globals.push_back({name, 0, 1, false, true, true, false, {}});
    auto encoded = codegen::x86_64::encode_image(module, abi);
    if (!encoded.ok()) {
        result.diagnostics = std::move(encoded.diagnostics);
        return result;
    }

    if (encoded.functions.size() != 1) {
        add_error(result.diagnostics, "native function compilation produced an unexpected function count");
        return result;
    }
    result.bytes = serialize(encoded.functions.front(), abi);
    return result;
}

IncrementalObjectResult assemble_native_object_artifacts(std::span<const ir::FunctionArtifact> artifacts,
                                                          std::span<const machine::Global> globals,
                                                          NativeObjectFormat format,
                                                          codegen::x86_64::Abi abi) {
    IncrementalObjectResult result;
    std::vector<codegen::x86_64::EncodedFunction> functions;
    functions.reserve(artifacts.size());
    for (const auto& artifact : artifacts) {
        codegen::x86_64::EncodedFunction function;
        codegen::x86_64::Abi artifact_abi{};
        if (!deserialize(artifact.bytes, function, artifact_abi, result.diagnostics)) return result;
        if (artifact_abi != abi) {
            add_error(result.diagnostics, "native function artifact ABI mismatch for @" + function.name);
            return result;
        }
        if (!artifact.name.empty() && artifact.name != function.name) {
            add_error(result.diagnostics, "native function artifact name mismatch for @" + artifact.name);
            return result;
        }
        result.encoded_function_bytes += function.code.size();
        functions.push_back(std::move(function));
    }

    // Preserve the caller's function order. The monolithic object path emits
    // machine functions in module order, and incremental callers that require a
    // canonical lexical order already provide artifacts in that order. Keeping
    // input order here makes cached and monolithic objects byte-identical for
    // frontends such as Raz without weakening deterministic cache keys.
    auto image = codegen::x86_64::assemble_image(std::move(functions), globals);
    if (!image.ok()) {
        result.diagnostics = std::move(image.diagnostics);
        return result;
    }
    result.function_count = image.functions.size();
    if (format == NativeObjectFormat::elf64) {
        if (abi != codegen::x86_64::Abi::system_v) {
            add_error(result.diagnostics, "incremental ELF assembly requires the System V ABI");
            return result;
        }
        auto object = emit_elf64_x86_64(std::move(image.image));
        result.bytes = std::move(object.bytes);
        result.diagnostics = std::move(object.diagnostics);
        result.stats = object.stats;
    } else {
        if (abi != codegen::x86_64::Abi::windows) {
            add_error(result.diagnostics, "incremental COFF assembly requires the Windows x64 ABI");
            return result;
        }
        auto object = emit_coff_x86_64(std::move(image.image));
        result.bytes = std::move(object.bytes);
        result.diagnostics = std::move(object.diagnostics);
        result.stats = object.stats;
    }
    return result;
}

IncrementalObjectResult assemble_cached_native_object(const ir::IncrementalBuildPlan& plan,
                                                       const ir::ArtifactCache& cache,
                                                       std::span<const machine::Global> globals,
                                                       NativeObjectFormat format,
                                                       codegen::x86_64::Abi abi) {
    std::vector<ir::FunctionArtifact> artifacts;
    for (const auto& decision : plan.functions) {
        if (decision.action == ir::BuildAction::remove || decision.semantic_cache_key.empty()) continue;
        if (!cache.contains(decision.semantic_cache_key)) {
            IncrementalObjectResult result;
            add_error(result.diagnostics, "missing native function artifact for @" + decision.name);
            return result;
        }
        artifacts.push_back({decision.name, cache.load(decision.semantic_cache_key)});
    }

    return assemble_native_object_artifacts(artifacts, globals, format, abi);
}

} // namespace forge::object
