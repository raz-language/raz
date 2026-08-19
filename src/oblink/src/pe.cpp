// Copyright 2026 Mario Vinciguerra
// SPDX-License-Identifier: Apache-2.0

#include "oblink/pe.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <stdexcept>
#include <type_traits>

namespace oblink::pe {
namespace {

class Writer {
public:
  template <typename T> void integer(T value) {
    using U = std::make_unsigned_t<T>;
    U bits = static_cast<U>(value);
    for (std::size_t i = 0; i < sizeof(T); ++i)
      bytes_.push_back(static_cast<std::byte>((bits >> (i * 8U)) & 0xffU));
  }
  void raw(std::span<const std::byte> bytes) { bytes_.insert(bytes_.end(), bytes.begin(), bytes.end()); }
  void zeros(std::size_t count) { bytes_.insert(bytes_.end(), count, std::byte{0}); }
  void ascii(std::string_view text, std::size_t width) {
    for (std::size_t i = 0; i < width; ++i)
      bytes_.push_back(i < text.size() ? static_cast<std::byte>(static_cast<unsigned char>(text[i])) : std::byte{0});
  }
  [[nodiscard]] std::size_t size() const { return bytes_.size(); }
  std::vector<std::byte> take() && { return std::move(bytes_); }
private:
  std::vector<std::byte> bytes_;
};

std::uint32_t align_up(std::uint32_t value, std::uint32_t alignment) {
  if (alignment == 0U) throw std::runtime_error("PE alignment must not be zero");
  return (value + alignment - 1U) / alignment * alignment;
}

} // namespace

std::vector<std::byte> build_pe32_plus(std::span<const SectionImage> sections,
                                       const ImageOptions& options) {
  if (sections.empty()) throw std::runtime_error("PE image requires at least one section");
  if (sections.size() > 96) throw std::runtime_error("too many PE sections");
  constexpr std::uint32_t pe_offset = 0x80;
  constexpr std::uint16_t optional_size = 0xF0;
  const std::uint32_t headers_unaligned = pe_offset + 4U + 20U + optional_size +
                                          static_cast<std::uint32_t>(sections.size()) * 40U;
  const std::uint32_t headers_size = align_up(headers_unaligned, options.file_alignment);

  struct Row { const SectionImage* section{}; std::uint32_t raw{}; std::uint32_t raw_size{}; };
  std::vector<Row> rows;
  rows.reserve(sections.size());
  std::uint32_t raw_cursor = headers_size;
  std::uint32_t image_end = align_up(headers_size, options.section_alignment);
  std::uint32_t size_code = 0, size_init = 0, size_uninit = 0, base_code = 0;
  for (const auto& section : sections) {
    const auto raw_size = section.data.empty() ? 0U : align_up(static_cast<std::uint32_t>(section.data.size()), options.file_alignment);
    rows.push_back({&section, raw_size == 0U ? 0U : raw_cursor, raw_size});
    raw_cursor += raw_size;
    const auto virtual_size = std::max(section.virtual_size, static_cast<std::uint32_t>(section.data.size()));
    image_end = std::max(image_end, align_up(section.rva + virtual_size, options.section_alignment));
    if ((section.characteristics & 0x00000020U) != 0U) { size_code += raw_size; if (base_code == 0) base_code = section.rva; }
    if ((section.characteristics & 0x00000040U) != 0U) size_init += raw_size;
    if ((section.characteristics & 0x00000080U) != 0U) size_uninit += align_up(virtual_size, options.section_alignment);
  }

  Writer w;
  // DOS header + stub. Only e_lfanew is semantically required, but a conventional header keeps tools happy.
  w.integer<std::uint16_t>(0x5A4D); // MZ
  w.zeros(58);
  w.integer<std::uint32_t>(pe_offset);
  w.zeros(pe_offset - static_cast<std::uint32_t>(w.size()));
  w.integer<std::uint32_t>(0x00004550); // PE\0\0
  w.integer<std::uint16_t>(0x8664);
  w.integer<std::uint16_t>(static_cast<std::uint16_t>(sections.size()));
  w.integer<std::uint32_t>(options.deterministic ? 0U : 0U);
  w.integer<std::uint32_t>(0U);
  w.integer<std::uint32_t>(0U);
  w.integer<std::uint16_t>(optional_size);
  // An image with no base relocation table cannot be moved, and the loader
  // expects that to be stated: RELOCS_STRIPPED in the file header, and no
  // ASLR bits in DllCharacteristics. Claiming relocatability without a .reloc
  // is the inconsistency that makes tools and hardening policies reject an
  // otherwise well-formed image.
  const bool relocatable = options.basereloc_size != 0U;
  w.integer<std::uint16_t>(static_cast<std::uint16_t>(relocatable ? 0x0022U : 0x0023U));

  w.integer<std::uint16_t>(0x20B); // PE32+
  w.integer<std::uint8_t>(1); w.integer<std::uint8_t>(0);
  w.integer<std::uint32_t>(size_code);
  w.integer<std::uint32_t>(size_init);
  w.integer<std::uint32_t>(size_uninit);
  w.integer<std::uint32_t>(options.entry_rva);
  w.integer<std::uint32_t>(base_code);
  w.integer<std::uint64_t>(options.image_base);
  w.integer<std::uint32_t>(options.section_alignment);
  w.integer<std::uint32_t>(options.file_alignment);
  w.integer<std::uint16_t>(6); w.integer<std::uint16_t>(0); // OS
  w.integer<std::uint16_t>(0); w.integer<std::uint16_t>(0); // image
  w.integer<std::uint16_t>(6); w.integer<std::uint16_t>(0); // subsystem
  w.integer<std::uint32_t>(0);
  w.integer<std::uint32_t>(image_end);
  w.integer<std::uint32_t>(headers_size);
  w.integer<std::uint32_t>(0); // checksum
  w.integer<std::uint16_t>(options.subsystem);
  // NX and terminal-server-aware always; dynamic base and high-entropy ASLR
  // only when the image carries the relocations that make rebasing possible.
  w.integer<std::uint16_t>(static_cast<std::uint16_t>(relocatable ? 0x8160U : 0x8100U));
  w.integer<std::uint64_t>(options.stack_reserve); w.integer<std::uint64_t>(options.stack_commit);
  w.integer<std::uint64_t>(options.heap_reserve); w.integer<std::uint64_t>(options.heap_commit);
  w.integer<std::uint32_t>(0);
  w.integer<std::uint32_t>(16); // directories
  // IMAGE_DIRECTORY_ENTRY_EXPORT
  w.integer<std::uint32_t>(0U); w.integer<std::uint32_t>(0U);
  // IMAGE_DIRECTORY_ENTRY_IMPORT
  w.integer<std::uint32_t>(options.import_rva); w.integer<std::uint32_t>(options.import_size);
  // RESOURCE
  w.integer<std::uint32_t>(0U); w.integer<std::uint32_t>(0U);
  // EXCEPTION (.pdata on AMD64)
  w.integer<std::uint32_t>(options.exception_rva); w.integer<std::uint32_t>(options.exception_size);
  // SECURITY
  w.integer<std::uint32_t>(0U); w.integer<std::uint32_t>(0U);
  // BASE RELOCATION (.reloc)
  w.integer<std::uint32_t>(options.basereloc_rva); w.integer<std::uint32_t>(options.basereloc_size);
  // DEBUG, ARCHITECTURE, GLOBALPTR
  for (int i = 0; i < 3; ++i) { w.integer<std::uint32_t>(0U); w.integer<std::uint32_t>(0U); }
  // TLS
  w.integer<std::uint32_t>(options.tls_rva); w.integer<std::uint32_t>(options.tls_size);
  // LOAD_CONFIG, BOUND_IMPORT
  for (int i = 0; i < 2; ++i) { w.integer<std::uint32_t>(0U); w.integer<std::uint32_t>(0U); }
  // IMAGE_DIRECTORY_ENTRY_IAT
  w.integer<std::uint32_t>(options.iat_rva); w.integer<std::uint32_t>(options.iat_size);
  // DELAY_IMPORT, COM_DESCRIPTOR, reserved
  for (int i = 0; i < 3; ++i) { w.integer<std::uint32_t>(0U); w.integer<std::uint32_t>(0U); }

  for (std::size_t i = 0; i < sections.size(); ++i) {
    const auto& s = sections[i];
    const auto& row = rows[i];
    w.ascii(s.name, 8);
    w.integer<std::uint32_t>(std::max(s.virtual_size, static_cast<std::uint32_t>(s.data.size())));
    w.integer<std::uint32_t>(s.rva);
    w.integer<std::uint32_t>(row.raw_size);
    w.integer<std::uint32_t>(row.raw);
    w.integer<std::uint32_t>(0); w.integer<std::uint32_t>(0);
    w.integer<std::uint16_t>(0); w.integer<std::uint16_t>(0);
    w.integer<std::uint32_t>(s.characteristics);
  }
  if (w.size() > headers_size) throw std::runtime_error("PE headers exceed file alignment reservation");
  w.zeros(headers_size - w.size());
  for (std::size_t i = 0; i < sections.size(); ++i) {
    const auto& row = rows[i];
    if (row.raw_size == 0) continue;
    w.raw(row.section->data);
    w.zeros(row.raw_size - row.section->data.size());
  }
  return std::move(w).take();
}

} // namespace oblink::pe
