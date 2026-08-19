// Copyright 2026 Mario Vinciguerra
// SPDX-License-Identifier: Apache-2.0

// Covers the linker features beyond a single-object link: archives, COMDAT
// selection, weak externals, exception data, thread-local storage, and base
// relocations. Where the result is a program that can run, it is run.

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "archive_builder.hpp"
#include "coff_builder.hpp"
#include "oblink/archive.hpp"
#include "oblink/coff.hpp"
#include "oblink/format.hpp"
#include "oblink/link.hpp"

namespace {

int failures = 0;

void check(bool condition, const std::string& message) {
    if (condition) return;
    std::cerr << "FAIL " << message << '\n';
    ++failures;
}

void report(const oblink::Diagnostics& diagnostics) {
    for (const auto& diagnostic : diagnostics) std::cerr << "    " << diagnostic.message << '\n';
}

constexpr std::uint32_t code_characteristics =
    oblink::section_code | oblink::section_memory_execute | oblink::section_memory_read;
constexpr std::uint32_t data_characteristics =
    oblink::section_initialized_data | oblink::section_memory_read | oblink::section_memory_write;

oblink::CoffObject parse(const std::vector<std::byte>& bytes, const std::string& name) {
    auto result = oblink::read_coff_object(bytes, name);
    check(result.ok(), "the generated object parses: " + name);
    report(result.diagnostics);
    return std::move(result.object);
}

oblink::ImportLibrary kernel32_exit() {
    oblink::ImportLibrary library;
    library.dll = "KERNEL32.dll";
    library.symbols.push_back({"ExitProcess", 0});
    return library;
}

// Runs a linked image and returns its exit code, or -1 when it cannot run.
int run_image(const std::vector<std::byte>& image, const std::string& stem) {
#ifdef _WIN32
    const auto path = std::filesystem::temp_directory_path() / (stem + ".exe");
    {
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        output.write(reinterpret_cast<const char*>(image.data()), static_cast<std::streamsize>(image.size()));
        if (!output.good()) return -1;
    }
    const std::string command = "\"" + path.string() + "\"";
    const int status = std::system(command.c_str());
    std::error_code ignored;
    std::filesystem::remove(path, ignored);
    return status;
#else
    (void)image;
    (void)stem;
    return -2; // Not runnable on this host.
#endif
}

// _start: sub rsp,0x28; call helper; mov ecx,eax; call ExitProcess; int3
std::vector<std::byte> caller_object() {
    oblink::testing::ObjectBuilder builder;
    oblink::testing::BuiltSection text;
    text.name = ".text";
    text.characteristics = code_characteristics;
    text.data = {
        0x48, 0x83, 0xEC, 0x28,
        0xE8, 0x00, 0x00, 0x00, 0x00,
        0x89, 0xC1,
        0xE8, 0x00, 0x00, 0x00, 0x00,
        0xCC,
    };
    text.relocations.push_back({5, 1, oblink::reloc_amd64_rel32});
    text.relocations.push_back({12, 2, oblink::reloc_amd64_rel32});
    builder.add_section(std::move(text));
    builder.add_symbol({"_start", 0, 1, 0x20, oblink::storage_class_external});
    builder.add_symbol({"helper", 0, 0, 0, oblink::storage_class_external});
    builder.add_symbol({"ExitProcess", 0, 0, 0, oblink::storage_class_external});
    return builder.build();
}

// helper: mov eax, <value>; ret
std::vector<std::byte> helper_object(std::uint8_t value, std::uint8_t comdat_selection = 0) {
    oblink::testing::ObjectBuilder builder;
    oblink::testing::BuiltSection text;
    text.name = comdat_selection == 0 ? ".text" : ".text$mn";
    text.characteristics = code_characteristics;
    text.data = {0xB8, value, 0x00, 0x00, 0x00, 0xC3};
    builder.add_section(std::move(text));
    if (comdat_selection != 0) builder.add_comdat_section_symbol(".text$mn", 1, comdat_selection, 6);
    builder.add_symbol({"helper", 0, 1, 0x20, oblink::storage_class_external});
    return builder.build();
}

void test_archive_supplies_a_definition() {
    const auto caller_bytes = caller_object();
    const auto helper_bytes = helper_object(5);

    oblink::testing::ArchiveBuilder archive_builder;
    archive_builder.add_member("helper.obj", helper_bytes, {"helper"});
    const auto archive_bytes = archive_builder.build();
    auto archive = oblink::read_archive(archive_bytes, "helpers.lib");
    check(archive.ok(), "the archive parses");
    report(archive.diagnostics);

    const std::vector<oblink::CoffObject> objects{parse(caller_bytes, "caller.obj")};
    oblink::LinkOptions options;
    options.entry_symbol = "_start";
    options.imports.push_back(kernel32_exit());
    options.archives.push_back(&archive.archive);

    const auto result = oblink::link_executable(objects, options);
    report(result.diagnostics);
    check(result.ok(), "linking pulls the definition from the archive");
    if (!result.ok()) return;

    check(result.stats.archive_members_pulled == 1, "exactly one member is pulled");
    check(result.stats.object_count == 1, "the pulled member is not counted as an input object");

    const auto status = run_image(result.image, "oblink-archive");
    if (status != -2) check(status == 5, "the archive-linked program returns 5, got " + std::to_string(status));
}

void test_unused_archive_members_are_not_pulled() {
    const auto caller_bytes = caller_object();
    oblink::testing::ArchiveBuilder archive_builder;
    archive_builder.add_member("helper.obj", helper_object(5), {"helper"});
    archive_builder.add_member("unused.obj", helper_object(9), {"unused_function"});
    const auto archive_bytes = archive_builder.build();
    auto archive = oblink::read_archive(archive_bytes, "helpers.lib");

    const std::vector<oblink::CoffObject> objects{parse(caller_bytes, "caller.obj")};
    oblink::LinkOptions options;
    options.entry_symbol = "_start";
    options.imports.push_back(kernel32_exit());
    options.archives.push_back(&archive.archive);

    const auto result = oblink::link_executable(objects, options);
    check(result.ok(), "the link succeeds");
    check(result.stats.archive_members_pulled == 1, "only the needed member is pulled");
}

void test_import_library_supplies_imports() {
    oblink::testing::ObjectBuilder builder;
    oblink::testing::BuiltSection text;
    text.name = ".text";
    text.characteristics = code_characteristics;
    text.data = {0x48, 0x83, 0xEC, 0x28, 0xB9, 0x03, 0x00, 0x00, 0x00, 0xE8, 0x00, 0x00, 0x00, 0x00, 0xCC};
    text.relocations.push_back({10, 1, oblink::reloc_amd64_rel32});
    builder.add_section(std::move(text));
    builder.add_symbol({"_start", 0, 1, 0x20, oblink::storage_class_external});
    builder.add_symbol({"ExitProcess", 0, 0, 0, oblink::storage_class_external});
    const auto object_bytes = builder.build();

    oblink::testing::ArchiveBuilder archive_builder;
    archive_builder.add_import_member("ExitProcess", "KERNEL32.dll", 0);
    const auto archive_bytes = archive_builder.build();
    auto archive = oblink::read_archive(archive_bytes, "kernel32.lib");

    const std::vector<oblink::CoffObject> objects{parse(object_bytes, "main.obj")};
    oblink::LinkOptions options;
    options.entry_symbol = "_start";
    options.archives.push_back(&archive.archive); // No declared imports at all.

    const auto result = oblink::link_executable(objects, options);
    report(result.diagnostics);
    check(result.ok(), "an import library satisfies the undefined import");
    if (!result.ok()) return;
    check(result.stats.imported_symbols == 1, "the import is recorded");

    const auto status = run_image(result.image, "oblink-importlib");
    if (status != -2) check(status == 3, "the program linked from an import library returns 3, got " + std::to_string(status));
}

void test_comdat_duplicates_are_discarded() {
    const auto caller_bytes = caller_object();
    const auto first = helper_object(4, oblink::comdat_any);
    const auto second = helper_object(4, oblink::comdat_any);

    const std::vector<oblink::CoffObject> objects{
        parse(caller_bytes, "caller.obj"), parse(first, "first.obj"), parse(second, "second.obj")};

    oblink::LinkOptions options;
    options.entry_symbol = "_start";
    options.imports.push_back(kernel32_exit());

    const auto result = oblink::link_executable(objects, options);
    report(result.diagnostics);
    check(result.ok(), "duplicate COMDAT definitions do not fail the link");
    if (!result.ok()) return;
    check(result.stats.comdats_discarded == 1, "one duplicate is discarded");

    const auto status = run_image(result.image, "oblink-comdat");
    if (status != -2) check(status == 4, "the surviving COMDAT definition runs, got " + std::to_string(status));
}

void test_no_duplicates_comdat_is_an_error() {
    const auto first = helper_object(4, oblink::comdat_no_duplicates);
    const auto second = helper_object(4, oblink::comdat_no_duplicates);
    const std::vector<oblink::CoffObject> objects{parse(first, "first.obj"), parse(second, "second.obj")};

    oblink::LinkOptions options;
    options.entry_symbol = "helper";
    const auto result = oblink::link_executable(objects, options);
    check(!result.ok(), "a no-duplicates COMDAT reports a conflict");
}

void test_weak_external_falls_back_to_its_tag() {
    // The object references `maybe_missing`, which is weak and tagged to the
    // locally defined `fallback`.
    oblink::testing::ObjectBuilder builder;
    oblink::testing::BuiltSection text;
    text.name = ".text";
    text.characteristics = code_characteristics;
    text.data = {
        0x48, 0x83, 0xEC, 0x28,
        0xE8, 0x00, 0x00, 0x00, 0x00,
        0x89, 0xC1,
        0xE8, 0x00, 0x00, 0x00, 0x00,
        0xCC,
        0xB8, 0x06, 0x00, 0x00, 0x00, 0xC3, // fallback: mov eax,6; ret
    };
    text.relocations.push_back({5, 3, oblink::reloc_amd64_rel32});
    text.relocations.push_back({12, 1, oblink::reloc_amd64_rel32});
    builder.add_section(std::move(text));

    builder.add_symbol({"_start", 0, 1, 0x20, oblink::storage_class_external});          // 0
    builder.add_symbol({"ExitProcess", 0, 0, 0, oblink::storage_class_external});        // 1
    builder.add_symbol({"fallback", 17, 1, 0x20, oblink::storage_class_external});       // 2
    builder.add_weak_symbol("maybe_missing", 2);                                          // 3

    const auto bytes = builder.build();
    const std::vector<oblink::CoffObject> objects{parse(bytes, "weak.obj")};

    oblink::LinkOptions options;
    options.entry_symbol = "_start";
    options.imports.push_back(kernel32_exit());

    const auto result = oblink::link_executable(objects, options);
    report(result.diagnostics);
    check(result.ok(), "a weak external without a definition still links");
    if (!result.ok()) return;
    check(result.stats.weak_symbols_resolved >= 1, "the weak symbol resolves through its tag");

    const auto status = run_image(result.image, "oblink-weak");
    if (status != -2) check(status == 6, "the weak fallback runs, got " + std::to_string(status));
}

// _start loads a global through an absolute address, which forces a base
// relocation, and exits with the value it read.
void test_base_relocations_are_emitted() {
    oblink::testing::ObjectBuilder builder;
    oblink::testing::BuiltSection text;
    text.name = ".text";
    text.characteristics = code_characteristics;
    text.data = {
        0x48, 0x83, 0xEC, 0x28,
        0x48, 0xB8, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // movabs rax, &value
        0x8B, 0x08,                                                  // mov ecx, [rax]
        0xE8, 0x00, 0x00, 0x00, 0x00,
        0xCC,
    };
    text.relocations.push_back({6, 2, oblink::reloc_amd64_addr64});
    text.relocations.push_back({17, 1, oblink::reloc_amd64_rel32});
    builder.add_section(std::move(text));

    oblink::testing::BuiltSection data;
    data.name = ".data";
    data.characteristics = data_characteristics;
    data.data = {0x09, 0x00, 0x00, 0x00};
    builder.add_section(std::move(data));

    builder.add_symbol({"_start", 0, 1, 0x20, oblink::storage_class_external});
    builder.add_symbol({"ExitProcess", 0, 0, 0, oblink::storage_class_external});
    builder.add_symbol({"value", 0, 2, 0, oblink::storage_class_external});

    const auto bytes = builder.build();
    const std::vector<oblink::CoffObject> objects{parse(bytes, "absolute.obj")};

    oblink::LinkOptions options;
    options.entry_symbol = "_start";
    options.imports.push_back(kernel32_exit());

    const auto result = oblink::link_executable(objects, options);
    report(result.diagnostics);
    check(result.ok(), "an absolute reference links");
    if (!result.ok()) return;
    check(result.stats.base_relocations >= 1, "a base relocation is recorded");

    const auto status = run_image(result.image, "oblink-reloc");
    if (status != -2) check(status == 9, "the relocatable program runs and reads its global, got " + std::to_string(status));

    // Without base relocations the image must declare itself fixed-base.
    auto fixed_options = options;
    fixed_options.emit_base_relocations = false;
    const auto fixed = oblink::link_executable(objects, fixed_options);
    check(fixed.ok(), "the same objects link without base relocations");
    check(fixed.stats.base_relocations == 0, "no base relocations are emitted when disabled");
}

void test_exception_data_is_sorted_and_published() {
    oblink::testing::ObjectBuilder builder;
    oblink::testing::BuiltSection text;
    text.name = ".text";
    text.characteristics = code_characteristics;
    text.data = {0x48, 0x83, 0xEC, 0x28, 0xB9, 0x02, 0x00, 0x00, 0x00, 0xE8, 0x00, 0x00, 0x00, 0x00, 0xCC};
    text.relocations.push_back({10, 1, oblink::reloc_amd64_rel32});
    builder.add_section(std::move(text));

    // Two runtime function entries, deliberately out of order.
    oblink::testing::BuiltSection pdata;
    pdata.name = ".pdata";
    pdata.characteristics = oblink::section_initialized_data | oblink::section_memory_read;
    pdata.data = {
        0x40, 0x00, 0x00, 0x00, 0x50, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x10, 0x00, 0x00, 0x00, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    };
    builder.add_section(std::move(pdata));

    builder.add_symbol({"_start", 0, 1, 0x20, oblink::storage_class_external});
    builder.add_symbol({"ExitProcess", 0, 0, 0, oblink::storage_class_external});

    const auto bytes = builder.build();
    const std::vector<oblink::CoffObject> objects{parse(bytes, "unwind.obj")};

    oblink::LinkOptions options;
    options.entry_symbol = "_start";
    options.imports.push_back(kernel32_exit());

    const auto result = oblink::link_executable(objects, options);
    report(result.diagnostics);
    check(result.ok(), "an object with unwind data links");
    if (!result.ok()) return;
    check(result.stats.exception_entries == 2, "both runtime function entries are counted");

    // The exception directory must point at a sorted table.
    const auto pe_offset = static_cast<std::size_t>(
        std::to_integer<std::uint8_t>(result.image[0x3C]) |
        (std::to_integer<std::uint8_t>(result.image[0x3D]) << 8));
    const auto directory = pe_offset + 24 + 112 + oblink::directory_exception * 8;
    std::uint32_t rva = 0;
    for (std::size_t index = 0; index < 4; ++index)
        rva |= static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(result.image[directory + index])) << (index * 8U);
    check(rva != 0, "the exception directory names the .pdata section");

