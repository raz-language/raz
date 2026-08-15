// Copyright 2026 Mario Vinciguerra
// SPDX-License-Identifier: Apache-2.0

#include "forge/object/archive.hpp"
#include "forge/object/coff.hpp"
#include "forge/object/elf.hpp"

#include <cstdlib>
#include <iostream>

namespace {
void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

forge::machine::Module make_module() {
    forge::machine::Module module;
    module.name = "archive-test";
    forge::machine::Function function;
    function.name = "answer";
    function.register_count = 1;
    function.register_widths = {8};
    function.register_classes = {forge::machine::RegisterClass::integer};
    forge::machine::Block block;
    block.name = "entry";
    block.instructions.push_back({forge::machine::Opcode::load_immediate_i64, 0, {}, 42, 0, {}, {}});
    block.instructions.push_back({forge::machine::Opcode::return_i64, 0, {0}, 0, 0, {}, {}});
    function.blocks.push_back(std::move(block));
    module.functions.push_back(std::move(function));
    return module;
}
}

int main() {
    const auto module = make_module();
    const auto elf = forge::object::emit_elf64_x86_64(module);
    const auto coff = forge::object::emit_coff_x86_64(module);
    require(elf.ok() && coff.ok(), "test object emission failed");

    const std::vector<forge::object::ArchiveMember> elf_members{{"answer-object-with-a-long-name.o", elf.bytes}};
    const auto archive_a = forge::object::emit_static_archive(elf_members);
    const auto archive_b = forge::object::emit_static_archive(elf_members);
    require(archive_a.ok(), "ELF static archive emission failed");
    require(archive_a.bytes == archive_b.bytes, "static archive output is not deterministic");
    require(archive_a.stats.member_count == 1, "archive member count mismatch");
    require(archive_a.stats.symbol_count >= 1, "archive symbol index is empty");
    require(archive_a.stats.long_name_count == 1, "long archive filename was not indexed");
    require(archive_a.bytes.size() > 8, "archive output is too small");
    const char magic[] = "!<arch>\n";
    for (std::size_t index = 0; index < 8; ++index)
        require(std::to_integer<unsigned char>(archive_a.bytes[index]) == static_cast<unsigned char>(magic[index]),
                "archive magic mismatch");

    const std::vector<forge::object::ArchiveMember> coff_members{{"answer.obj", coff.bytes}};
    const auto coff_archive = forge::object::emit_static_archive(coff_members);
    require(coff_archive.ok(), "COFF static archive emission failed");
    require(coff_archive.stats.symbol_count >= 1, "COFF archive symbol index is empty");

    const std::vector<forge::object::ArchiveMember> invalid{{"bad.o", {std::byte{1}, std::byte{2}}}};
    require(!forge::object::emit_static_archive(invalid).ok(), "invalid object was accepted into archive");

    std::cout << "archive tests passed\n";
}
