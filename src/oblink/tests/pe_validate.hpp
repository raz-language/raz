// Copyright 2026 Mario Vinciguerra
// SPDX-License-Identifier: Apache-2.0

// Structural validation for a linked PE32+ image.
//
// The linker tests used to assert only that a file appeared and began with MZ.
// That is not enough: an image whose section table is internally inconsistent
// is written happily and then rejected by the Windows loader with
// STATUS_INVALID_IMAGE_FORMAT, which is indistinguishable from "the file is
// not an executable at all" from a test's point of view. The checks below
// mirror the ones the loader itself performs when mapping an image.

#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <algorithm>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

namespace oblink::testing {

struct PeSection {
  std::string name;
  std::uint32_t virtual_size{};
  std::uint32_t rva{};
  std::uint32_t raw_size{};
  std::uint32_t raw_pointer{};
  std::uint32_t characteristics{};
};

struct PeImage {
  std::vector<unsigned char> bytes;
  std::vector<PeSection> sections;
  std::string error;
  std::uint32_t entry_rva{};
  std::uint32_t size_of_image{};
  std::uint32_t size_of_headers{};
  std::uint16_t subsystem{};
  std::uint16_t dll_characteristics{};
  std::uint16_t file_characteristics{};
  std::uint32_t basereloc_rva{};
  std::uint32_t import_rva{};
  std::uint32_t import_size{};
  std::uint32_t section_count{};

  [[nodiscard]] bool ok() const noexcept { return error.empty(); }

  [[nodiscard]] const PeSection* section(std::string_view name) const {
    for (const auto& s : sections)
      if (s.name == name) return &s;
    return nullptr;
  }

  // Reads `count` bytes of image content at `rva`, or an empty vector when the
  // range is not backed by file data.
  // NUL-terminated string at `rva`, empty when it is not backed by file data.
  [[nodiscard]] std::string string_at_rva(std::uint32_t rva) const {
    const auto bytes = at_rva(rva, 1);
    if (bytes.empty()) return {};
    std::string text;
    for (std::uint32_t i = 0;; ++i) {
      const auto ch = at_rva(rva + i, 1);
      if (ch.empty() || ch[0] == 0) break;
      text.push_back(static_cast<char>(ch[0]));
      if (text.size() > 512U) break;
    }
    return text;
  }

  // The DLL names in the published import directory, in descriptor order.
  [[nodiscard]] std::vector<std::string> imported_dlls() const {
    std::vector<std::string> names;
    for (std::uint32_t i = 0;; ++i) {
      const auto record = at_rva(import_rva + i * 20U, 20);
      if (record.size() != 20) break;
      auto field = [&](std::size_t o) {
        return std::uint32_t(record[o]) | (std::uint32_t(record[o + 1]) << 8) |
               (std::uint32_t(record[o + 2]) << 16) | (std::uint32_t(record[o + 3]) << 24);
      };
      const auto name = field(12), first_thunk = field(16);
      if (name == 0 && first_thunk == 0) break;
      names.push_back(string_at_rva(name));
    }
    return names;
  }

