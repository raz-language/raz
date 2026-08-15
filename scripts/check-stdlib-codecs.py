#!/usr/bin/env python3
# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0

from pathlib import Path
import sys
root = Path(__file__).resolve().parents[1]
errors = []
def require(rel, needles):
    path = root / rel
    if not path.is_file():
        errors.append(f"{rel}: missing file")
        return
    text = path.read_text(encoding='utf-8')
    for needle in needles:
        if needle not in text:
            errors.append(f"{rel}: missing {needle!r}")
require('library/core/ascii/ascii.rz', ['is_ascii', 'is_hex_digit', 'equal_ignore_case'])
require('library/std/encoding/hex/hex.rz', ['public enum HexError', 'encode_lower', 'encode_upper', 'public fn decode'])
require('library/std/encoding/base64/base64.rz', ['public enum Base64Error', 'public fn encode', 'public fn encode_url', 'public fn decode', 'public fn decode_url'])
require('library/std/encoding/checksum/crc32.rz', ['crc32_begin', 'crc32_update', 'crc32_finish', 'public fn crc32'])
require('library/std/net/url/url.rz', ['public struct UrlView', 'public fn parse', 'public fn is_http', 'public fn is_https', 'InvalidPort'])
for rel in [
    'library/core/ascii/ascii.rz',
    'library/std/encoding/hex/hex.rz',
    'library/std/encoding/base64/base64.rz',
    'library/std/encoding/checksum/crc32.rz',
    'library/std/net/url/url.rz',
]:
    text=(root/rel).read_text(encoding='utf-8')
    if 'extern fn' in text:
        errors.append(f'{rel}: new pure-Raz module introduced a native ABI boundary')
modules=list((root/'library').rglob('*.rz'))
if len(modules) < 71:
    errors.append(f'library: expected at least 71 Raz modules, found {len(modules)}')
if errors:
    print('stdlib codecs/URL audit: FAIL')
    for e in errors: print('  '+e)
    sys.exit(1)
print('stdlib codecs/URL audit: PASS')
print('  core: ASCII classification/case/hex helpers')
print('  encoding: hex + Base64 + IEEE CRC-32')
print('  net: borrowed validated URL parser')
print(f'  library modules: {len(modules)}')
