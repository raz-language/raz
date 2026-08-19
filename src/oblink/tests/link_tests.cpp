// Copyright 2026 Mario Vinciguerra
// SPDX-License-Identifier: Apache-2.0

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
#include "oblink/link.hpp"
#include "oblink/coff.hpp"
#include "pe_validate.hpp"

// Every successful link in this file must produce an image the Windows loader
// would accept. Checking only for "MZ" let a build ship in which each output
// carried two zero-length sections at the same RVA -- a file that is obviously
// a PE and that no Windows machine will run.
static bool valid_image(const std::filesystem::path& path, const char* what) {
  const auto image = oblink::testing::validate_pe(path);
  if (image.ok()) return true;
  std::cerr << what << ": " << image.error << '\n';
  return false;
}

static void u16(std::vector<std::byte>& b, std::uint16_t v){b.push_back(std::byte(v&255));b.push_back(std::byte(v>>8));}
static void u32(std::vector<std::byte>& b, std::uint32_t v){for(int i=0;i<4;++i)b.push_back(std::byte((v>>(8*i))&255));}
static void be32(std::vector<std::byte>& b, std::uint32_t v){for(int i=3;i>=0;--i)b.push_back(std::byte((v>>(8*i))&255));}
static std::vector<std::byte> object_with_symbol(std::string symbol, bool define, std::string ref = {}, bool comdat = false) {
  std::vector<std::byte> b;
  const std::uint16_t reloc_count = ref.empty() ? 0 : 1;
  const std::uint32_t raw_off = 60, raw_size = ref.empty() ? 1 : 5;
  const std::uint32_t reloc_off = raw_off + raw_size;
  const std::uint32_t sym_off = reloc_off + reloc_count * 10U;
  const std::uint32_t sym_count = ref.empty() ? 1 : 2;
  u16(b,0x8664); u16(b,1); u32(b,0); u32(b,sym_off); u32(b,sym_count); u16(b,0); u16(b,0);
  for(char c: std::string(".text")) b.push_back(std::byte(c)); while(b.size()<28)b.push_back(std::byte{0});
  u32(b,0); u32(b,0); u32(b,raw_size); u32(b,raw_off); u32(b,reloc_count?reloc_off:0); u32(b,0); u16(b,reloc_count); u16(b,0); u32(b,0x60000020U | (comdat ? 0x00001000U : 0U));
  if (ref.empty()) b.push_back(std::byte{0xC3});
  else { b.push_back(std::byte{0xE8}); u32(b,0); u32(b,1); u32(b,1); u16(b,0x0004); }
  auto sym=[&](const std::string& n,std::int16_t sec){ for(size_t i=0;i<8;++i)b.push_back(i<n.size()?std::byte(n[i]):std::byte{0}); u32(b,0); u16(b,(std::uint16_t)sec); u16(b,0x20); b.push_back(std::byte{2}); b.push_back(std::byte{0}); };
  sym(symbol, define?1:0); if(!ref.empty()) sym(ref,0); u32(b,4); return b;
}

static std::vector<std::byte> object_with_long_symbol_ref(std::string symbol, std::string ref) {
  std::vector<std::byte> b;
  const std::uint32_t raw_off=60U, raw_size=5U, reloc_off=65U, sym_off=75U, sym_count=2U;
  u16(b,0x8664);u16(b,1);u32(b,0);u32(b,sym_off);u32(b,sym_count);u16(b,0);u16(b,0);
  for(char c:std::string(".text"))b.push_back(std::byte(c));while(b.size()<28)b.push_back(std::byte{0});
  u32(b,0);u32(b,0);u32(b,raw_size);u32(b,raw_off);u32(b,reloc_off);u32(b,0);u16(b,1);u16(b,0);u32(b,0x60000020U);
  b.push_back(std::byte{0xE8});u32(b,0);u32(b,1);u32(b,1);u16(b,0x0004);
  std::vector<std::string> long_names;
  auto name_entry=[&](const std::string& n){
    if(n.size()<=8U){for(size_t i=0;i<8;++i)b.push_back(i<n.size()?std::byte(n[i]):std::byte{0});return;}
    std::uint32_t off=4U;for(const auto& prior:long_names)off+=static_cast<std::uint32_t>(prior.size()+1U);
    u32(b,0);u32(b,off);long_names.push_back(n);
  };
  auto sym=[&](const std::string& n,std::int16_t sec){name_entry(n);u32(b,0);u16(b,(std::uint16_t)sec);u16(b,0x20);b.push_back(std::byte{2});b.push_back(std::byte{0});};
  sym(symbol,1);sym(ref,0);
  std::uint32_t string_size=4U;for(const auto& n:long_names)string_size+=static_cast<std::uint32_t>(n.size()+1U);u32(b,string_size);
  for(const auto& n:long_names){for(char c:n)b.push_back(std::byte(c));b.push_back(std::byte{0});}
  return b;
}

static std::vector<std::byte> object_with_comdat_data(std::string symbol, std::vector<std::byte> payload,
                                                       std::uint8_t selection, bool mark_comdat = true) {
  std::vector<std::byte> b;
  const std::uint32_t raw_off=60U;
  const std::uint32_t sym_off=raw_off+static_cast<std::uint32_t>(payload.size());
  // section symbol + its auxiliary section-definition record + public external
  u16(b,0x8664);u16(b,1);u32(b,0);u32(b,sym_off);u32(b,3);u16(b,0);u16(b,0);
  for(char c:std::string(".rdata"))b.push_back(std::byte(c));while(b.size()<28)b.push_back(std::byte{0});
  u32(b,0);u32(b,0);u32(b,static_cast<std::uint32_t>(payload.size()));u32(b,raw_off);
  u32(b,0);u32(b,0);u16(b,0);u16(b,0);u32(b,0x40000040U|(mark_comdat?0x00001000U:0U));
  b.insert(b.end(),payload.begin(),payload.end());
  auto name8=[&](const std::string& n){for(size_t i=0;i<8;++i)b.push_back(i<n.size()?std::byte(n[i]):std::byte{0});};
  // IMAGE_SYM_CLASS_STATIC section symbol with one IMAGE_AUX_SYMBOL_SECTION record.
  name8(".rdata");u32(b,0);u16(b,1);u16(b,0);b.push_back(std::byte{3});b.push_back(std::byte{1});
  u32(b,static_cast<std::uint32_t>(payload.size()));u16(b,0);u16(b,0);u32(b,0);
  u16(b,0);b.push_back(std::byte(selection));b.push_back(std::byte{0});u16(b,0);
  if(symbol.size()<=8U) name8(symbol); else { u32(b,0);u32(b,4); }
  u32(b,0);u16(b,1);u16(b,0);b.push_back(std::byte{2});b.push_back(std::byte{0});
  if(symbol.size()<=8U) u32(b,4);
  else { u32(b,static_cast<std::uint32_t>(4U+symbol.size()+1U));for(char c:symbol)b.push_back(std::byte(c));b.push_back(std::byte{0}); }
  return b;
}

