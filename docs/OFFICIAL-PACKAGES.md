# Official packages

The official [`raz-language/packages`](https://github.com/raz-language/packages) registry keeps application protocols, data formats, database clients, cryptographic utilities, and other ecosystem libraries outside the core standard library. Package archives are deterministic and immutable; their editable sources live alongside the registry data.

The standard library stays focused on broadly useful systems primitives. A feature that is useful to a particular protocol or application domain should usually begin as a package unless the compiler/runtime itself depends on it.

## Data formats and serialization

| Package | Purpose |
| --- | --- |
| `cbor` | CBOR encoding and decoding. |
| `csv` | Streaming CSV reading and bounded writing. |
| `encoding` | Hex, Base32, Base64url, and checksums. |
| `json` | Strict JSON parsing, writing, DOM access, and JSON Pointer. |
| `msgpack` | MessagePack encoding and decoding. |
| `protobuf` | Protocol Buffers wire primitives. |
| `serde` | Format-neutral serialization contracts. |
| `toml` | Source-backed TOML parsing. |
| `xml` | Streaming XML tokenization. |
| `yaml` | Bounded YAML 1.2 block-style parsing. |

## Security, identity, and chain formats

| Package | Purpose |
| --- | --- |
| `base58` | Bitcoin-alphabet Base58. |
| `bech32` | Bech32 and Bech32m codecs. |
| `crypto` | Hashing, MACs, KDFs, signatures, and secure utilities. |
| `jwt` | JWT/JWS claims, JWKs, and verification helpers. |
| `merkle` | Deterministic SHA-256 Merkle trees. |
| `rlp` | Canonical Recursive Length Prefix encoding. |
| `secp256k1` | Strict secp256k1 key/signature wire validation. |
| `ssz` | SimpleSerialize primitives and tree hashing. |
| `uuid` | UUID parsing, formatting, and v4/v7 generation. |

## Networking and application protocols

| Package | Purpose |
| --- | --- |
| `dns` | DNS wire-format and resolution helpers. |
| `graphql` | GraphQL document and JSON-envelope helpers. |
| `http-router` | Allocation-conscious HTTP route matching. |
| `http3` | HTTP/3 framing and stream-order primitives. |
| `multipart` | Streaming MIME multipart/form-data. |
| `oauth2` | OAuth 2.0 authorization-code and S256 PKCE helpers. |
| `quic` | QUIC wire primitives. |
| `rpc` | Transport-neutral bounded RPC framing. |
| `websocket` | RFC 6455 WebSocket protocol primitives. |

## Storage and systems

| Package | Purpose |
| --- | --- |
| `archive` | Safe USTAR and stored-ZIP primitives. |
| `compression` | Stable compression APIs. |
| `mmap` | Owned anonymous virtual-memory mappings. |
| `postgres` | PostgreSQL v3 wire-protocol client. |
| `redis` | RESP2/RESP3 protocol primitives. |
| `sqlite` | SQLite integration over the stable C ABI. |
| `uring` | io_uring-style operation and queue primitives. |
| `zstd` | Bounded Zstandard frame support. |

## Runtime, observability, and developer utilities

| Package | Purpose |
| --- | --- |
| `bigint` | Caller-buffered arbitrary-precision unsigned integers. |
| `datetime` | Civil time, timestamps, durations, offsets, and RFC 3339. |
| `decimal` | Checked fixed-scale decimal arithmetic. |
| `metrics` | Concurrent metrics primitives. |
| `regex` | Allocation-conscious regular expressions. |
| `semver` | Semantic-version parsing and requirement matching. |
| `testing` | Assertions, property testing, shrinking, fuzz mutation, and benchmark statistics. |
| `tracing` | Distributed tracing primitives. |
| `wasm-component` | WebAssembly Component Model binary inspection. |
| `wasm-runtime` | Bounded WebAssembly core execution primitives. |

Use `raz search`, `raz info`, and `raz add` to discover and install packages. [Package management](PACKAGE-MANAGEMENT.md) explains version constraints, lockfiles, registry resolution, offline use, and publishing.
