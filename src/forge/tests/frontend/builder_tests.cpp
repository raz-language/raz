// Copyright 2026 Mario Vinciguerra
// SPDX-License-Identifier: Apache-2.0

#include "forge/ir/builder.hpp"
#include "forge/ir/printer.hpp"
#include "forge/ir/incremental.hpp"
#include "forge/ir/source_map.hpp"
#include "forge/ir/verifier.hpp"
#include <iostream>
#include <stdexcept>

int main() {
    forge::ir::Context context;
    auto& module = context.create_module("builder_test");
    forge::ir::IRBuilder builder(context, module);
    std::vector<forge::ir::ValueDecl> parameters{{"%left", forge::ir::i64_type()}, {"%right", forge::ir::i64_type()}};
    const auto function_handle = builder.create_function_handle("memory_add", forge::ir::i64_type(), parameters);
    const auto entry_handle = builder.create_block_handle(function_handle, "entry");
    builder.position_at_end(entry_handle);
    builder.set_source_range("sample.rz", 3, 7, 3, 18);
    builder.set_module_metadata("frontend.language", "Raz");
    builder.set_next_attribute("frontend.ast_id", "42");
    builder.set_next_attribute("frontend.debug_name", "scratch");
    auto& entry = builder.resolve(entry_handle);
    const auto slot = builder.create_stack_allocation(8, 8);
    builder.create_store(forge::ir::i64_type(), "%right", slot, 8);
    const auto loaded = builder.create_load(forge::ir::i64_type(), slot, 8);
    const auto sum = builder.create_add(forge::ir::i64_type(), "%left", loaded);
    builder.create_return(sum);
    if (slot != "%v0" || sum != "%v2") return 1;
    if (entry.operations.front().source_file != "sample.rz" || entry.operations.front().source_line != 3 ||
        entry.operations.front().source_end_column != 18) return 1;
    if (entry.operations.front().attributes.size() != 2 ||
        entry.operations.front().attributes.front().name != "frontend.ast_id") return 1;
    if (builder.module_metadata("frontend.language") != "Raz") return 1;
    if (!builder.verify().empty()) return 1;
    if (!builder.insertion_block_terminated()) return 1;
    bool rejected_after_terminator = false;
    try { (void)builder.create_constant(forge::ir::i64_type(), "1"); }
    catch (const std::logic_error& error) {
        rejected_after_terminator = std::string(error.what()).find("sample.rz:3:7") != std::string::npos;
    }

    if (!rejected_after_terminator) return 1;
    // Grow both vectors and prove stable handles still resolve correctly.
    const auto helper = builder.create_function_handle("helper", forge::ir::void_type());
    (void)builder.create_block_handle(helper, "entry");
    if (builder.find_function("memory_add").index != function_handle.index) return 1;
    if (builder.find_block(function_handle, "entry").block_index != entry_handle.block_index) return 1;
    if (builder.resolve(function_handle).name != "memory_add" || builder.resolve(entry_handle).name != "entry") return 1;
    bool rejected_duplicate_block = false;
    try { (void)builder.create_block(builder.resolve(function_handle), "entry"); }
    catch (const std::invalid_argument&) { rejected_duplicate_block = true; }
    if (!rejected_duplicate_block) return 1;
    const auto text = forge::ir::print_module(module);
    if (text.find("stack.alloc ptr 8 align 8") == std::string::npos) return 1;
    if (text.find("store i64") == std::string::npos) return 1;
    const auto source_map = forge::ir::build_source_map_json(module);
    if (source_map.find("\"frontend.language\":\"Raz\"") == std::string::npos ||
        source_map.find("\"frontend.ast_id\":\"42\"") == std::string::npos ||
        source_map.find("\"endColumn\":18") == std::string::npos) return 1;
    const auto initial_snapshot = forge::ir::build_incremental_snapshot(module);
    if (initial_snapshot.semantic_fingerprint.size() != 64 || initial_snapshot.frontend_fingerprint.size() != 64)
        return 1;
    const auto repeated_snapshot = forge::ir::build_incremental_snapshot(module);
    if (initial_snapshot.semantic_fingerprint != repeated_snapshot.semantic_fingerprint ||
        initial_snapshot.frontend_fingerprint != repeated_snapshot.frontend_fingerprint)
        return 1;
    auto metadata_only = module;
    metadata_only.metadata().push_back({"frontend.cache_hint", "warm"});
    const auto metadata_snapshot = forge::ir::build_incremental_snapshot(metadata_only);
    if (initial_snapshot.semantic_fingerprint != metadata_snapshot.semantic_fingerprint ||
        initial_snapshot.frontend_fingerprint == metadata_snapshot.frontend_fingerprint)
        return 1;
    auto semantic_change = module;
    semantic_change.functions().front().blocks.front().operations.front().alignment = 16;
    const auto changed_snapshot = forge::ir::build_incremental_snapshot(semantic_change);
    const auto changes = forge::ir::compare_incremental_snapshots(initial_snapshot, changed_snapshot);
    if (changes.size() != 1 || changes.front().name != "memory_add" ||
        changes.front().kind != forge::ir::FunctionChangeKind::modified)
        return 1;
    const auto metadata_changes = forge::ir::compare_incremental_snapshots(initial_snapshot, metadata_snapshot);
    if (!metadata_changes.empty()) return 1;
    const auto manifest = forge::ir::build_incremental_manifest_json(initial_snapshot);
    if (manifest.find("\"semanticFingerprint\"") == std::string::npos ||
        manifest.find("memory_add") == std::string::npos)
        return 1;
    const auto cache_key = forge::ir::build_cache_key(module, "raz", "-O2;x86_64");
    if (cache_key.size() != 64 || cache_key != forge::ir::build_cache_key(module, "raz", "-O2;x86_64") ||
        cache_key == forge::ir::build_cache_key(module, "raz", "-O3;x86_64"))
        return 1;
    std::cout << "builder frontend safety and incremental fingerprint test passed\n";
    return 0;
}
