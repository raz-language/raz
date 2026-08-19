// Copyright 2026 Mario Vinciguerra
// SPDX-License-Identifier: Apache-2.0

#include "oblink/image.hpp"

#include <algorithm>
#include <array>
#include <limits>
#include <span>
#include <string_view>
#include <type_traits>

namespace oblink {
namespace {

constexpr std::uint32_t pe_signature = 0x00004550; // "PE\0\0"
constexpr std::uint16_t machine_amd64 = 0x8664;
constexpr std::uint16_t optional_header_magic_pe32plus = 0x020B;
constexpr std::uint16_t optional_header_size_pe32plus = 240;

constexpr std::uint16_t characteristics_executable = 0x0002;
constexpr std::uint16_t characteristics_large_address_aware = 0x0020;
// Set only when the image carries no base relocation table, which tells the
// loader it must be mapped at its preferred base.
constexpr std::uint16_t characteristics_relocs_stripped = 0x0001;

constexpr std::uint16_t dll_characteristics_dynamic_base = 0x0040;
constexpr std::uint16_t dll_characteristics_nx_compat = 0x0100;
constexpr std::uint16_t dll_characteristics_terminal_server_aware = 0x8000;

class ByteWriter {
public:
    template <typename T>
    void integer(T value) {
        using U = std::make_unsigned_t<T>;
        const auto bits = static_cast<U>(value);
        for (std::size_t index = 0; index < sizeof(T); ++index)
            bytes_.push_back(static_cast<std::byte>(static_cast<std::uint8_t>(bits >> (index * 8U))));
    }

    void raw(std::span<const std::byte> bytes) { bytes_.insert(bytes_.end(), bytes.begin(), bytes.end()); }

    void text(std::string_view value, std::size_t width) {
        for (std::size_t index = 0; index < width; ++index)
            bytes_.push_back(index < value.size()
                ? static_cast<std::byte>(static_cast<unsigned char>(value[index]))
                : std::byte{0});
    }

    void zeros(std::size_t count) { bytes_.insert(bytes_.end(), count, std::byte{0}); }

    void pad_to(std::size_t offset) {
        if (bytes_.size() < offset) zeros(offset - bytes_.size());
    }

