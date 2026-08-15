// Copyright 2026 Mario Vinciguerra
// SPDX-License-Identifier: Apache-2.0

#include "forge/object/native_link.hpp"

#include <cstdlib>
#include <fstream>
#include <sstream>
#include <system_error>

#include "forge/ir/incremental.hpp"

namespace forge::object {
namespace {

void add_error(Diagnostics& diagnostics, std::string message) {
    diagnostics.emplace_back(DiagnosticSeverity::error, std::move(message));
}

std::string quote_argument(std::string_view value) {
#ifdef _WIN32
    std::string result{"\""};
    std::size_t slashes{};
    for (const char ch : value) {
        if (ch == '\\') {
            ++slashes;
            continue;
        }
        if (ch == '"') {
            result.append(slashes * 2U + 1U, '\\');
            result.push_back('"');
            slashes = 0;
            continue;
        }
        result.append(slashes, '\\');
        slashes = 0;
        result.push_back(ch);
    }
    result.append(slashes * 2U, '\\');
    result.push_back('"');
    return result;
#else
    std::string result{"'"};
    for (const char ch : value) {
        if (ch == '\'') result.append("'\\''");
        else result.push_back(ch);
    }
    result.push_back('\'');
    return result;
#endif
}

std::vector<std::uint8_t> read_file(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("failed to read linked binary: " + path.string());
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

void write_file_atomic(const std::filesystem::path& path, std::span<const std::uint8_t> bytes) {
    if (!path.parent_path().empty()) std::filesystem::create_directories(path.parent_path());
    const auto temporary = std::filesystem::path(path.string() + ".tmp");
    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        if (!output) throw std::runtime_error("failed to create output binary: " + temporary.string());
        output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        if (!output) throw std::runtime_error("failed to write output binary: " + temporary.string());
    }
    std::error_code error;
    std::filesystem::rename(temporary, path, error);
    if (error) {
        std::filesystem::remove(path, error);
        error.clear();
        std::filesystem::rename(temporary, path, error);
        if (error) {
            std::filesystem::remove(temporary);
            throw std::runtime_error("failed to commit output binary: " + path.string());
        }
    }
#ifndef _WIN32
    auto permissions = std::filesystem::status(path).permissions();
    permissions |= std::filesystem::perms::owner_exec | std::filesystem::perms::group_exec
                 | std::filesystem::perms::others_exec;
    std::filesystem::permissions(path, permissions, error);
    if (error) throw std::runtime_error("failed to mark linked binary executable: " + path.string());
#endif
}

std::vector<std::uint8_t> to_bytes(std::span<const std::byte> bytes) {
    std::vector<std::uint8_t> result;
    result.reserve(bytes.size());
    for (const auto byte : bytes) result.push_back(std::to_integer<std::uint8_t>(byte));
    return result;
}

} // namespace

std::string build_native_link_cache_key(const ir::IncrementalBuildPlan& plan,
                                        NativeObjectFormat format,
                                        codegen::x86_64::Abi abi,
                                        const NativeLinkOptions& options) {
    ir::Module key_module("native-link-cache-key");
    auto& metadata = key_module.metadata();
    metadata.push_back({"link.semantic", plan.current_semantic_fingerprint});
    metadata.push_back({"link.format", format == NativeObjectFormat::elf64 ? "elf64" : "coff"});
    metadata.push_back({"link.abi", abi == codegen::x86_64::Abi::system_v ? "system-v" : "windows-x64"});
    metadata.push_back({"link.driver", options.linker.generic_string()});
    metadata.push_back({"link.toolchain-identity", options.toolchain_identity});
    for (const auto& argument : options.arguments) metadata.push_back({"link.argument", argument});
    for (const auto& path : options.library_paths) metadata.push_back({"link.library-path", path.generic_string()});
    for (const auto& library : options.libraries) metadata.push_back({"link.library", library});
    return ir::build_cache_key(key_module, "forge-native-link-v1", "executable");
}

NativeLinkResult link_cached_native_executable(const ir::IncrementalBuildPlan& plan,
                                                ir::ArtifactCache& cache,
                                                std::span<const machine::Global> globals,
                                                NativeObjectFormat format,
                                                codegen::x86_64::Abi abi,
                                                const std::filesystem::path& output_path,
                                                const NativeLinkOptions& options) {
    NativeLinkResult result;
    result.output_path = output_path;
    if (output_path.empty()) {
        add_error(result.diagnostics, "native link output path is empty");
        return result;
    }
#ifndef _WIN32
    if (format != NativeObjectFormat::elf64 || abi != codegen::x86_64::Abi::system_v) {
        add_error(result.diagnostics, "native linking on this host requires ELF64 with the System V ABI");
        return result;
    }
#else
    if (format != NativeObjectFormat::coff || abi != codegen::x86_64::Abi::windows) {
        add_error(result.diagnostics, "native linking on this host requires COFF with the Windows x64 ABI");
        return result;
    }
#endif
    try {
        result.cache_key = build_native_link_cache_key(plan, format, abi, options);
        if (cache.contains(result.cache_key)) {
            const auto binary = cache.load(result.cache_key);
            write_file_atomic(output_path, binary);
            result.cache_hit = true;
            result.binary_bytes = binary.size();
            return result;
        }

        auto object = assemble_cached_native_object(plan, cache, globals, format, abi);
        if (!object.ok()) {
            result.diagnostics = std::move(object.diagnostics);
            return result;
        }
        result.object_bytes = object.bytes.size();
        const auto object_path = std::filesystem::path(output_path.string() + (format == NativeObjectFormat::elf64 ? ".forge-link.o" : ".forge-link.obj"));
        const auto temporary_output = std::filesystem::path(output_path.string() + ".forge-link-output");
        const auto object_data = to_bytes(object.bytes);
        write_file_atomic(object_path, object_data);

        std::ostringstream command;
        command << quote_argument(options.linker.string()) << ' ' << quote_argument(object_path.string());
        for (const auto& argument : options.arguments) command << ' ' << quote_argument(argument);
        for (const auto& path : options.library_paths) command << " -L" << quote_argument(path.string());
        for (const auto& library : options.libraries) command << " -l" << quote_argument(library);
        command << " -o " << quote_argument(temporary_output.string());
        result.command = command.str();
        result.linker_invoked = true;
        const int status = std::system(result.command.c_str());
        std::filesystem::remove(object_path);
        if (status != 0 || !std::filesystem::is_regular_file(temporary_output)) {
            std::filesystem::remove(temporary_output);
            add_error(result.diagnostics, "native linker failed with status " + std::to_string(status));
            return result;
        }
        const auto binary = read_file(temporary_output);
        std::filesystem::remove(temporary_output);
        cache.store(result.cache_key, binary);
        write_file_atomic(output_path, binary);
        result.binary_bytes = binary.size();
    } catch (const std::exception& exception) {
        add_error(result.diagnostics, exception.what());
    }
    return result;
}

NativeLibraryLinkResult link_native_shared_library(
    std::span<const std::filesystem::path> object_paths,
    const std::filesystem::path& output_path,
    const NativeLinkOptions& options) {
    NativeLibraryLinkResult result;
    result.output_path = output_path;
    result.input_count = object_paths.size();
    if (object_paths.empty()) {
        add_error(result.diagnostics, "shared library link requires at least one object file");
        return result;
    }

    if (output_path.empty()) {
        add_error(result.diagnostics, "shared library output path is empty");
        return result;
    }
    try {
        for (const auto& path : object_paths)
            if (!std::filesystem::is_regular_file(path))
                throw std::runtime_error("shared library input does not exist: " + path.string());
        if (!output_path.parent_path().empty()) std::filesystem::create_directories(output_path.parent_path());
        const auto temporary_output = std::filesystem::path(output_path.string() + ".forge-shared-output");
        std::ostringstream command;
        command << quote_argument(options.linker.string()) << " -shared";
        for (const auto& path : object_paths) command << ' ' << quote_argument(path.string());
        for (const auto& argument : options.arguments) command << ' ' << quote_argument(argument);
        for (const auto& path : options.library_paths) command << " -L" << quote_argument(path.string());
        for (const auto& library : options.libraries) command << " -l" << quote_argument(library);
        command << " -o " << quote_argument(temporary_output.string());
        result.command = command.str();
        const int status = std::system(result.command.c_str());
        if (status != 0 || !std::filesystem::is_regular_file(temporary_output)) {
            std::filesystem::remove(temporary_output);
            add_error(result.diagnostics, "shared library linker failed with status " + std::to_string(status));
            return result;
        }
        std::error_code error;
        std::filesystem::rename(temporary_output, output_path, error);
        if (error) {
            std::filesystem::remove(output_path, error);
            error.clear();
            std::filesystem::rename(temporary_output, output_path, error);
            if (error) {
                std::filesystem::remove(temporary_output);
                throw std::runtime_error("failed to commit shared library: " + output_path.string());
            }
        }
        result.output_bytes = std::filesystem::file_size(output_path);
    } catch (const std::exception& exception) {
        add_error(result.diagnostics, exception.what());
    }
    return result;
}

} // namespace forge::object
