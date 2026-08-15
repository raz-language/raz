# Raz standard library

The Raz standard library is implemented primarily in Raz. Native code is reserved for operating-system, ABI, cryptographic-engine, and raw memory boundaries that cannot reasonably be expressed portably in the language itself.

## Core and allocation

The library provides ownership-aware slices, strings, vectors, deques, hash sets, hash maps, options/results, iterators, formatting, and raw allocation adapters. Collection storage is layout-aware and supports generic values with nontrivial size and alignment.

## I/O and operating-system APIs

Hosted APIs include:

- paths and filesystem operations;
- files and text I/O;
- environment and process helpers;
- time, durations, monotonic clocks, and randomness;
- threads, synchronization, channels, futures, worker scheduling, cancellation, and timers.

Resource-owning values use deterministic `Drop` behavior.

## Networking

The network stack includes IPv4/IPv6 addressing, DNS resolution, TCP/UDP sockets, vectored I/O, buffering, framing, and higher-level protocol support. Socket/protocol policy lives in Raz while the runtime exposes the underlying OS operations.

## Serialization and application protocols

The library includes binary readers/writers, hexadecimal and Base64 codecs, CRC-32, URL/query/form handling, JSON parsing, HTTP/1.1 parsing and writing, reusable HTTP client/server primitives, streaming bodies, chunked transfer support, headers, and cookies.

TLS can use the optional OpenSSL engine for cryptography and certificate verification while connection/protocol policy remains in Raz.

## Performance principles

- avoid hidden allocation in hot parsing and networking paths;
- prefer borrowed views when ownership is unnecessary;
- keep collection growth and probing predictable;
- preserve deterministic destruction and explicit ownership transfer;
- keep native boundaries narrow and measurable.

See [Standard-library performance](../docs/STANDARD-LIBRARY-PERFORMANCE.md) for current performance guidance.
