# Official packages

The official `raz-language/packages` registry keeps larger protocols, data formats, database clients, and application-layer utilities outside the core standard library. Published archives are deterministic and immutable; editable sources live beside them in the registry repository.

## Data formats and serialization

| Package | Purpose |
| --- | --- |
| `json` | JSON document/value parsing and writing. |
| `yaml` | YAML reading, values, errors, and writing. |
| `toml` | TOML configuration parsing. |
| `csv` | Streaming zero-copy CSV reading and allocation-free writing. |
| `xml` | Streaming zero-copy XML tokens and attributes. |
| `cbor` | CBOR reader/writer primitives. |
| `msgpack` | MessagePack reader/writer primitives. |
| `protobuf` | Protocol Buffers wire reader/writer primitives. |
| `serde` | Shared serialization helpers. |
| `encoding` | Hex, base32, base64url, and checksums. |

## Security and identity

| Package | Purpose |
| --- | --- |
| `crypto` | Hashing, MACs, password/KDF, signatures, and secure utilities. |
| `jwt` | JWT/JWS claims, JWKs, HS256 policy and verification. |
| `uuid` | UUID representation/parsing/generation helpers. |

## Networking and web

| Package | Purpose |
| --- | --- |
| `http-router` | HTTP route matching. |
| `multipart` | Multipart form reading/writing. |
| `websocket` | WebSocket framing/protocol support. |

## Storage and databases

| Package | Purpose |
| --- | --- |
| `sqlite` | SQLite bindings and safe statement/connection APIs. |
| `postgres` | PostgreSQL wire/client functionality. |

## Systems and utilities

| Package | Purpose |
| --- | --- |
| `archive` | USTAR and stored-ZIP format primitives with path validation. |
| `compression` | Stable LZ4 block compression package API. |
| `regex` | Regular-expression compilation and matching. |
| `semver` | Semantic-version parsing and comparison. |
| `datetime` | Date/time application utilities. |
| `testing` | Assertions, property testing, deterministic fuzz mutation, and benchmark statistics. |

## Exact numeric types

| Package | Purpose |
| --- | --- |
| `decimal` | Checked fixed-scale decimal arithmetic for finance/deterministic applications. |
| `bigint` | Caller-buffered arbitrary-precision unsigned integer arithmetic. |

The standard library remains focused on universally needed systems primitives. New application formats and protocols should normally begin as registry packages unless they are required by the compiler/runtime itself or are fundamental host abstractions.
