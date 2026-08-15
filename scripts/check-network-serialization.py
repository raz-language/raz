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
    text = path.read_text(encoding="utf-8")
    for needle in needles:
        if needle not in text:
            errors.append(f"{rel}: missing {needle!r}")

require('library/std/net/address/address.rz', [
    'public struct Ipv4Address', 'public struct SocketAddress', 'parse_ipv4',
    'ipv4_to_string', 'socket_address_to_string'])
require('library/std/encoding/binary/binary.rz', [
    'public struct BinaryReader', 'public struct BinaryWriter',
    'read_u8(BinaryReader&mut value) -> Result<u8, CodecError>',
    'read_u16_be(BinaryReader&mut value) -> Result<u16, CodecError>',
    'read_u32_be(BinaryReader&mut value) -> Result<u32, CodecError>',
    'read_u64_be', 'read_var_u64', 'write_var_i64', 'read_length_prefixed'])
require('library/std/io/buffer/buffer.rz', [
    'public struct ByteBuffer', 'byte_buffer_reserve', 'byte_buffer_commit',
    'byte_buffer_compact', 'byte_buffer_read_into'])
require('library/std/net/typed/typed.rz', [
    'public fn try_tcp_connect', 'public fn try_udp_connect', 'public fn try_udp_send_to'])
require('library/std/net/buffered/buffered.rz', [
    'public enum StreamError', 'public struct BufferedTcpStream',
    'public fn connect_address', 'public fn read_exact', 'public fn read_until', 'public fn flush'])
require('library/std/net/framed/framed.rz', [
    'public fn write_frame', 'public fn read_frame', 'StreamError::LimitExceeded'])
require('library/std/net/net.rz', [
    'tcp_stream_local_port', 'tcp_stream_peer_port', 'tcp_listener_local_port', 'udp_socket_local_port'])
require('src/forge/src/ir/lexer.cpp', [
    "source[i + 1] == 'x' || source[i + 1] == 'X'", 'hexadecimal'])
require('src/forge/src/machine/lower.cpp', [
    'parse_float_bits', 'from_chars', 'integer source literal as a floating FIR constant'])

# New high-level modules must consume shared byte primitives rather than growing
# duplicate native ABI shims for copy/load/store.
for rel in [
    'library/std/net/address/address.rz',
    'library/std/encoding/binary/binary.rz',
    'library/std/io/buffer/buffer.rz',
    'library/std/net/typed/typed.rz',
    'library/std/net/buffered/buffered.rz',
    'library/std/net/framed/framed.rz',
]:
    text = (root / rel).read_text(encoding='utf-8')
    for forbidden in ['extern fn raz_rt_memcpy', 'extern fn raz_rt_copy', 'extern fn raz_rt_load_u8', 'extern fn raz_rt_store_u8']:
        if forbidden in text:
            errors.append(f"{rel}: duplicates native boundary via {forbidden!r}")

library_modules = list((root / 'library').rglob('*.rz'))
if len(library_modules) < 66:
    errors.append(f"library: expected at least 66 Raz modules, found {len(library_modules)}")

if errors:
    print('networking/serialization audit: FAIL')
    for error in errors:
        print('  ' + error)
    sys.exit(1)
print('networking/serialization audit: PASS')
print('  network: typed IPv4/socket addresses + buffered TCP + bounded frames')
print('  encoding: endian integers + varints + length-prefixed byte views')
print('  I/O: owned byte buffering with reserve/commit/consume/compact')
print('  Forge FIR: hexadecimal constant print/parse/lower round-trip')
print(f'  library modules: {len(library_modules)}')