static std::vector<std::byte> object_with_defaultlib(std::string symbol, std::string ref, std::string library) {
  const std::string directive = " /DEFAULTLIB:" + library;
  std::vector<std::byte> b;
  const std::uint32_t text_raw = 100U;
  const std::uint32_t text_reloc = text_raw + 5U;
  const std::uint32_t directive_raw = text_reloc + 10U;
  const std::uint32_t sym_off = directive_raw + static_cast<std::uint32_t>(directive.size());
  u16(b,0x8664); u16(b,2); u32(b,0); u32(b,sym_off); u32(b,2); u16(b,0); u16(b,0);
  auto section=[&](std::string name,std::uint32_t size,std::uint32_t raw,std::uint32_t reloc,std::uint16_t nreloc,std::uint32_t chars){
    for(size_t i=0;i<8;++i)b.push_back(i<name.size()?std::byte(name[i]):std::byte{0});
    u32(b,0);u32(b,0);u32(b,size);u32(b,raw);u32(b,reloc);u32(b,0);u16(b,nreloc);u16(b,0);u32(b,chars);
  };
  section(".text",5,text_raw,text_reloc,1,0x60000020U);
  section(".drectve",static_cast<std::uint32_t>(directive.size()),directive_raw,0,0,0x00100A00U);
  b.push_back(std::byte{0xE8}); u32(b,0);
  u32(b,1); u32(b,1); u16(b,0x0004);
  for(char c:directive)b.push_back(std::byte(c));
  auto sym=[&](const std::string& n,std::int16_t sec){for(size_t i=0;i<8;++i)b.push_back(i<n.size()?std::byte(n[i]):std::byte{0});u32(b,0);u16(b,(std::uint16_t)sec);u16(b,0x20);b.push_back(std::byte{2});b.push_back(std::byte{0});};
  sym(symbol,1); sym(ref,0); u32(b,4); return b;
}
static std::vector<std::byte> object_with_common(std::string symbol, std::uint32_t size) {
  std::vector<std::byte> b;
  const std::uint32_t raw_off=60, sym_off=61;
  u16(b,0x8664);u16(b,1);u32(b,0);u32(b,sym_off);u32(b,1);u16(b,0);u16(b,0);
  for(char c:std::string(".text"))b.push_back(std::byte(c));while(b.size()<28)b.push_back(std::byte{0});
  u32(b,0);u32(b,0);u32(b,1);u32(b,raw_off);u32(b,0);u32(b,0);u16(b,0);u16(b,0);u32(b,0x60000020U);
  b.push_back(std::byte{0xC3});
  for(size_t i=0;i<8;++i)b.push_back(i<symbol.size()?std::byte(symbol[i]):std::byte{0});u32(b,size);u16(b,0);u16(b,0);b.push_back(std::byte{2});b.push_back(std::byte{0});u32(b,4);return b;
}
// A .text section plus an uninitialized .bss section holding one symbol. The
// .bss header reports its extent in SizeOfRawData with PointerToRawData zero,
// which is how every real COFF producer emits uninitialized data.
static std::vector<std::byte> object_with_bss(std::string symbol, std::uint32_t size) {
  std::vector<std::byte> b;
  const std::uint32_t text_raw=100U;
  const std::uint32_t sym_off=text_raw+1U;
  u16(b,0x8664);u16(b,2);u32(b,0);u32(b,sym_off);u32(b,1);u16(b,0);u16(b,0);
  auto section=[&](std::string name,std::uint32_t raw_size,std::uint32_t raw,std::uint32_t chars){
    for(size_t i=0;i<8;++i)b.push_back(i<name.size()?std::byte(name[i]):std::byte{0});
    u32(b,0);u32(b,0);u32(b,raw_size);u32(b,raw);u32(b,0);u32(b,0);u16(b,0);u16(b,0);u32(b,chars);
  };
  section(".text",1,text_raw,0x60000020U);
  section(".bss",size,0,0xC0000080U);
  b.push_back(std::byte{0xC3});
  for(size_t i=0;i<8;++i)b.push_back(i<symbol.size()?std::byte(symbol[i]):std::byte{0});
  u32(b,0);u16(b,2);u16(b,0);b.push_back(std::byte{2});b.push_back(std::byte{0});u32(b,4);
  return b;
}

// A COMDAT .text$mn defining `symbol`, whose body is `filler` bytes of 0x90
// followed by `ret`. Two objects built with different fillers give the
// prevailing definition a distinguishable size.
static std::vector<std::byte> object_with_comdat_code(std::string symbol, std::uint32_t filler) {
  std::vector<std::byte> b;
  const std::uint32_t raw_off=60U, raw_size=filler+1U;
  const std::uint32_t sym_off=raw_off+raw_size;
  u16(b,0x8664);u16(b,1);u32(b,0);u32(b,sym_off);u32(b,3);u16(b,0);u16(b,0);
  for(char c:std::string(".text$mn"))b.push_back(std::byte(c));
  u32(b,0);u32(b,0);u32(b,raw_size);u32(b,raw_off);u32(b,0);u32(b,0);u16(b,0);u16(b,0);u32(b,0x60001020U);
  for(std::uint32_t i=0;i<filler;++i)b.push_back(std::byte{0x90});
  b.push_back(std::byte{0xC3});
  auto name8=[&](const std::string& n){for(size_t i=0;i<8;++i)b.push_back(i<n.size()?std::byte(n[i]):std::byte{0});};
  // Static section symbol carrying the section-definition auxiliary record.
  name8(".text$mn");u32(b,0);u16(b,1);u16(b,0);b.push_back(std::byte{3});b.push_back(std::byte{1});
  u32(b,raw_size);u16(b,0);u16(b,0);u32(b,0);
  u16(b,0);b.push_back(std::byte(oblink::coff::comdat_select_any));b.push_back(std::byte{0});u16(b,0);
  name8(symbol);u32(b,0);u16(b,1);u16(b,0x20);b.push_back(std::byte{2});b.push_back(std::byte{0});
  u32(b,4);
  return b;
}

