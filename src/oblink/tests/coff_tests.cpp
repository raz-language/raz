// Copyright 2026 Mario Vinciguerra
// SPDX-License-Identifier: Apache-2.0

#include <cstddef>
#include <cstdint>
#include <iostream>
#include <vector>
#include "oblink/coff.hpp"

static void u16(std::vector<std::byte>& b, std::uint16_t v){b.push_back(std::byte(v&255));b.push_back(std::byte(v>>8));}
static void u32(std::vector<std::byte>& b, std::uint32_t v){for(int i=0;i<4;++i)b.push_back(std::byte((v>>(8*i))&255));}

int main(){
  std::vector<std::byte> b;
  u16(b,0x8664); u16(b,1); u32(b,0); u32(b,61); u32(b,1); u16(b,0); u16(b,0);
  for(char c: std::string(".text")) { b.push_back(std::byte(c)); } while(b.size()<28)b.push_back(std::byte{0});
  u32(b,0); u32(b,0); u32(b,1); u32(b,60); u32(b,0); u32(b,0); u16(b,0); u16(b,0); u32(b,0x60000020);
  b.push_back(std::byte{0xC3});
  for(char c: std::string("main")) { b.push_back(std::byte(c)); } while(b.size()<69)b.push_back(std::byte{0});
  u32(b,0); u16(b,1); u16(b,0x20); b.push_back(std::byte{2}); b.push_back(std::byte{0}); u32(b,4);
  auto parsed=oblink::coff::parse(b);
  if(!parsed.ok()||parsed.object.sections.size()!=1||parsed.object.symbols.empty()||parsed.object.symbols[0].name!="main") return 1;
  // BigObj uses a 56-byte header and 20-byte symbol records with 32-bit
  // section indices. Large MSVC/clang-cl objects can appear inside ordinary
  // .lib archives, so this is part of the Windows compatibility baseline.
  std::vector<std::byte> big;
  u16(big,0);u16(big,0xffff);u16(big,2);u16(big,0x8664);u32(big,0);
  const unsigned char uuid[16]={0xc7,0xa1,0xba,0xd1,0xee,0xba,0xa9,0x4b,0xaf,0x20,0xfa,0xf6,0x6a,0xa4,0xdc,0xb8};
  for(unsigned char v:uuid)big.push_back(std::byte(v));
  for(int i=0;i<4;++i) { u32(big,0); } u32(big,1);u32(big,97);u32(big,1);
  for(char c:std::string(".text")) { big.push_back(std::byte(c)); } while(big.size()<64)big.push_back(std::byte{0});
  u32(big,0);u32(big,0);u32(big,1);u32(big,96);u32(big,0);u32(big,0);u16(big,0);u16(big,0);u32(big,0x60000020U);
  big.push_back(std::byte{0xC3});
  for(char c:std::string("main")) { big.push_back(std::byte(c)); } while(big.size()<105)big.push_back(std::byte{0});
  u32(big,0);u32(big,1);u16(big,0x20);big.push_back(std::byte{2});big.push_back(std::byte{0});u32(big,4);
  auto big_parsed=oblink::coff::parse(big);
  if(!big_parsed.ok()||!oblink::coff::is_bigobj(big)||big_parsed.object.sections.size()!=1||
     big_parsed.object.symbols.empty()||big_parsed.object.symbols[0].name!="main"||big_parsed.object.symbols[0].section_number!=1) return 2;

  // IMAGE_SCN_LNK_NRELOC_OVFL stores the real relocation count in a dummy
  // first relocation entry. Large generated compiler objects can exceed 65535
  // relocations even when the ordinary COFF section-count limit is not hit.
  std::vector<std::byte> ov;
  u16(ov,0x8664);u16(ov,1);u32(ov,0);u32(ov,85);u32(ov,2);u16(ov,0);u16(ov,0);
  for(char c:std::string(".text")) { ov.push_back(std::byte(c)); } while(ov.size()<28)ov.push_back(std::byte{0});
  u32(ov,0);u32(ov,0);u32(ov,5);u32(ov,60);u32(ov,65);u32(ov,0);u16(ov,0xffff);u16(ov,0);u32(ov,0x61000020U);
  ov.push_back(std::byte{0xE8});u32(ov,0);
  u32(ov,2);u32(ov,0);u16(ov,0); // overflow entry: dummy + one real relocation
  u32(ov,1);u32(ov,1);u16(ov,0x0004);
  for(size_t i=0;i<8;++i) { ov.push_back(i<4?std::byte("main"[i]):std::byte{0}); } u32(ov,0);u16(ov,1);u16(ov,0x20);ov.push_back(std::byte{2});ov.push_back(std::byte{0});
  for(size_t i=0;i<8;++i) { ov.push_back(i<6?std::byte("helper"[i]):std::byte{0}); } u32(ov,0);u16(ov,0);u16(ov,0);ov.push_back(std::byte{2});ov.push_back(std::byte{0});u32(ov,4);
  auto ov_parsed=oblink::coff::parse(ov);
  if(!ov_parsed.ok()||ov_parsed.object.sections[0].relocations.size()!=1||
     ov_parsed.object.sections[0].relocations[0].symbol_index!=1) return 3;
  std::cout << "COFF + BigObj + relocation-overflow parser baseline PASS\n";
  return 0;
}
