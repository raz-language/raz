// Copyright 2026 Mario Vinciguerra
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdint>
#include <span>
#include <vector>

#include "forge/codegen/x86_64/encoder.hpp"
#include "forge/diagnostics/diagnostic.hpp"
#include "forge/ir/artifact_cache.hpp"
#include "forge/ir/dependency_build.hpp"
#include "forge/machine/module.hpp"
#include "forge/object/elf.hpp"

namespace forge::object {

enum class NativeObjectFormat : std::uint8_t { elf64, coff };

struct NativeFunctionArtifactResult {
    std::vector<std::uint8_t> bytes;
    Diagnostics diagnostics;
    [[nodiscard]] bool ok() const noexcept { return diagnostics.empty(); }
};

struct IncrementalObjectResult {
    std::vector<std::byte> bytes;
    Diagnostics diagnostics;
    ObjectEmissionStats stats;
    std::size_t function_count{};
    std::size_t encoded_function_bytes{};
    [[nodiscard]] bool ok() const noexcept { return diagnostics.empty(); }
};

[[nodiscard]] NativeFunctionArtifactResult compile_native_function_artifact(
    const machine::Function& function,
    codegen::x86_64::Abi abi);

[[nodiscard]] IncrementalObjectResult assemble_native_object_artifacts(
    std::span<const ir::FunctionArtifact> artifacts,
    std::span<const machine::Global> globals,
    NativeObjectFormat format,
    codegen::x86_64::Abi abi);

[[nodiscard]] IncrementalObjectResult assemble_cached_native_object(
    const ir::IncrementalBuildPlan& plan,
    const ir::ArtifactCache& cache,
    std::span<const machine::Global> globals,
    NativeObjectFormat format,
    codegen::x86_64::Abi abi);

} // namespace forge::object
