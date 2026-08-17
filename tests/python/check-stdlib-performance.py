#!/usr/bin/env python3
# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0

from pathlib import Path
import sys

root = Path(__file__).resolve().parents[2]
errors: list[str] = []


def text(rel: str) -> str:
    path = root / rel
    if not path.is_file():
        errors.append(f"{rel}: missing file")
        return ""
    return path.read_text(encoding="utf-8")


def require(rel: str, needles: list[str]) -> None:
    source = text(rel)
    for needle in needles:
        if needle not in source:
            errors.append(f"{rel}: missing {needle!r}")


def forbid(rel: str, needles: list[str]) -> None:
    source = text(rel)
    for needle in needles:
        if needle in source:
            errors.append(f"{rel}: hot-path regression contains {needle!r}")


require("src/runtime/core.cpp", [
    "raz_rt_realloc_aligned",
    "raz_rt_hash_bytes",
    "raz_rt_atomic_exchange_i64_ordered",
    "raz_rt_atomic_compare_exchange_i64_ordered",
    "raz_rt_cpu_relax",
])
require("library/alloc/arena/arena.rz", [
    "public struct BumpArena",
    "return (value + alignment - 1) & (0 - alignment)",
    "arena.offset = start + size",
    "public fn rewind",
    "public fn reset",
])
require("library/alloc/vec/vec.rz", ["raz_rt_realloc_aligned"])
require("library/alloc/pool/pool.rz", [
    "public struct FixedPool",
    "pool.free_head = get_next(slot)",
    "set_next(pointer, pool.free_head)",
])
require("library/alloc/deque/deque.rz", [
    "raw_deque_wrap_index",
    "raw_deque_copy_linear",
    "raz_rt_realloc_aligned",
])
require("library/collections/hash_map/hash_map.rz", [
    "fn hash_control",
    "state == control",
    "raz_rt_fill(states, 0, capacity)",
])
require("library/collections/hash_set/hash_set.rz", [
    "fn set_hash_control",
    "state == control",
    "raz_rt_fill(states, 0, capacity)",
])
require("library/alloc/hash_map/hash_map.rz", ["hash & (map.capacity - 1)", "fn hash_map_control", "state == control"])
require("library/alloc/hash_set/hash_set.rz", ["hash & (set.capacity - 1)", "fn hash_set_control", "state == control"])
require("library/alloc/string/hash.rz", ["raz_rt_hash_bytes(self.data, self.length)"])
require("library/core/utf8/utf8.rz", [
    "fn load_byte",
    "fn store_byte",
    "public fn decode",
    "public fn validate",
    "public fn count_scalars",
])
forbid("library/core/utf8/utf8.rz", ["core::bytes::load_u8(", "core::bytes::store_u8("])
require("library/alloc/string/string.rz", [
    "import core::utf8;",
    "return core::utf8::validate(data, length)",
    "return core::utf8::count_scalars(value.data, value.length)",
    "DecodeResult decoded = core::utf8::decode",
])
require("library/std/io/buffered.rz", [
    "default_buffer_capacity = 65536",
    "fn writer_drain",
    "return raz_rt_file_flush(value.handle) == 1",
])
require("library/std/fs/tree.rz", [
    "String scratch = alloc::string::with_capacity(length + 128)",
    "i64 parent_length = path.length",
    "alloc::string::truncate(path, parent_length)",
])
require("library/std/net/reactor.rz", [
    "public struct BatchReactor",
    "RawVec timers",
    "timer_sift_up",
    "timer_sift_down",
    "public fn batch_watch_readable",
    "public fn batch_set_interests",
    "public fn batch_remove_swap",
    "public fn batch_schedule_after",
    "public fn batch_pop_expired",
    "std::net::poll_set::wait(&mut reactor.set, effective)",
])
require("library/std/fs/read_dir.rz", [
    "i64 available = alloc::string::capacity(name)",
    "raz_rt_dir_next(iterator.handle, name.data, available + 1",
    "while (next_capacity < required)",
])
require("library/std/process/args/args.rz", ["public fn next_into", "alloc::string::reserve(output, required)"])
require("library/std/process/command.rz", ["raz_rt_process_run_argv_cwd", "public fn cwd_bytes"])
require("library/std/random/random.rz", ["public fn rng_fill_u64", "public fn rng_shuffle_raw"])
require("library/std/cli/parser/parser.rz", ["public fn split_long_option", "core::bytes::find_byte(body, 61)"])

