// Copyright 2026 Mario Vinciguerra
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "forge/diagnostics/diagnostic.hpp"
#include "forge/ir/artifact_cache.hpp"
#include "forge/machine/module.hpp"
#include "forge/object/incremental.hpp"

namespace forge::object {

struct NativeLinkOptions {
    std::filesystem::path linker{"cc"};
    std::string toolchain_identity;
    std::vector<std::string> arguments;
    std::vector<std::filesystem::path> library_paths;
    std::vector<std::string> libraries;
};

struct NativeLinkResult {
    Diagnostics diagnostics;
    std::filesystem::path output_path;
    std::string cache_key;
    std::string command;
    bool cache_hit{};
    bool linker_invoked{};
    std::size_t object_bytes{};
    std::size_t binary_bytes{};
    [[nodiscard]] bool ok() const noexcept { return diagnostics.empty(); }
};

[[nodiscard]] std::string build_native_link_cache_key(
    const ir::IncrementalBuildPlan& plan,
    NativeObjectFormat format,
    codegen::x86_64::Abi abi,
    const NativeLinkOptions& options = {});

[[nodiscard]] NativeLinkResult link_cached_native_executable(
    const ir::IncrementalBuildPlan& plan,
    ir::ArtifactCache& cache,
    std::span<const machine::Global> globals,
    NativeObjectFormat format,
    codegen::x86_64::Abi abi,
    const std::filesystem::path& output_path,
    const NativeLinkOptions& options = {});

struct NativeLibraryLinkResult {
    Diagnostics diagnostics;
    std::filesystem::path output_path;
    std::string command;
    std::size_t input_count{};
    std::size_t output_bytes{};
    [[nodiscard]] bool ok() const noexcept { return diagnostics.empty(); }
};

[[nodiscard]] NativeLibraryLinkResult link_native_shared_library(
    std::span<const std::filesystem::path> object_paths,
    const std::filesystem::path& output_path,
    const NativeLinkOptions& options = {});

} // namespace forge::object