// A COMDAT .text$mn defining `symbol`, plus an ordinary .text section defining
// `caller` that calls it. When another object's copy of the COMDAT prevails,
// this object's definition is discarded and `caller` -- which survives -- is
// left holding a relocation against a section that is no longer in the image.
static std::vector<std::byte> object_with_discarded_comdat_reference(std::string symbol, std::string caller,
                                                                     std::uint32_t filler) {
  std::vector<std::byte> b;
  const std::uint32_t comdat_raw=100U, comdat_size=filler+1U;
  const std::uint32_t caller_raw=comdat_raw+comdat_size, caller_size=5U;
  const std::uint32_t reloc_off=caller_raw+caller_size;
  const std::uint32_t sym_off=reloc_off+10U;
  u16(b,0x8664);u16(b,2);u32(b,0);u32(b,sym_off);u32(b,4);u16(b,0);u16(b,0);
  auto section=[&](std::string name,std::uint32_t size,std::uint32_t raw,std::uint32_t reloc,
                   std::uint16_t nreloc,std::uint32_t chars){
    for(size_t i=0;i<8;++i)b.push_back(i<name.size()?std::byte(name[i]):std::byte{0});
    u32(b,0);u32(b,0);u32(b,size);u32(b,raw);u32(b,reloc);u32(b,0);u16(b,nreloc);u16(b,0);u32(b,chars);
  };
  section(".text$mn",comdat_size,comdat_raw,0,0,0x60001020U);
  section(".text",caller_size,caller_raw,reloc_off,1,0x60000020U);
  for(std::uint32_t i=0;i<filler;++i)b.push_back(std::byte{0x90});
  b.push_back(std::byte{0xC3});
  b.push_back(std::byte{0xE8});u32(b,0);
  // The relocation names symbol index 2, the COMDAT's external definition.
  u32(b,1);u32(b,2);u16(b,0x0004);
  auto name8=[&](const std::string& n){for(size_t i=0;i<8;++i)b.push_back(i<n.size()?std::byte(n[i]):std::byte{0});};
  name8(".text$mn");u32(b,0);u16(b,1);u16(b,0);b.push_back(std::byte{3});b.push_back(std::byte{1});
  u32(b,comdat_size);u16(b,0);u16(b,0);u32(b,0);
  u16(b,0);b.push_back(std::byte(oblink::coff::comdat_select_any));b.push_back(std::byte{0});u16(b,0);
  name8(symbol);u32(b,0);u16(b,1);u16(b,0x20);b.push_back(std::byte{2});b.push_back(std::byte{0});
  name8(caller);u32(b,0);u16(b,2);u16(b,0x20);b.push_back(std::byte{2});b.push_back(std::byte{0});
  u32(b,4);
  return b;
}

// Calls an imported symbol and also contributes its own .idata$5 section, the
// way MinGW-style import libraries do. The synthesized import descriptors have
// to coexist with that contribution instead of landing in it.
static std::vector<std::byte> object_with_idata_contribution(std::string symbol, std::string ref) {
  std::vector<std::byte> b;
  const std::uint32_t text_raw=100U, text_size=5U;
  const std::uint32_t text_reloc=text_raw+text_size;
  const std::uint32_t idata_raw=text_reloc+10U, idata_size=8U;
  const std::uint32_t sym_off=idata_raw+idata_size;
  u16(b,0x8664);u16(b,2);u32(b,0);u32(b,sym_off);u32(b,2);u16(b,0);u16(b,0);
  auto section=[&](std::string name,std::uint32_t size,std::uint32_t raw,std::uint32_t reloc,
                   std::uint16_t nreloc,std::uint32_t chars){
    for(size_t i=0;i<8;++i)b.push_back(i<name.size()?std::byte(name[i]):std::byte{0});
    u32(b,0);u32(b,0);u32(b,size);u32(b,raw);u32(b,reloc);u32(b,0);u16(b,nreloc);u16(b,0);u32(b,chars);
  };
  section(".text",text_size,text_raw,text_reloc,1,0x60000020U);
  section(".idata$5",idata_size,idata_raw,0,0,0xC0000040U);
  b.push_back(std::byte{0xE8});u32(b,0);
  u32(b,1);u32(b,1);u16(b,0x0004);
  for(std::uint32_t i=0;i<idata_size;++i)b.push_back(std::byte{0});
  auto sym=[&](const std::string& n,std::int16_t sec){for(size_t i=0;i<8;++i)b.push_back(i<n.size()?std::byte(n[i]):std::byte{0});u32(b,0);u16(b,(std::uint16_t)sec);u16(b,0x20);b.push_back(std::byte{2});b.push_back(std::byte{0});};
  sym(symbol,1);sym(ref,0);u32(b,4);
  return b;
}

// The three member shapes dlltool emits for a GNU import library. The stub
// names its DLL only indirectly: stub -> `_head_<dll>` -> the descriptor's Name
// relocation -> `<dll>_iname` -> the tail's `.idata$7` string.
static std::vector<std::byte> mingw_import_head(std::string head_symbol, std::string iname_symbol) {
  std::vector<std::byte> b;
  const std::uint32_t desc_raw=100U, desc_size=20U;
  const std::uint32_t reloc_off=desc_raw+desc_size;
  const std::uint32_t sym_off=reloc_off+10U;
  u16(b,0x8664);u16(b,2);u32(b,0);u32(b,sym_off);u32(b,2);u16(b,0);u16(b,0);
  auto section=[&](std::string name,std::uint32_t size,std::uint32_t raw,std::uint32_t reloc,
                   std::uint16_t nreloc,std::uint32_t chars){
    for(size_t i=0;i<8;++i)b.push_back(i<name.size()?std::byte(name[i]):std::byte{0});
    u32(b,0);u32(b,0);u32(b,size);u32(b,raw);u32(b,reloc);u32(b,0);u16(b,nreloc);u16(b,0);u32(b,chars);
  };
  section(".idata$2",desc_size,desc_raw,reloc_off,1,0xC0000040U);
  section(".idata$5",0,0,0,0,0xC0000040U);
  for(std::uint32_t i=0;i<desc_size;++i)b.push_back(std::byte{0});
  // IMAGE_IMPORT_DESCRIPTOR::Name, at offset 12, names the DLL-string symbol.
  u32(b,12);u32(b,1);u16(b,0x0003);
  auto sym=[&](const std::string& n,std::int16_t sec){for(size_t i=0;i<8;++i)b.push_back(i<n.size()?std::byte(n[i]):std::byte{0});u32(b,0);u16(b,(std::uint16_t)sec);u16(b,0);b.push_back(std::byte{2});b.push_back(std::byte{0});};
  sym(head_symbol,1); sym(iname_symbol,0); u32(b,4);
  return b;
}

