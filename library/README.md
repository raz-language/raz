# Raz standard library

The Raz standard library is implemented primarily in Raz. Native code is reserved for operating-system, ABI, cryptographic-engine, and raw memory boundaries that cannot reasonably be expressed portably in the language itself.

## Core and allocation

The library provides ownership-aware slices, strings, vectors, deques, hash sets, hash maps, options/results, iterators, formatting, bump arenas, fixed-size object pools, and raw allocation adapters. Collection storage is layout-aware and supports generic values with nontrivial size and alignment.

## I/O and operating-system APIs

Hosted APIs include:

- paths and filesystem operations, including allocation-reusing tree traversal/removal;
- files and text I/O;
- environment and shell-free process helpers, including reusable argv/cwd command execution;
- time, durations, monotonic clocks, deadlines, OS entropy, and bulk deterministic randomness;
- threads, synchronization, channels, futures, worker scheduling, cancellation, and timers; and
- allocation-free CLI option parsing plus reusable process-argument iteration.

Resource-owning values use deterministic `Drop` behavior.

## Networking

The network stack includes IPv4/IPv6 addressing, DNS resolution with a bounded hot-host cache, TCP/UDP sockets, vectored I/O, buffering, framing, an auto-growing batched readiness reactor, and higher-level protocol support. Socket/protocol policy lives in Raz while the runtime exposes the underlying OS operations.

## Serialization and application protocols

The library includes binary readers/writers, hexadecimal and Base64 codecs, CRC-32, URL/query/form handling, JSON parsing, allocation-reusing structured logging, HTTP/1.1 parsing and writing, reusable HTTP client/server primitives, streaming bodies, chunked transfer support, headers, and cookies.

TLS can use the optional OpenSSL engine for cryptography and certificate verification while connection/protocol policy remains in Raz.

## Performance principles

- keep scalar parser, codec, path, and buffer loops in Raz instead of crossing the native ABI per byte;
- use in-place-capable growth for ordinary vector/string/deque storage and bulk copies for wrapped regions;
- use control-byte fingerprints and power-of-two probing in hash containers;
- distinguish buffered draining from explicit stream flushing;
- batch network work through vectored I/O and reusable multi-socket polling;
- provide topology-specific concurrency primitives, including a lock-free batched SPSC ring, a lock-free bounded MPMC queue, and the blocking/cancellable general channel;
- prefer borrowed views and explicit reservation when ownership/allocation is unnecessary; and
- preserve deterministic destruction and narrow, measurable native boundaries.

See [Standard-library performance](../docs/STANDARD-LIBRARY-PERFORMANCE.md) for current performance guidance.
