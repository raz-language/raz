# Package management

Raz uses `raz.toml` for package metadata and `raz.lock` for deterministic dependency resolution. Path and registry dependencies feed the same project graph.

## Path dependencies

Add a local package with:

```text
raz add math ../math
```

Raz validates the dependency manifest, updates `[dependencies]`, and regenerates `raz.lock`.

## Registry dependencies

Raz supports either a local registry snapshot or a network registry.

For a local snapshot, set:

```text
RAZ_REGISTRY_INDEX=/path/to/index.txt
```

For a network registry, set a base URL:

```text
RAZ_REGISTRY_URL=https://packages.example.com
```

Raz requests `index.txt` from that base. Optional mirrors are supplied as a semicolon-separated list:

```text
RAZ_REGISTRY_MIRRORS=https://mirror-a.example.com;https://mirror-b.example.com
```

The primary registry is tried first, followed by mirrors in order. HTTP and HTTPS are supported; HTTPS uses Raz's verified TLS client transport.

Each index record contains a package name, semantic version, package source, and complete package-tree checksum. Package sources may be local directories, absolute HTTP/HTTPS archive URLs, or paths relative to the registry base.

```text
raz add json registry:json@^1.2.0
```

Supported constraints are:

- exact: `1.2.3`
- caret: `^1.2.0`
- tilde: `~1.2.0`
- minimum: `>=1.2.0`

The highest compatible version is selected deterministically.

## Package archives

Network registry packages use the deterministic Raz package archive format (`.dpk`). An archive begins with `RAZPKG1` and contains path/content records. Paths are validated during extraction; absolute paths, traversal components, and platform-specific escape forms are rejected.

The archive itself is only a transport container. Package identity is the checksum of the complete extracted package tree in deterministic relative-path order. This means changing any source file changes the package identity even when `raz.toml` is unchanged.

## Integrity

The registry checksum is verified against the complete selected package tree before the package can enter the project graph. A checksum mismatch is a hard error. Existing store entries are reverified before reuse.

## Shared package store

Registry packages are materialized into a content-addressed store before they are written into the project manifest.

Store selection is:

1. `RAZ_PACKAGE_STORE`, when set.
2. `RAZ_HOME/store`, when `RAZ_HOME` is set.
3. `~/.raz/store` on Unix-like systems or `%USERPROFILE%\.raz\store` on Windows.
4. `./.raz/store` only when no user-home location is available.

Entries are stored as:

```text
<store>/<content-hash>/
```

Projects therefore reference immutable verified package content rather than mutable registry locations. Multiple projects using the same package content reuse the same store entry.

## Offline mode

Set:

```text
RAZ_OFFLINE=1
```

to prohibit network access and registry-source materialization. Raz keeps the last successfully fetched registry index in `.raz.registry-index`; offline resolution can use that snapshot together with integrity-valid entries already present in the shared store.

A missing or corrupt store entry is an error in offline mode. Raz never silently substitutes mutable source content.

## Locking and updates

`raz lock` rebuilds the deterministic lockfile from the current package graph. `raz update` re-evaluates tracked registry constraints and selects the highest compatible version currently available. Repeating either operation without input changes produces byte-stable output.

Registry constraints are tracked separately from the concrete stored path so updates retain the original version intent. Removing a dependency removes its registry tracking as well.

## Inspection

```text
raz metadata
raz graph
raz registry <name> <constraint>
```

`metadata` prints recursive package information. `graph` prints dependency relationships. `registry` shows the version and store path selected for a registry constraint.

## Creating package archives

Create a deterministic Raz package archive from the current project:

```text
raz pack
```

The default output is `<name>-<version>.dpk`. You can choose another output path:

```text
raz pack dist/widget-1.2.3.dpk
```

`pack` excludes generated project state (`.git/`, `.raz/`, `build/`, `target/`, compiler diagnostics, and existing `.dpk` outputs). The archive uses the same full-tree content hash used by the package store and registry verifier.

## Publishing

Publish the current package to an HTTP/HTTPS registry:

```text
RAZ_REGISTRY_URL=https://packages.example.com \
RAZ_REGISTRY_TOKEN=<token> \
RAZ_REGISTRY_SIGNATURE=<detached-signature> \
raz publish
```

Publishing uses idempotent `PUT` requests for the archive and version metadata. `RAZ_REGISTRY_TOKEN`, when set, is sent as a Bearer authorization token. `RAZ_REGISTRY_SIGNATURE`, when set, is sent as `X-Raz-Signature`; this is a detached-signature hook so private registries can enforce their own signing algorithm and trust policy. The published metadata includes the package name, semantic version, archive location, and complete package-tree hash.

For a filesystem registry, set:

```text
RAZ_REGISTRY_PUBLISH_DIR=/srv/raz-registry raz publish
```

Raz writes the archive under `packages/<name>/<version>.dpk` and updates `index.txt`. Re-publishing the same name/version replaces that index entry instead of adding a duplicate.

### Publishing environment

- `RAZ_REGISTRY_URL` — primary HTTP/HTTPS registry base URL.
- `RAZ_REGISTRY_TOKEN` — optional Bearer token used by `raz publish`.
- `RAZ_REGISTRY_SIGNATURE` — optional detached signature sent as `X-Raz-Signature` during publishing.
- `RAZ_REGISTRY_PUBLISH_DIR` — filesystem registry destination for local/private registries.
- `RAZ_REGISTRY_MIRRORS` — ordered fallback registry bases for package consumption.
- `RAZ_PACKAGE_STORE` — explicit content-addressed package store.
- `RAZ_HOME` — Raz home directory; its `store/` subdirectory is used when no explicit store is configured.
- `RAZ_OFFLINE=1` — disable network access and require cached index/store content.

## Package signing and trusted keys

Raz can sign published package metadata with Ed25519. Generate a key pair with:

```text
raz keygen registry
```

This writes `registry.private.key` and `registry.public.key`. Keep the private key out of source control. Set `RAZ_SIGNING_KEY` to the private-key file when publishing:

```text
RAZ_SIGNING_KEY=/secure/registry.private.key raz publish
```

Signed registry records carry a key identifier and Ed25519 signature over the package name, version, and verified package-tree hash. Consumers configure trusted public keys with `RAZ_TRUSTED_KEYS`; each line contains a key identifier followed by a 32-byte public key encoded as hexadecimal. Set `RAZ_REQUIRE_SIGNATURES=1` to reject unsigned registry records.

The existing `RAZ_REGISTRY_SIGNATURE` HTTP header remains a registry-authentication hook and is independent from package identity signing.