static std::vector<std::byte> mingw_import_tail(std::string iname_symbol, std::string dll) {
  std::vector<std::byte> b;
  const std::uint32_t name_raw=60U;
  const std::uint32_t name_size=static_cast<std::uint32_t>(dll.size()+1U);
  const std::uint32_t sym_off=name_raw+name_size;
  u16(b,0x8664);u16(b,1);u32(b,0);u32(b,sym_off);u32(b,1);u16(b,0);u16(b,0);
  for(char c:std::string(".idata$7"))b.push_back(std::byte(c));
  u32(b,0);u32(b,0);u32(b,name_size);u32(b,name_raw);u32(b,0);u32(b,0);u16(b,0);u16(b,0);u32(b,0xC0000040U);
  for(char c:dll)b.push_back(std::byte(c)); b.push_back(std::byte{0});
  for(size_t i=0;i<8;++i)b.push_back(i<iname_symbol.size()?std::byte(iname_symbol[i]):std::byte{0});
  u32(b,0);u16(b,1);u16(b,0);b.push_back(std::byte{2});b.push_back(std::byte{0});u32(b,4);
  return b;
}

static std::vector<std::byte> mingw_import_stub(std::string symbol, std::string head_symbol,
                                                std::uint16_t hint) {
  std::vector<std::byte> b;
  // Four section headers follow the 20-byte file header, so content starts at 180.
  const std::uint32_t text_raw=180U, text_size=6U;                 // jmp *__imp_symbol(%rip)
  const std::uint32_t iname_raw=text_raw+text_size, iname_size=4U; // relocation slot only
  const std::uint32_t iat_raw=iname_raw+iname_size, iat_size=8U;
  const std::uint32_t hintname_raw=iat_raw+iat_size;
  const std::uint32_t hintname_size=static_cast<std::uint32_t>(2U+symbol.size()+1U);
  const std::uint32_t text_reloc=hintname_raw+hintname_size;
  const std::uint32_t iname_reloc=text_reloc+10U;
  const std::uint32_t iat_reloc=iname_reloc+10U;
  const std::uint32_t sym_off=iat_reloc+10U;
  u16(b,0x8664);u16(b,4);u32(b,0);u32(b,sym_off);u32(b,4);u16(b,0);u16(b,0);
  auto section=[&](std::string name,std::uint32_t size,std::uint32_t raw,std::uint32_t reloc,
                   std::uint16_t nreloc,std::uint32_t chars){
    for(size_t i=0;i<8;++i)b.push_back(i<name.size()?std::byte(name[i]):std::byte{0});
    u32(b,0);u32(b,0);u32(b,size);u32(b,raw);u32(b,reloc);u32(b,0);u16(b,nreloc);u16(b,0);u32(b,chars);
  };
  section(".text",text_size,text_raw,text_reloc,1,0x60000020U);
  section(".idata$7",iname_size,iname_raw,iname_reloc,1,0xC0000040U);
  section(".idata$5",iat_size,iat_raw,iat_reloc,1,0xC0000040U);
  section(".idata$6",hintname_size,hintname_raw,0,0,0xC0000040U);
  b.push_back(std::byte{0xFF});b.push_back(std::byte{0x25});u32(b,0);
  u32(b,0);
  for(std::uint32_t i=0;i<iat_size;++i)b.push_back(std::byte{0});
  u16(b,hint); for(char c:symbol)b.push_back(std::byte(c)); b.push_back(std::byte{0});
  u32(b,2);u32(b,2);u16(b,0x0004);  // .text -> __imp_symbol (REL32)
  u32(b,0);u32(b,3);u16(b,0x0003);  // .idata$7 -> _head (ADDR32NB)
  u32(b,0);u32(b,1);u16(b,0x0003);  // .idata$5 -> .idata$6 (ADDR32NB)
  auto sym=[&](const std::string& n,std::int16_t sec,std::uint8_t storage){for(size_t i=0;i<8;++i)b.push_back(i<n.size()?std::byte(n[i]):std::byte{0});u32(b,0);u16(b,(std::uint16_t)sec);u16(b,0);b.push_back(std::byte(storage));b.push_back(std::byte{0});};
  sym(symbol,1,2);                  // 0: public entry in .text
  sym(".idata$6",4,3);              // 1: static section symbol
  sym("__imp_"+symbol,3,2);         // 2: IAT slot
  sym(head_symbol,0,2);             // 3: undefined, names the descriptor head
  u32(b,4);
  return b;
}

