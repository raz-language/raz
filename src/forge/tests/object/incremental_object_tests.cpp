// Copyright 2026 Mario Vinciguerra
// SPDX-License-Identifier: Apache-2.0

#include "forge/ir/artifact_cache.hpp"
#include "forge/ir/builder.hpp"
#include "forge/machine/lower.hpp"
#include "forge/machine/optimize.hpp"
#include "forge/object/coff.hpp"
#include "forge/object/elf.hpp"
#include "forge/object/incremental.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <cstdlib>
#if defined(__unix__)
#include <sys/wait.h>
#endif

namespace {
forge::ir::Module make_module() {
    forge::ir::Context context;
    auto& module = context.create_module("incremental-native-object");
    forge::ir::IRBuilder builder(context, module);

    auto main_function = builder.create_function_handle("main", forge::ir::i32_type());
    auto entry = builder.create_block_handle(main_function, "entry");
    builder.position_at_end(entry);
    const auto call = builder.create_call(forge::ir::i32_type(), "@leaf", {});
    builder.create_return(call);

    auto leaf = builder.create_function_handle("leaf", forge::ir::i32_type());
    entry = builder.create_block_handle(leaf, "entry");
    builder.position_at_end(entry);
    const auto seven = builder.create_constant(forge::ir::i32_type(), "7");
    builder.create_return(seven);
    return module;
}
}

int main() {
    const auto ir_module = make_module();
    auto lowered = forge::machine::lower_module(ir_module);
    if (!lowered.ok()) return 1;
    const auto optimization = forge::machine::optimize_module(*lowered.module);
    (void)optimization;
    std::sort(lowered.module->functions.begin(), lowered.module->functions.end(),
              [](const auto& left, const auto& right) { return left.name < right.name; });

    const auto monolithic = forge::object::emit_elf64_x86_64(*lowered.module);
    if (!monolithic.ok()) return 1;

    std::vector<forge::ir::FunctionArtifact> artifacts;
    forge::ir::IncrementalBuildPlan plan;
    const auto cache_root = std::filesystem::temp_directory_path() / "forge-native-object-cache-test";
    std::filesystem::remove_all(cache_root);
    forge::ir::ArtifactCache cache(cache_root);

    for (const auto& function : lowered.module->functions) {
        auto artifact = forge::object::compile_native_function_artifact(
            function, forge::codegen::x86_64::Abi::system_v);
        if (!artifact.ok() || artifact.bytes.empty()) return 1;
        artifacts.push_back({function.name, artifact.bytes});
        const auto key = std::string(64, function.name == "main" ? 'a' : 'b');
        cache.store(key, artifact.bytes);
        plan.functions.push_back({function.name, forge::ir::BuildAction::reuse, key, {}});
    }

    const auto assembled = forge::object::assemble_native_object_artifacts(
        artifacts, lowered.module->globals, forge::object::NativeObjectFormat::elf64,
        forge::codegen::x86_64::Abi::system_v);
    if (!assembled.ok() || assembled.function_count != 2 || assembled.bytes != monolithic.bytes) return 1;

    const auto cached = forge::object::assemble_cached_native_object(
        plan, cache, lowered.module->globals, forge::object::NativeObjectFormat::elf64,
        forge::codegen::x86_64::Abi::system_v);
    if (!cached.ok() || cached.bytes != monolithic.bytes) return 1;


    const auto monolithic_coff = forge::object::emit_coff_x86_64(
        *lowered.module, forge::codegen::x86_64::Abi::windows);
    if (!monolithic_coff.ok()) return 1;
    std::vector<forge::ir::FunctionArtifact> coff_artifacts;
    for (const auto& function : lowered.module->functions) {
        auto artifact = forge::object::compile_native_function_artifact(
            function, forge::codegen::x86_64::Abi::windows);
        if (!artifact.ok()) return 1;
        coff_artifacts.push_back({function.name, std::move(artifact.bytes)});
    }
    const auto assembled_coff = forge::object::assemble_native_object_artifacts(
        coff_artifacts, lowered.module->globals, forge::object::NativeObjectFormat::coff,
        forge::codegen::x86_64::Abi::windows);
    if (!assembled_coff.ok() || assembled_coff.bytes != monolithic_coff.bytes) return 1;

#if defined(__unix__)
    const auto object_path = cache_root / "incremental.o";
    const auto executable_path = cache_root / "incremental-executable";
    {
        std::ofstream output(object_path, std::ios::binary);
        output.write(reinterpret_cast<const char*>(assembled.bytes.data()),
                     static_cast<std::streamsize>(assembled.bytes.size()));
    }
    const auto link_command = std::string("cc -no-pie ") + object_path.string() + " -o " + executable_path.string();
    if (std::system(link_command.c_str()) != 0) return 1;
    const auto run_command = executable_path.string();
    const auto status = std::system(run_command.c_str());
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 7) return 1;
#endif

    auto corrupt = artifacts;
    corrupt.front().bytes.front() ^= 0xffU;
    const auto invalid = forge::object::assemble_native_object_artifacts(
        corrupt, lowered.module->globals, forge::object::NativeObjectFormat::elf64,
        forge::codegen::x86_64::Abi::system_v);
    if (invalid.ok()) return 1;

    std::filesystem::remove_all(cache_root);
    std::cout << "native incremental object test passed\n";
    return 0;
}