    [[nodiscard]] std::size_t size() const noexcept { return bytes_.size(); }
    [[nodiscard]] std::vector<std::byte> take() && noexcept { return std::move(bytes_); }

private:
    std::vector<std::byte> bytes_;
};

// The classic stub: a tiny real-mode program that prints a message when the
// image is run under DOS. Windows only reads the e_lfanew field at 0x3C.
constexpr std::array<std::uint8_t, 128> dos_stub = {
    0x4D, 0x5A, 0x90, 0x00, 0x03, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0x00, 0x00,
    0xB8, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0x00, 0x00, 0x00,
    0x0E, 0x1F, 0xBA, 0x0E, 0x00, 0xB4, 0x09, 0xCD, 0x21, 0xB8, 0x01, 0x4C, 0xCD, 0x21, 0x54, 0x68,
    0x69, 0x73, 0x20, 0x70, 0x72, 0x6F, 0x67, 0x72, 0x61, 0x6D, 0x20, 0x63, 0x61, 0x6E, 0x6E, 0x6F,
    0x74, 0x20, 0x62, 0x65, 0x20, 0x72, 0x75, 0x6E, 0x20, 0x69, 0x6E, 0x20, 0x44, 0x4F, 0x53, 0x20,
    0x6D, 0x6F, 0x64, 0x65, 0x2E, 0x0D, 0x0D, 0x0A, 0x24, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};

} // namespace

ImageWriteResult write_pe_image(const ImageLayout& layout) {
    ImageWriteResult result;

    if (layout.sections.empty()) {
        add_error(result.diagnostics, "cannot write an image with no sections");
        return result;
    }
    if (layout.section_alignment == 0 || layout.file_alignment == 0) {
        add_error(result.diagnostics, "image alignments must be non-zero");
        return result;
    }
    if (layout.section_alignment < layout.file_alignment) {
        add_error(result.diagnostics, "section alignment must not be smaller than file alignment");
        return result;
    }
    if (layout.sections.size() > std::numeric_limits<std::uint16_t>::max()) {
        add_error(result.diagnostics, "image has more sections than the PE format allows");
        return result;
    }

    const auto section_count = static_cast<std::uint16_t>(layout.sections.size());
    const std::size_t headers_size =
        dos_stub.size() + 4U + 20U + optional_header_size_pe32plus + static_cast<std::size_t>(section_count) * 40U;
    const auto headers_on_disk = align_up(static_cast<std::uint32_t>(headers_size), layout.file_alignment);

    // Assign file offsets in section order; sections without file bytes keep a
    // zero pointer so the loader zero-fills them.
    std::vector<std::uint32_t> raw_offsets(layout.sections.size());
    std::vector<std::uint32_t> raw_sizes(layout.sections.size());
    std::uint32_t cursor = headers_on_disk;
    std::uint32_t image_size = align_up(headers_on_disk, layout.section_alignment);

    for (std::size_t index = 0; index < layout.sections.size(); ++index) {
        const auto& section = layout.sections[index];
        const auto raw_size = align_up(static_cast<std::uint32_t>(section.data.size()), layout.file_alignment);
        if (section.data.empty()) {
            raw_offsets[index] = 0;
            raw_sizes[index] = 0;
        } else {
            raw_offsets[index] = cursor;
            raw_sizes[index] = raw_size;
            cursor += raw_size;
        }
        const auto virtual_size = std::max<std::uint32_t>(
            section.virtual_size, static_cast<std::uint32_t>(section.data.size()));
        image_size = std::max(image_size, align_up(section.rva + virtual_size, layout.section_alignment));
    }

    ByteWriter writer;
    for (const auto byte : dos_stub) writer.integer(byte);

    writer.integer(pe_signature);
    writer.integer(machine_amd64);
    writer.integer(section_count);
    writer.integer(std::uint32_t{0}); // Timestamp: zero keeps output deterministic.
    writer.integer(std::uint32_t{0}); // Symbol table offset.
    writer.integer(std::uint32_t{0}); // Symbol count.
    writer.integer(optional_header_size_pe32plus);
    std::uint16_t characteristics = characteristics_executable | characteristics_large_address_aware;
    if (layout.relocations_stripped) characteristics |= characteristics_relocs_stripped;
    writer.integer(characteristics);

    std::uint32_t code_size = 0;
    std::uint32_t initialized_size = 0;
    std::uint32_t uninitialized_size = 0;
    std::uint32_t base_of_code = 0;
    for (std::size_t index = 0; index < layout.sections.size(); ++index) {
        const auto& section = layout.sections[index];
        const auto virtual_size = std::max<std::uint32_t>(
            section.virtual_size, static_cast<std::uint32_t>(section.data.size()));
        if ((section.characteristics & section_code) != 0) {
            code_size += raw_sizes[index];
            if (base_of_code == 0) base_of_code = section.rva;
        } else if ((section.characteristics & section_uninitialized_data) != 0) {
            uninitialized_size += virtual_size;
        } else {
            initialized_size += raw_sizes[index];
        }
    }

    writer.integer(optional_header_magic_pe32plus);
    writer.integer(std::uint8_t{14}); // Linker major version.
    writer.integer(std::uint8_t{0});  // Linker minor version.
    writer.integer(code_size);
    writer.integer(initialized_size);
    writer.integer(uninitialized_size);
    writer.integer(layout.entry_rva);
    writer.integer(base_of_code);
    writer.integer(layout.image_base);
    writer.integer(layout.section_alignment);
    writer.integer(layout.file_alignment);
    writer.integer(std::uint16_t{6}); // Major OS version.
    writer.integer(std::uint16_t{0});
    writer.integer(std::uint16_t{0}); // Major image version.
    writer.integer(std::uint16_t{0});
    writer.integer(std::uint16_t{6}); // Major subsystem version.
    writer.integer(std::uint16_t{0});
    writer.integer(std::uint32_t{0}); // Win32 version.
    writer.integer(image_size);
    writer.integer(headers_on_disk);
    writer.integer(std::uint32_t{0}); // Checksum: not required for executables.
    writer.integer(layout.subsystem);
    std::uint16_t dll_characteristics = dll_characteristics_nx_compat | dll_characteristics_terminal_server_aware;
    if (layout.dynamic_base) dll_characteristics |= dll_characteristics_dynamic_base;
    writer.integer(dll_characteristics);
    writer.integer(layout.stack_reserve);
    writer.integer(layout.stack_commit);
    writer.integer(layout.heap_reserve);
    writer.integer(layout.heap_commit);
    writer.integer(std::uint32_t{0}); // Loader flags.
    writer.integer(static_cast<std::uint32_t>(directory_count));
    for (const auto& directory : layout.directories) {
        writer.integer(directory.rva);
        writer.integer(directory.size);
    }

    for (std::size_t index = 0; index < layout.sections.size(); ++index) {
        const auto& section = layout.sections[index];
        const auto virtual_size = std::max<std::uint32_t>(
            section.virtual_size, static_cast<std::uint32_t>(section.data.size()));
        writer.text(section.name, 8);
        writer.integer(virtual_size);
        writer.integer(section.rva);
        writer.integer(raw_sizes[index]);
        writer.integer(raw_offsets[index]);
        writer.integer(std::uint32_t{0}); // Relocation pointer.
        writer.integer(std::uint32_t{0}); // Line number pointer.
        writer.integer(std::uint16_t{0}); // Relocation count.
        writer.integer(std::uint16_t{0}); // Line number count.
        writer.integer(section.characteristics);
    }

    writer.pad_to(headers_on_disk);
    for (std::size_t index = 0; index < layout.sections.size(); ++index) {
        if (layout.sections[index].data.empty()) continue;
        writer.pad_to(raw_offsets[index]);
        writer.raw(layout.sections[index].data);
        writer.pad_to(raw_offsets[index] + raw_sizes[index]);
    }

    result.bytes = std::move(writer).take();
    return result;
}

} // namespace oblink