  [[nodiscard]] std::vector<unsigned char> at_rva(std::uint32_t rva, std::size_t count) const {
    for (const auto& s : sections) {
      if (rva < s.rva || rva >= s.rva + std::max(s.virtual_size, s.raw_size)) continue;
      const std::uint32_t offset = rva - s.rva;
      if (offset + count > s.raw_size) return {};
      const std::size_t begin = s.raw_pointer + offset;
      if (begin + count > bytes.size()) return {};
      return {bytes.begin() + static_cast<std::ptrdiff_t>(begin),
              bytes.begin() + static_cast<std::ptrdiff_t>(begin + count)};
    }
    return {};
  }
};

// Reads `path` and checks it against the PE32+ rules the loader enforces.
// On failure `error` names the first rule that was broken.
inline PeImage validate_pe(const std::filesystem::path& path) {
  PeImage image;
  {
    std::ifstream in(path, std::ios::binary);
    if (!in) { image.error = "cannot open image: " + path.string(); return image; }
    image.bytes.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
  }
  const auto& b = image.bytes;
  auto fail = [&](std::string message) { image.error = std::move(message); return image; };
  auto u16 = [&](std::size_t o) -> std::uint32_t {
    return std::uint32_t(b[o]) | (std::uint32_t(b[o + 1]) << 8);
  };
  auto u32 = [&](std::size_t o) -> std::uint32_t {
    return std::uint32_t(b[o]) | (std::uint32_t(b[o + 1]) << 8) |
           (std::uint32_t(b[o + 2]) << 16) | (std::uint32_t(b[o + 3]) << 24);
  };

  if (b.size() < 0x40U) return fail("image is smaller than a DOS header");
  if (b[0] != 'M' || b[1] != 'Z') return fail("missing MZ signature");
  const std::uint32_t pe = u32(0x3C);
  if (pe + 24U > b.size()) return fail("e_lfanew points past the end of the file");
  if (u32(pe) != 0x00004550U) return fail("missing PE signature");
  if (u16(pe + 4U) != 0x8664U) return fail("machine is not AMD64");

  image.section_count = u16(pe + 6U);
  const std::uint32_t optional_size = u16(pe + 24U - 4U);
  image.file_characteristics = static_cast<std::uint16_t>(u16(pe + 24U - 2U));
  const std::size_t opt = pe + 24U;
  if (opt + optional_size > b.size()) return fail("optional header is truncated");
  if (optional_size < 112U) return fail("optional header is too small for PE32+");
  if (u16(opt) != 0x20BU) return fail("optional header is not PE32+");

  image.entry_rva = u32(opt + 16U);
  const std::uint32_t section_alignment = u32(opt + 32U);
  const std::uint32_t file_alignment = u32(opt + 36U);
  image.size_of_image = u32(opt + 56U);
  image.size_of_headers = u32(opt + 60U);
  image.subsystem = static_cast<std::uint16_t>(u16(opt + 68U));
  image.dll_characteristics = static_cast<std::uint16_t>(u16(opt + 70U));
  const std::uint32_t directory_count = u32(opt + 108U);

  if (section_alignment == 0U || file_alignment == 0U) return fail("zero alignment in optional header");
  if (section_alignment < file_alignment) return fail("section alignment is below file alignment");
  if (image.size_of_headers % file_alignment != 0U)
    return fail("SizeOfHeaders is not a multiple of FileAlignment");
  if (image.size_of_image % section_alignment != 0U)
    return fail("SizeOfImage is not a multiple of SectionAlignment");
  if (directory_count > 16U) return fail("more than 16 data directories");
  if (directory_count >= 6U) image.basereloc_rva = u32(opt + 112U + 5U * 8U);
  if (directory_count >= 2U) {
    image.import_rva = u32(opt + 112U + 1U * 8U);
    image.import_size = u32(opt + 112U + 1U * 8U + 4U);
  }

  // The loader requires RELOCS_STRIPPED whenever an image cannot be moved, and
  // rejects DYNAMIC_BASE images that carry no relocation table in some
  // configurations. Keep the two consistent.
  const bool relocs_stripped = (image.file_characteristics & 0x0001U) != 0U;
  const bool dynamic_base = (image.dll_characteristics & 0x0040U) != 0U;
  if (relocs_stripped && dynamic_base)
    return fail("image claims both RELOCS_STRIPPED and DYNAMIC_BASE");
  if (dynamic_base && image.basereloc_rva == 0U)
    return fail("DYNAMIC_BASE image has no base relocation directory");

  const std::size_t table = opt + optional_size;
  if (table + std::size_t(image.section_count) * 40U > b.size())
    return fail("section table is truncated");
  if (table + std::size_t(image.section_count) * 40U > image.size_of_headers)
    return fail("section table does not fit inside SizeOfHeaders");

  std::uint32_t expected_rva = image.size_of_headers;
  if (expected_rva % section_alignment != 0U)
    expected_rva += section_alignment - (expected_rva % section_alignment);
  for (std::uint32_t i = 0; i < image.section_count; ++i) {
    const std::size_t row = table + std::size_t(i) * 40U;
    std::string name;
    for (std::size_t c = 0; c < 8U && b[row + c] != 0; ++c) name.push_back(char(b[row + c]));
    const std::uint32_t virtual_size = u32(row + 8U);
    const std::uint32_t virtual_address = u32(row + 12U);
    const std::uint32_t raw_size = u32(row + 16U);
    const std::uint32_t raw_pointer = u32(row + 20U);

    if (virtual_size == 0U && raw_size == 0U)
      return fail("section " + name + " occupies no image or file space");
    if (virtual_address != expected_rva)
      return fail("section " + name + " is not contiguous with the previous section");
    if (raw_size != 0U) {
      if (raw_size % file_alignment != 0U)
        return fail("section " + name + " raw size is not file-aligned");
      if (raw_pointer % file_alignment != 0U)
        return fail("section " + name + " raw pointer is not file-aligned");
      if (std::size_t(raw_pointer) + raw_size > b.size())
        return fail("section " + name + " raw data runs past the end of the file");
    }
    image.sections.push_back({name, virtual_size, virtual_address, raw_size, raw_pointer,
                              u32(row + 36U)});
    const std::uint32_t span = virtual_size != 0U ? virtual_size : raw_size;
    expected_rva = virtual_address + span;
    if (expected_rva % section_alignment != 0U)
      expected_rva += section_alignment - (expected_rva % section_alignment);
    if (expected_rva > image.size_of_image)
      return fail("section " + name + " extends past SizeOfImage");
  }
  if (expected_rva != image.size_of_image)
    return fail("SizeOfImage does not match the end of the last section");
  if (image.entry_rva == 0U) return fail("image has no entry point");
  if (image.entry_rva >= image.size_of_image) return fail("entry point lies outside the image");
  return image;
}

// Runs a linked image and returns its exit status, or -1 when this host cannot
// execute PE files. A structurally valid image that the loader still refuses
// shows up here and nowhere else.
inline int run_pe(const std::filesystem::path& path) {
#if defined(_WIN32)
  const std::string command = "\"" + path.string() + "\"";
  return std::system(command.c_str());
#else
  (void)path;
  return -1;
#endif
}

} // namespace oblink::testing
