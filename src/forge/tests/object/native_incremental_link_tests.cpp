// Copyright 2026 Mario Vinciguerra
// SPDX-License-Identifier: Apache-2.0

#include "forge/ir/artifact_cache.hpp"
#include "forge/ir/incremental.hpp"
#include "forge/ir/parser.hpp"
#include "forge/machine/lower.hpp"
#include "forge/object/incremental.hpp"
#include "forge/object/native_link.hpp"

#include <algorithm>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#if defined(__unix__)
#include <sys/wait.h>
#endif

#ifndef _WIN32
namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
}
#endif

int main() {
#ifndef _WIN32
    const auto parsed = forge::ir::parse_module(R"(
module @native_incremental_link {
  func @answer() -> i32 {
  entry:
    %value = const i32 42
    return %value
  }
  func @main() -> i32 {
  entry:
    %result = call i32 @answer()
    return %result
  }
}
)");
    require(parsed.ok(), "native link module did not parse");
    const auto lowered = forge::machine::lower_module(*parsed.module);
    require(lowered.ok(), "native link module did not lower");

    const auto root = std::filesystem::temp_directory_path() / "forge-native-incremental-link-test";
    std::filesystem::remove_all(root);
    forge::ir::ArtifactCache cache(root / "cache");
    const auto snapshot = forge::ir::build_incremental_snapshot(*parsed.module);
    auto plan = forge::ir::build_incremental_build_plan({}, snapshot, "native-link-test", "-O2;x86_64");
    for (const auto& function : lowered.module->functions) {
        const auto decision = std::find_if(plan.functions.begin(), plan.functions.end(), [&](const auto& item) {
            return item.name == function.name;
        });
        require(decision != plan.functions.end(), "missing function build decision");
        const auto artifact = forge::object::compile_native_function_artifact(function, forge::codegen::x86_64::Abi::system_v);
        require(artifact.ok(), "function artifact compilation failed");
        cache.store(decision->semantic_cache_key, artifact.bytes);
    }

    forge::object::NativeLinkOptions options;
    options.linker = "cc";
    options.toolchain_identity = "host-cc-test-v1";
    options.arguments = {"-no-pie"};
    const auto first_path = root / "first-executable";
    const auto first = forge::object::link_cached_native_executable(
        plan, cache, lowered.module->globals, forge::object::NativeObjectFormat::elf64,
        forge::codegen::x86_64::Abi::system_v, first_path, options);
    require(first.ok(), "first incremental native link failed");
    require(first.linker_invoked && !first.cache_hit, "first link should invoke the linker");
    require(first.object_bytes > 0 && first.binary_bytes > 0, "first link reported empty artifacts");
    const auto first_status = std::system(first_path.string().c_str());
    require(WIFEXITED(first_status) && WEXITSTATUS(first_status) == 42, "linked executable returned the wrong result");

    const auto second_path = root / "second-executable";
    const auto second = forge::object::link_cached_native_executable(
        plan, cache, lowered.module->globals, forge::object::NativeObjectFormat::elf64,
        forge::codegen::x86_64::Abi::system_v, second_path, options);
    require(second.ok(), "cached incremental native link failed");
    require(second.cache_hit && !second.linker_invoked, "second link should come from the final binary cache");
    require(second.cache_key == first.cache_key, "native link cache key changed unexpectedly");
    require(second.binary_bytes == first.binary_bytes, "cached binary size changed");
    const auto second_status = std::system(second_path.string().c_str());
    require(WIFEXITED(second_status) && WEXITSTATUS(second_status) == 42, "cached executable returned the wrong result");
    require(forge::object::build_native_link_cache_key(plan, forge::object::NativeObjectFormat::elf64,
        forge::codegen::x86_64::Abi::system_v, options) == first.cache_key, "cache key is not deterministic");
    options.arguments.push_back("-s");
    require(forge::object::build_native_link_cache_key(plan, forge::object::NativeObjectFormat::elf64,
        forge::codegen::x86_64::Abi::system_v, options) != first.cache_key, "link options did not affect cache key");
    std::filesystem::remove_all(root);
#endif
    std::cout << "cache-aware native incremental link test passed\n";
}