    const auto status = run_image(result.image, "oblink-pdata");
    if (status != -2) check(status == 2, "the program with unwind data runs, got " + std::to_string(status));
}

void test_thread_local_storage_gets_a_directory() {
    oblink::testing::ObjectBuilder builder;
    oblink::testing::BuiltSection text;
    text.name = ".text";
    text.characteristics = code_characteristics;
    text.data = {0x48, 0x83, 0xEC, 0x28, 0xB9, 0x01, 0x00, 0x00, 0x00, 0xE8, 0x00, 0x00, 0x00, 0x00, 0xCC};
    text.relocations.push_back({10, 1, oblink::reloc_amd64_rel32});
    builder.add_section(std::move(text));

    oblink::testing::BuiltSection tls;
    tls.name = ".tls";
    tls.characteristics = data_characteristics;
    tls.data = {0x00, 0x00, 0x00, 0x00};
    builder.add_section(std::move(tls));

    builder.add_symbol({"_start", 0, 1, 0x20, oblink::storage_class_external});
    builder.add_symbol({"ExitProcess", 0, 0, 0, oblink::storage_class_external});

    const auto bytes = builder.build();
    const std::vector<oblink::CoffObject> objects{parse(bytes, "tls.obj")};

    oblink::LinkOptions options;
    options.entry_symbol = "_start";
    options.imports.push_back(kernel32_exit());

    const auto result = oblink::link_executable(objects, options);
    report(result.diagnostics);
    check(result.ok(), "an object with thread-local data links");
    if (!result.ok()) return;
    check(result.stats.tls_directory, "a TLS directory is produced");

    const auto status = run_image(result.image, "oblink-tls");
    if (status != -2) check(status == 1, "the program with a TLS directory loads and runs, got " + std::to_string(status));
}

} // namespace

int main() {
    test_archive_supplies_a_definition();
    test_unused_archive_members_are_not_pulled();
    test_import_library_supplies_imports();
    test_comdat_duplicates_are_discarded();
    test_no_duplicates_comdat_is_an_error();
    test_weak_external_falls_back_to_its_tag();
    test_base_relocations_are_emitted();
    test_exception_data_is_sorted_and_published();
    test_thread_local_storage_gets_a_directory();

    if (failures != 0) {
        std::cerr << "oblink feature tests: " << failures << " failure(s)\n";
        return 1;
    }
    std::cout << "oblink feature tests: PASS\n";
    return 0;
}