static std::vector<std::byte> object_with_directive(std::string symbol, std::string directive) {
  std::vector<std::byte> b;
  const std::uint32_t text_raw=100U, directive_raw=101U;
  const std::uint32_t sym_off=directive_raw+static_cast<std::uint32_t>(directive.size());
  u16(b,0x8664);u16(b,2);u32(b,0);u32(b,sym_off);u32(b,1);u16(b,0);u16(b,0);
  auto section=[&](std::string name,std::uint32_t size,std::uint32_t raw,std::uint32_t chars){for(size_t i=0;i<8;++i)b.push_back(i<name.size()?std::byte(name[i]):std::byte{0});u32(b,0);u32(b,0);u32(b,size);u32(b,raw);u32(b,0);u32(b,0);u16(b,0);u16(b,0);u32(b,chars);};
  section(".text",1,text_raw,0x60000020U);section(".drectve",static_cast<std::uint32_t>(directive.size()),directive_raw,0x00100A00U);
  b.push_back(std::byte{0xC3});for(char c:directive)b.push_back(std::byte(c));
  for(size_t i=0;i<8;++i)b.push_back(i<symbol.size()?std::byte(symbol[i]):std::byte{0});u32(b,0);u16(b,1);u16(b,0x20);b.push_back(std::byte{2});b.push_back(std::byte{0});u32(b,4);return b;
}
static std::vector<std::byte> object_with_alias(std::string symbol, std::string ref, std::string library, std::string alias_target) {
  const std::string directive=" /DEFAULTLIB:"+library+" /ALTERNATENAME:"+ref+"="+alias_target;
  std::vector<std::byte> b; const std::uint32_t text_raw=100U,text_reloc=105U,directive_raw=115U;
  const std::uint32_t sym_off=directive_raw+static_cast<std::uint32_t>(directive.size());
  u16(b,0x8664);u16(b,2);u32(b,0);u32(b,sym_off);u32(b,2);u16(b,0);u16(b,0);
  auto section=[&](std::string name,std::uint32_t size,std::uint32_t raw,std::uint32_t reloc,std::uint16_t nreloc,std::uint32_t chars){for(size_t i=0;i<8;++i)b.push_back(i<name.size()?std::byte(name[i]):std::byte{0});u32(b,0);u32(b,0);u32(b,size);u32(b,raw);u32(b,reloc);u32(b,0);u16(b,nreloc);u16(b,0);u32(b,chars);};
  section(".text",5,text_raw,text_reloc,1,0x60000020U);section(".drectve",static_cast<std::uint32_t>(directive.size()),directive_raw,0,0,0x00100A00U);
  b.push_back(std::byte{0xE8});u32(b,0);u32(b,1);u32(b,1);u16(b,0x0004);for(char c:directive)b.push_back(std::byte(c));
  auto sym=[&](const std::string& n,std::int16_t sec){for(size_t i=0;i<8;++i)b.push_back(i<n.size()?std::byte(n[i]):std::byte{0});u32(b,0);u16(b,(std::uint16_t)sec);u16(b,0x20);b.push_back(std::byte{2});b.push_back(std::byte{0});};sym(symbol,1);sym(ref,0);u32(b,4);return b;
}
static std::vector<std::byte> object_with_imagebase_pointer() {
  std::vector<std::byte> b;
  const std::uint32_t text_raw=100U, data_raw=101U, data_reloc=109U, sym_off=119U;
  u16(b,0x8664);u16(b,2);u32(b,0);u32(b,sym_off);u32(b,2);u16(b,0);u16(b,0);
  auto section=[&](std::string name,std::uint32_t size,std::uint32_t raw,std::uint32_t reloc,std::uint16_t nreloc,std::uint32_t chars){
    for(size_t i=0;i<8;++i)b.push_back(i<name.size()?std::byte(name[i]):std::byte{0});
    u32(b,0);u32(b,0);u32(b,size);u32(b,raw);u32(b,reloc);u32(b,0);u16(b,nreloc);u16(b,0);u32(b,chars);
  };
  section(".text",1,text_raw,0,0,0x60000020U);
  section(".data",8,data_raw,data_reloc,1,0xC0000040U);
  b.push_back(std::byte{0xC3}); for(int i=0;i<8;++i)b.push_back(std::byte{0});
  u32(b,0);u32(b,1);u16(b,0x0001); // ADDR64 -> __ImageBase
  for(size_t i=0;i<8;++i)b.push_back(i<4?std::byte("main"[i]):std::byte{0});
  u32(b,0);u16(b,1);u16(b,0x20);b.push_back(std::byte{2});b.push_back(std::byte{0});
  u32(b,0);u32(b,4); // long symbol name at string-table offset 4
  u32(b,0);u16(b,0);u16(b,0);b.push_back(std::byte{2});b.push_back(std::byte{0});
  const std::string name="__ImageBase";
  u32(b,static_cast<std::uint32_t>(4U+name.size()+1U));
  for(char c:name)b.push_back(std::byte(c));b.push_back(std::byte{0});
  return b;
}