require("library/std/net/http/client/client.rz", [
    "public struct HttpClientPool",
    "public fn pool_get",
    "pool.next_slot = (pool.next_slot + 1) & 3",
])
require("library/std/net/http/server/server.rz", [
    "byte_buffer_capacity(&output.input)",
    "byte_buffer_reserve(&mut output.input, server.buffer_capacity)",
    "String decode_buffer",
    "alloc::string::clear(&mut connection.decode_buffer)",
    "IoVector send_vector",
    "send_header_body(",
    "std::net::vectored::send_tcp",
    "public fn read_request_view",
    "public fn release_request",
    "public fn buffered_request_ready",
    "connection.request_pending = true",
])
require("library/std/net/resolve/resolve.rz", [
    "public struct DnsCache",
    "public fn resolve_first_cached",
    "cache.next_slot = (cache.next_slot + 1) & 3",
])
require("library/std/log/log.rz", [
    "public struct LogBuffer",
    "public fn field_i64",
    "public fn field_bytes",
    "public fn field_bool",
    "public fn field_u64",
    "public fn write_stream",
])

require("src/runtime/runtime_internal.hpp", [
    "struct RazTlsSessionCache",
    "std::array<RazTlsCacheEntry, 8>",
    "raz_tls_cached_session",
    "raz_tls_cache_session",
    "SSL_SESS_CACHE_CLIENT",
])
require("src/runtime/tls.cpp", [
    "SSL_set_session(ssl, cached)",
    "raz_tls_cache_session(session->hostname, session->ssl)",
])
require("library/std/compress/lz4/lz4.rz", [
    "public struct Encoder",
    "const i64 hash_slots = 4096",
    "public fn compress_bound",
    "public fn compress(",
    "public fn decompress(",
    "raz_rt_fill(encoder.table, 255, hash_bytes)",
])

require("library/std/net/poll_set/poll_set.rz", [
    "public struct PollSet",
    "raz_rt_socket_poll_many",
    "max_records = 1048576",
    "public fn reserve",
    "public fn set_interests",
    "public fn remove_swap",
    "set.length >= set.capacity && !reserve(set, set.length + 1)",
])
require("src/runtime/network.cpp", [
    "raz_rt_socket_poll_many",
    "std::array<pollfd, 256> stack_fds",
    "thread_local std::vector<pollfd> dynamic_fds",
])
require("library/std/thread/spsc/spsc.rz", [
    "public struct SpscI64",
    "state + 64",
    "try_send_many",
    "try_receive_many",
    "capacity - slot",
])
require("library/std/thread/mpmc/mpmc.rz", [
    "public struct MpmcI64",
    "state + 64",
    "position & queue.mask",
    "raz_rt_atomic_compare_exchange_i64_ordered",
    "raz_rt_cpu_relax",
])
require("library/std/encoding/checksum/crc32.rz", [
    "raz_rt_crc32_ieee_update",
])
require("src/runtime/core.cpp", [
    "raz_make_crc32_ieee_table",
    "while (index + 8 <= size)",
])

# Scalar byte access in std is deliberately Raz-local. Reintroducing these
# helper/native calls inside parsers, paths, codecs, and buffers adds a call per
# byte and should fail the performance contract.
stdlib = "\n".join(p.read_text(encoding="utf-8") for p in sorted((root / "library/std").rglob("*.rz")))
for needle in ("core::bytes::load_u8(", "raz_rt_load_u8(", "raz_rt_store_u8("):
    if needle in stdlib:
        errors.append(f"library/std: scalar byte hot paths contain {needle!r}")

# Internal buffered drains must not force an OS-visible fflush; only the public
# writer_flush path is allowed to invoke it.
buffered = text("library/std/io/buffered.rz")
drain_start = buffered.find("fn writer_drain")
flush_start = buffered.find("public fn writer_flush")
if drain_start >= 0 and flush_start > drain_start:
    drain_body = buffered[drain_start:flush_start]
    if "raz_rt_file_flush" in drain_body:
        errors.append("library/std/io/buffered.rz: writer_drain must not call file flush")

if errors:
    print("stdlib performance audit: FAIL")
    for error in errors:
        print("  " + error)
    sys.exit(1)

print("stdlib performance audit: PASS")
print("  allocation: in-place-capable vector/deque growth")
print("  collections: power-of-two probing + control-byte fingerprints")
print("  bytes/strings/codecs: scalar hot loops stay in Raz, bulk work stays batched")
print("  I/O/network: retained tree paths + 64 KiB buffering + timer-integrated batch reactor + DNS/HTTP/TLS reuse")
print("  compression: allocation-reusing caller-buffer LZ4 block codec")
print("  concurrency: cacheline-separated lock-free SPSC + sequence-based MPMC queues")
