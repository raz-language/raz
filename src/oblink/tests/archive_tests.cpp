// Copyright 2026 Mario Vinciguerra
// SPDX-License-Identifier: Apache-2.0

#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

#include "archive_builder.hpp"
#include "coff_builder.hpp"
#include "oblink/archive.hpp"
#include "oblink/format.hpp"

namespace {

int failures = 0;

void check(bool condition, const std::string& message) {
    if (condition) return;
    std::cerr << "FAIL " << message << '\n';
    ++failures;
}

std::vector<std::byte> simple_object(const std::string& symbol) {
    oblink::testing::ObjectBuilder builder;
    oblink::testing::BuiltSection text;
    text.name = ".text";
    text.characteristics = oblink::section_code | oblink::section_memory_execute | oblink::section_memory_read;
    text.data = {0xB8, 0x05, 0x00, 0x00, 0x00, 0xC3};
    builder.add_section(std::move(text));
    builder.add_symbol({symbol, 0, 1, 0x20, oblink::storage_class_external});
    return builder.build();
}

void test_reads_members_and_symbol_index() {
    oblink::testing::ArchiveBuilder builder;
    builder.add_member("helper.obj", simple_object("helper_function"), {"helper_function"});
    builder.add_member("second.obj", simple_object("second_function"), {"second_function"});

    const auto bytes = builder.build();
    const auto result = oblink::read_archive(bytes, "sample.lib");

    for (const auto& diagnostic : result.diagnostics) std::cerr << "  " << diagnostic.message << '\n';
    check(result.ok(), "a well-formed archive parses");
    if (!result.ok()) return;

    check(result.archive.members.size() == 2, "both members are read");
    check(result.archive.members[0].name == "helper.obj", "member names are read");
    check(result.archive.symbol_index.size() == 2, "the symbol index is read");
    check(result.archive.symbol_index.contains("helper_function"), "the index maps the first symbol");
    check(result.archive.symbol_index.contains("second_function"), "the index maps the second symbol");

    if (result.archive.symbol_index.contains("second_function")) {
        const auto member = result.archive.symbol_index.at("second_function");
        check(result.archive.members.at(member).name == "second.obj",
            "the index points at the member that defines the symbol");
    }
}

void test_reads_long_member_names() {
    oblink::testing::ArchiveBuilder builder;
    const std::string name = "a_member_name_far_longer_than_fifteen_characters.obj";
    builder.add_member(name, simple_object("long_named_function"), {"long_named_function"});

    const auto bytes = builder.build();
    const auto result = oblink::read_archive(bytes, "long.lib");
    check(result.ok(), "an archive with long names parses");
    if (!result.ok()) return;
    check(result.archive.members.size() == 1, "the member is read");
    check(result.archive.members[0].name == name, "the long member name is resolved");
}

void test_reads_import_members() {
    oblink::testing::ArchiveBuilder builder;
    builder.add_import_member("CreateFileW", "KERNEL32.dll", 42);

    const auto bytes = builder.build();
    const auto result = oblink::read_archive(bytes, "kernel32.lib");
    check(result.ok(), "an import library parses");
    if (!result.ok()) return;

    check(result.archive.imports.size() == 1, "the import member is read");
    check(result.archive.members.empty(), "an import member is not treated as an object");
    if (result.archive.imports.size() == 1) {
        check(result.archive.imports[0].symbol == "CreateFileW", "the imported symbol name is read");
        check(result.archive.imports[0].dll == "KERNEL32.dll", "the source DLL is read");
        check(result.archive.imports[0].hint == 42, "the hint is read");
    }
    check(result.archive.import_index.contains("CreateFileW"), "imports are indexed by symbol");
}

void test_rejects_a_non_archive() {
    const std::vector<std::byte> bytes(32, std::byte{0x7F});
    const auto result = oblink::read_archive(bytes, "junk.lib");
    check(!result.ok(), "a file without the archive signature is rejected");
}

} // namespace

int main() {
    test_reads_members_and_symbol_index();
    test_reads_long_member_names();
    test_reads_import_members();
    test_rejects_a_non_archive();

    if (failures != 0) {
        std::cerr << "oblink archive tests: " << failures << " failure(s)\n";
        return 1;
    }
    std::cout << "oblink archive tests: PASS\n";
    return 0;
}