static std::vector<std::byte> import_object(std::string symbol, std::string dll) {
  std::vector<std::byte> payload;
  for (char c : symbol) payload.push_back(std::byte(c)); payload.push_back(std::byte{0});
  for (char c : dll) payload.push_back(std::byte(c)); payload.push_back(std::byte{0});
  std::vector<std::byte> b;
  u16(b,0); u16(b,0xffff); u16(b,0); u16(b,0x8664); u32(b,0);
  u32(b,static_cast<std::uint32_t>(payload.size())); u16(b,0);
  // IMPORT_OBJECT_CODE (0) | IMPORT_OBJECT_NAME (1 << 2)
  u16(b,4); b.insert(b.end(),payload.begin(),payload.end()); return b;
}
static bool contains_ascii(const std::vector<char>& bytes, const std::string& needle) {
  return std::search(bytes.begin(),bytes.end(),needle.begin(),needle.end()) != bytes.end();
}
static void write_bytes(const std::filesystem::path& p,const std::vector<std::byte>& b){std::ofstream o(p,std::ios::binary);o.write(reinterpret_cast<const char*>(b.data()),b.size());}
static void ar_raw_member(std::ofstream& out, std::string name, const std::vector<std::byte>& data){
  name.resize(16,' '); std::string hdr=name; hdr += std::string(12,' ')+std::string(6,' ')+std::string(6,' ')+std::string(8,' ');
  auto size=std::to_string(data.size()); size.resize(10,' '); hdr += size; hdr += "`\n"; out.write(hdr.data(),60); out.write(reinterpret_cast<const char*>(data.data()),data.size()); if(data.size()&1) out.put('\n');
}
static void ar_member(std::ofstream& out, std::string name, const std::vector<std::byte>& data){ ar_raw_member(out,name+"/",data); }
static void write_indexed_archive(const std::filesystem::path& path, std::string symbol,
                                  std::string member_name, const std::vector<std::byte>& member,
                                  const std::vector<std::byte>& unindexed = {}) {
  const std::size_t index_size=8U+symbol.size()+1U;
  const std::uint32_t member_offset=static_cast<std::uint32_t>(8U+60U+index_size+(index_size&1U));
  std::vector<std::byte> index;be32(index,1);be32(index,member_offset);
  for(char c:symbol)index.push_back(std::byte(c));index.push_back(std::byte{0});
  std::ofstream out(path,std::ios::binary);out<<"!<arch>\n";ar_raw_member(out,"/",index);ar_member(out,member_name,member);
  if(!unindexed.empty())ar_member(out,"unused.obj",unindexed);
}
static std::uint32_t pe_entry_rva(const std::filesystem::path& path){
  return oblink::testing::validate_pe(path).entry_rva;
}
static int run(){
  const auto dir=std::filesystem::temp_directory_path()/"oblink-link-test"; std::filesystem::remove_all(dir); std::filesystem::create_directories(dir);
  const auto mainobj=dir/"main.obj", lib=dir/"runtime.lib", exe=dir/"main.exe";
  write_bytes(mainobj, object_with_symbol("main", true, "helper"));
  {std::ofstream out(lib,std::ios::binary); out<<"!<arch>\n"; ar_member(out,"helper.obj",object_with_symbol("helper",true)); ar_member(out,"unused.obj",object_with_symbol("unused",true,"missing"));}
  auto result=oblink::link({mainobj,lib},{.output=exe}); if(!result.ok()){for(auto& d:result.diagnostics)std::cerr<<d.message<<'\n';return 1;}
  if(!valid_image(exe,"single-object link")) return 2;

  // Archive resolution must be lazy per symbol. If two members both define
  // helper, only the first provider should be extracted; eagerly loading both
  // creates a false duplicate and diverges from link.exe/lld-link semantics.
  const auto lazy_main=dir/"lazy-main.obj", lazy_lib=dir/"lazy.lib", lazy_exe=dir/"lazy.exe";
  write_bytes(lazy_main, object_with_symbol("main", true, "helper"));
  {std::ofstream out(lazy_lib,std::ios::binary); out<<"!<arch>\n";
   ar_member(out,"first.obj",object_with_symbol("helper",true));
   ar_member(out,"second.obj",object_with_symbol("helper",true,"must_not_be_loaded"));}
  auto lazy_result=oblink::link({lazy_main,lazy_lib},{.output=lazy_exe});
  if(!lazy_result.ok()){for(auto& d:lazy_result.diagnostics)std::cerr<<d.message<<'\n';return 12;}
  if(!valid_image(lazy_exe,"lazy archive link")) return 20;

  // Real COFF libraries carry a linker symbol index. The unused member below is
  // deliberately not a valid COFF object; indexed lazy extraction must never
  // parse it while resolving helper.
  const auto idx_main=dir/"indexed-main.obj", idx_lib=dir/"indexed.lib", idx_exe=dir/"indexed.exe";
  write_bytes(idx_main,object_with_symbol("main",true,"helper"));
  write_indexed_archive(idx_lib,"helper","helper.obj",object_with_symbol("helper",true),
                        {std::byte{0x13},std::byte{0x37},std::byte{0x42}});
  auto indexed_result=oblink::link({idx_main,idx_lib},{.output=idx_exe});
  if(!indexed_result.ok()){for(auto& d:indexed_result.diagnostics)std::cerr<<d.message<<'\n';return 14;}
  if(!valid_image(idx_exe,"indexed archive link")) return 21;

  // If a CRT startup symbol exists in the selected libraries, it—not main—is
  // the PE loader entry. This keeps static initialization and CRT setup intact.
  const auto crt_main=dir/"crt-main.obj", crt_lib=dir/"crt.lib", crt_exe=dir/"crt.exe";
  write_bytes(crt_main,object_with_symbol("main",true));
  write_indexed_archive(crt_lib,"mainCRTStartup","startup.obj",
                        object_with_long_symbol_ref("mainCRTStartup","main"));
  auto crt_result=oblink::link({crt_main,crt_lib},{.output=crt_exe});
  if(!crt_result.ok()){for(auto& d:crt_result.diagnostics)std::cerr<<d.message<<'\n';return 15;}
  if(!valid_image(crt_exe,"CRT startup link")) return 22;
  if(pe_entry_rva(crt_exe)==0x1000U) return 16;

  // __ImageBase is linker-synthesized on Windows and is referenced by real CRT
  // and C++ objects. It must resolve without an input definition.
  const auto imagebase_obj=dir/"imagebase.obj", imagebase_exe=dir/"imagebase.exe";
  write_bytes(imagebase_obj,object_with_imagebase_pointer());
  auto imagebase_result=oblink::link({imagebase_obj},{.output=imagebase_exe});
  if(!imagebase_result.ok()){for(auto& d:imagebase_result.diagnostics)std::cerr<<d.message<<'\n';return 17;}
  if(!valid_image(imagebase_exe,"__ImageBase link")) return 23;
  const auto c1=dir/"comdat1.obj", c2=dir/"comdat2.obj", cexe=dir/"comdat.exe";
  write_bytes(c1, object_with_symbol("main", true, {}, true));
  write_bytes(c2, object_with_symbol("main", true, {}, true));
  auto comdat_result=oblink::link({c1,c2},{.output=cexe});
  if(!comdat_result.ok()){for(auto& d:comdat_result.diagnostics)std::cerr<<d.message<<'\n';return 3;}
  if(!valid_image(cexe,"code COMDAT link")) return 24;

  // MSVC's STL/runtime uses data COMDATs (for example std::nothrow).  Exercise
  // the authoritative section-definition Selection byte, including a fixture
  // without IMAGE_SCN_LNK_COMDAT so parser metadata—not a coarse flag test—is
  // what makes the duplicate legal.
  const auto dmain=dir/"data-main.obj", d1=dir/"data1.obj", d2=dir/"data2.obj", dexe=dir/"data-comdat.exe";
  const std::string nothrow="?nothrow@std@@3Unothrow_t@1@B";
  write_bytes(dmain,object_with_long_symbol_ref("main",nothrow));
  write_bytes(d1,object_with_comdat_data(nothrow,{std::byte{1},std::byte{2}},oblink::coff::comdat_select_any,false));
  write_bytes(d2,object_with_comdat_data(nothrow,{std::byte{3},std::byte{4}},oblink::coff::comdat_select_any,false));
  auto data_comdat_result=oblink::link({dmain,d1,d2},{.output=dexe});
  if(!data_comdat_result.ok()){for(auto& d:data_comdat_result.diagnostics)std::cerr<<d.message<<'\n';return 9;}
  if(!valid_image(dexe,"data COMDAT link")) return 25;

  const auto smmain=dir/"same-main.obj", sm1=dir/"same1.obj", sm2=dir/"same2.obj", smexe=dir/"same.exe";
  write_bytes(smmain,object_with_symbol("main",true));
  write_bytes(sm1,object_with_comdat_data("same",{std::byte{1}},oblink::coff::comdat_select_same_size));
  write_bytes(sm2,object_with_comdat_data("same",{std::byte{1},std::byte{2}},oblink::coff::comdat_select_same_size));
  auto same_result=oblink::link({smmain,sm1,sm2},{.output=smexe});
  if(same_result.ok()) return 10;

  // Microsoft short-import object: the referring object asks for __imp_f while
  // the import member names public symbol f in kernel32.dll. ObLink must synthesize
  // the IAT slot and PE import directory rather than treating the member as COFF.
  const auto impmain=dir/"impmain.obj", implib=dir/"kernel.lib", impexe=dir/"import.exe";
  write_bytes(impmain, object_with_symbol("main", true, "__imp_f"));
  {std::ofstream out(implib,std::ios::binary); out<<"!<arch>\n"; ar_member(out,"f.imp",import_object("f","kernel32.dll"));}
  auto import_result=oblink::link({impmain,implib},{.output=impexe});
  if(!import_result.ok()){for(auto& d:import_result.diagnostics)std::cerr<<d.message<<'\n';return 4;}
  if(!valid_image(impexe,"import link")) return 18;
  std::vector<char> import_bytes;
  {std::ifstream import_in(impexe,std::ios::binary); import_bytes.assign((std::istreambuf_iterator<char>(import_in)),{});}
  if(!contains_ascii(import_bytes,"kernel32.dll") || !contains_ascii(import_bytes,"f")) return 5;

  // Exercise the exact MSVC path used by Forge/raz_runtime: the object names a
  // default library in .drectve, and ObLink discovers that library via -L state
  // rather than receiving it as an explicit positional input.
  const auto defmain=dir/"defaultlib.obj", defexe=dir/"defaultlib.exe", deflib=dir/"winapi.lib";
  write_bytes(defmain, object_with_defaultlib("main","__imp_f","winapi"));
  {std::ofstream out(deflib,std::ios::binary); out<<"!<arch>\n"; ar_member(out,"f.imp",import_object("f","kernel32.dll"));}
  oblink::LinkOptions defopts; defopts.output=defexe; defopts.library_paths.push_back(dir);
  auto default_result=oblink::link({defmain},defopts);
  if(!default_result.ok()){for(auto& d:default_result.diagnostics)std::cerr<<d.message<<'\n';return 6;}
  if(!valid_image(defexe,"DEFAULTLIB link")) return 26;

  const auto commonmain=dir/"common-main.obj", commonobj=dir/"common.obj", commonexe=dir/"common.exe";
  write_bytes(commonmain,object_with_symbol("main",true,"common"));write_bytes(commonobj,object_with_common("common",8));
  auto common_result=oblink::link({commonmain,commonobj},{.output=commonexe});
  if(!common_result.ok()){for(auto& d:common_result.diagnostics)std::cerr<<d.message<<'\n';return 7;}
  if(!valid_image(commonexe,"common symbol link")) return 27;

  const auto aliasmain=dir/"alias-main.obj", aliaslib=dir/"aliaslib.lib", aliasexe=dir/"alias.exe";
  write_bytes(aliasmain,object_with_alias("main","alias","aliaslib","real"));
  {std::ofstream out(aliaslib,std::ios::binary);out<<"!<arch>\n";ar_member(out,"real.obj",object_with_symbol("real",true));}
  oblink::LinkOptions aliasopts;aliasopts.output=aliasexe;aliasopts.library_paths.push_back(dir);
  auto alias_result=oblink::link({aliasmain},aliasopts);
  if(!alias_result.ok()){for(auto& d:alias_result.diagnostics)std::cerr<<d.message<<'\n';return 8;}
  if(!valid_image(aliasexe,"alias link")) return 28;

  // /FAILIFMISMATCH is how MSVC prevents ABI-incompatible CRT/object sets
  // from being silently combined. ObLink must reject conflicting values itself.
  const auto mm1=dir/"mismatch1.obj", mm2=dir/"mismatch2.obj", mmexe=dir/"mismatch.exe";
  write_bytes(mm1,object_with_directive("main"," /FAILIFMISMATCH:RuntimeLibrary=ONE"));
  write_bytes(mm2,object_with_directive("other"," /FAILIFMISMATCH:RuntimeLibrary=TWO"));
  auto mismatch_result=oblink::link({mm1,mm2},{.output=mmexe});
  if(mismatch_result.ok()) return 13;

  // Uninitialized data must reach the image as image extent with no file bytes.
  // Copying SizeOfRawData bytes from a null PointerToRawData reads the object's
  // own COFF header into .bss, and every zero-initialized global -- including
  // the CRT's startup lock -- then starts out non-zero.
  const auto bssmain=dir/"bss-main.obj", bss1=dir/"bss1.obj", bss2=dir/"bss2.obj", bssexe=dir/"bss.exe";
  write_bytes(bssmain,object_with_symbol("main",true));
  write_bytes(bss1,object_with_bss("first_slot",64));
  write_bytes(bss2,object_with_bss("second_slot",64));
  oblink::LinkOptions bssopts; bssopts.output=bssexe; bssopts.map_output=dir/"bss.map";
  auto bss_result=oblink::link({bssmain,bss1,bss2},bssopts);
  if(!bss_result.ok()){for(auto& d:bss_result.diagnostics)std::cerr<<d.message<<'\n';return 29;}
  if(!valid_image(bssexe,"bss link")) return 30;
  {
    const auto image=oblink::testing::validate_pe(bssexe);
    const auto* bss=image.section(".bss");
    if(bss==nullptr){std::cerr<<"bss link: no .bss section in the image\n";return 31;}
    if(bss->raw_size!=0U){std::cerr<<"bss link: .bss carries "<<bss->raw_size<<" file bytes\n";return 32;}
    // Two 64-byte slots cannot share one offset, so the section must span both.
    if(bss->virtual_size<128U){std::cerr<<"bss link: .bss spans only "<<bss->virtual_size<<" bytes\n";return 33;}
  }

  // A reference to a COMDAT definition that lost selection must reach the copy
  // that won. Resolving it through the discarded section's placement instead
  // silently aims the call at the start of the first output section.
  const auto dupmain=dir/"dup-main.obj", dup1=dir/"dup1.obj", dup2=dir/"dup2.obj", dupexe=dir/"dup.exe";
  const auto dupmap=dir/"dup.map";
  write_bytes(dupmain,object_with_symbol("main",true,"dupcall"));
  write_bytes(dup1,object_with_comdat_code("dupfn",3));
  write_bytes(dup2,object_with_discarded_comdat_reference("dupfn","dupcall",64));
  oblink::LinkOptions dupopts; dupopts.output=dupexe; dupopts.map_output=dupmap;
  auto dup_result=oblink::link({dupmain,dup1,dup2},dupopts);
  if(!dup_result.ok()){for(auto& d:dup_result.diagnostics)std::cerr<<d.message<<'\n';return 34;}
  if(!valid_image(dupexe,"discarded COMDAT link")) return 35;
  {
    auto mapped=[&](const std::string& want)->std::uint32_t{
      std::ifstream map(dupmap);
      for(std::string line;std::getline(map,line);){
        const auto space=line.find_last_of(' ');
        if(space==std::string::npos||line.substr(space+1)!=want) continue;
        return static_cast<std::uint32_t>(std::stoul(line.substr(0,space),nullptr,16));
      }
      return 0U;
    };
    const auto dupfn_rva=mapped("dupfn"), dupcall_rva=mapped("dupcall");
    if(dupfn_rva==0U||dupcall_rva==0U){std::cerr<<"discarded COMDAT link: symbols missing from the map\n";return 36;}
    const auto image=oblink::testing::validate_pe(dupexe);
    // dupcall survived while its own object's dupfn did not. Recompute its
    // branch target and require it to name the definition that prevailed.
    const auto call=image.at_rva(dupcall_rva,5);
    if(call.size()!=5||call[0]!=0xE8){std::cerr<<"discarded COMDAT link: dupcall is not a direct call\n";return 37;}
    const std::int32_t displacement=static_cast<std::int32_t>(
        std::uint32_t(call[1])|(std::uint32_t(call[2])<<8)|(std::uint32_t(call[3])<<16)|(std::uint32_t(call[4])<<24));
    const std::uint32_t target=static_cast<std::uint32_t>(
        static_cast<std::int64_t>(dupcall_rva)+5+displacement);
    if(target!=dupfn_rva){
      std::cerr<<"discarded COMDAT link: call targets 0x"<<std::hex<<target
               <<" but 'dupfn' is at 0x"<<dupfn_rva<<std::dec<<'\n';
      return 38;
    }
  }

  // An input object that brings its own .idata must not collide with the
  // synthesized import block. Emitting a second output section under the same
  // name made the descriptor writer address the contributed one and overrun it.
  const auto idmain=dir/"idata-main.obj", idlib=dir/"idata.lib", idexe=dir/"idata.exe";
  write_bytes(idmain,object_with_idata_contribution("main","__imp_f"));
  {std::ofstream out(idlib,std::ios::binary); out<<"!<arch>\n"; ar_member(out,"f.imp",import_object("f","kernel32.dll"));}
  auto idata_result=oblink::link({idmain,idlib},{.output=idexe});
  if(!idata_result.ok()){for(auto& d:idata_result.diagnostics)std::cerr<<d.message<<'\n';return 39;}
  if(!valid_image(idexe,"contributed .idata link")) return 40;
  {
    const auto image=oblink::testing::validate_pe(idexe);
    int idata_sections=0;
    for(const auto& s:image.sections) if(s.name==".idata") ++idata_sections;
    if(idata_sections!=1){
      std::cerr<<"contributed .idata link: image has "<<idata_sections<<" .idata sections\n";
      return 41;
    }
  }

  // A GNU import library states its imports as stub objects rather than
  // Microsoft short-import members. Recover the same import record from them,
  // so the DLL is published in the one import directory a PE has.
  const auto gmain=dir/"gnu-main.obj", glib=dir/"gnu.lib", gexe=dir/"gnu.exe";
  write_bytes(gmain,object_with_symbol("main",true,"f"));
  {std::ofstream out(glib,std::ios::binary); out<<"!<arch>\n";
   ar_member(out,"head.obj",mingw_import_head("_hd_gnu","gnu_inam"));
   ar_member(out,"stub.obj",mingw_import_stub("f","_hd_gnu",0x1234));
   ar_member(out,"tail.obj",mingw_import_tail("gnu_inam","gnu-test.dll"));}
  oblink::LinkOptions gopts; gopts.output=gexe; gopts.map_output=dir/"gnu.map";
  auto gnu_result=oblink::link({gmain,glib},gopts);
  if(!gnu_result.ok()){for(auto& d:gnu_result.diagnostics)std::cerr<<d.message<<'\n';return 42;}
  if(!valid_image(gexe,"GNU import library link")) return 43;
  {
    const auto image=oblink::testing::validate_pe(gexe);
    // The DLL has to appear in the published import directory, not merely
    // somewhere in the file: an unpublished descriptor leaves the IAT unbound
    // and the image crashes on the first call through it.
    const auto dlls=image.imported_dlls();
    if(std::find(dlls.begin(),dlls.end(),"gnu-test.dll")==dlls.end()){
      std::cerr<<"GNU import library link: gnu-test.dll is not in the import directory ("
               <<dlls.size()<<" entries)\n";
      return 44;
    }
    // The descriptor head and tail carry no image content; if they had been
    // linked as ordinary objects their `.idata$2`/`.idata$7` would show up as a
    // second, unpublished import structure.
    int idata_sections=0;
    for(const auto& s:image.sections) if(s.name==".idata") ++idata_sections;
    if(idata_sections!=1){
      std::cerr<<"GNU import library link: image has "<<idata_sections<<" .idata sections\n";
      return 46;
    }
  }

  std::cout<<"PE32+ lazy-archive/COMDAT/import/defaultlib/common/alias link PASS "<<alias_result.output_bytes<<" bytes\n"; std::filesystem::remove_all(dir); return 0;
}

// An exception escaping the body aborts the process with a status code that
// says nothing about which case failed, so report it before returning.
int main(){
  try { return run(); }
  catch (const std::exception& error) { std::cerr<<"link tests threw: "<<error.what()<<'\n'; return 99; }
}
