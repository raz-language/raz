# Package management

Raz uses `raz.toml` for package metadata and `raz.lock` for deterministic dependency resolution. Local paths, the official registry, and commit-pinned Git sources feed the same package graph.

## Dependency sections

A manifest can declare runtime, test-only, build-tool, optional, and operating-system-specific dependencies:

```toml
[dependencies]
core-utils = "../core-utils"

[dev-dependencies]
test-kit = "../test-kit"

[build-dependencies]
codegen = "../codegen"

[optional-dependencies]
tls = "../tls"

[target.windows.dependencies]
win32 = "../win32"

[target.linux.dependencies]
epoll = "../epoll"

[target.macos.dependencies]
darwin = "../darwin"

[features]
default = ["dep:tls"]
full = ["default", "dep:extra"]
```

Normal dependencies are part of every build. Dev dependencies are added to the project graph for `raz test`. OS sections are selected from the host platform. Build dependencies are resolved and locked for tooling use but are not linked into the target program. Optional dependencies enter the graph only when enabled by a feature.

Feature names can reference `dep:<alias>` or another feature. The root feature set is selected with:

```text
raz build --features=tls,extra raz.toml
raz build --all-features raz.toml
raz build --no-default-features raz.toml
```

Features are unified by name across the assembled package graph. `default` is enabled unless `--no-default-features` is supplied.

## Workspaces

A repository containing several Raz packages can use a root `raz.toml` with a workspace manifest:

```toml
[workspace]
members = [
    "crates/core",
    "crates/net",
    "apps/server",
]
```

Workspace member paths are relative to the workspace root, use `/` separators, and must remain inside the workspace. Duplicate members, absolute paths, and `.` or `..` path segments are rejected. Inline and multiline member arrays are supported, including comments and trailing commas.

From the workspace root, the following commands operate across every member in declaration order:

```text
raz build --workspace
raz check --workspace
raz test --workspace
raz update --workspace
raz fetch --workspace
raz lock --workspace
raz metadata --workspace
raz graph --workspace
raz tree --workspace
```

Backend and feature options are forwarded to member build/check/test commands. `library` members are type-checked during `raz build --workspace`; executable members emit normally. This matches Raz's source-package model, where library sources are assembled into consumers rather than requiring a standalone native library artifact.

A workspace owns one root `raz.lock`. `raz lock --workspace` walks every member and its dependency graph, canonicalizes package identities, and writes each package once even when several members share it. `raz update --workspace` and `raz fetch --workspace` run dependency maintenance in each member directory, remove member-local lockfiles created by those operations, and regenerate the root lock. Registry/Git cache and tracking files remain member-local so a member can still be used independently.

Workspace metadata, graph, and tree output are grouped under `workspace: <member>` headings. A package can depend on another workspace member using the same relative path dependency syntax used outside a workspace.

## Adding dependencies

Add a dependency directly to a specific section:

```text
raz add algorithms
raz add test-kit ../test-kit --dev
raz add codegen ../codegen --build
raz add tls@^1.4.0 --optional
raz add epoll ../epoll --target=linux
```

The supported section selectors are `--dev`, `--build`, `--optional`, `--target=windows`, `--target=linux`, and `--target=macos`. `raz remove <alias>` searches every dependency section. Registry tracking records the original section, so `raz update` preserves dependency scope.

## Path dependencies

Add a local package with:

```text
raz add math ../math
```

Raz validates the dependency manifest, updates the selected dependency section, and regenerates `raz.lock`.

## Git dependencies

Git dependencies are deliberately commit-pinned. A source uses this form:

```text
raz add codec git:https://github.com/example/raz-codec#0123456789abcdef0123456789abcdef01234567
```

The revision must be a complete 40-character hexadecimal commit SHA. Raz invokes Git without a shell, materializes the checkout under `.raz/git/<content-key>/`, verifies that it contains `raz.toml`, records the immutable source in `.raz.git`, and writes the concrete cache path into the selected manifest section.

`raz fetch` and `raz update` recreate missing Git materializations from `.raz.git`. They do not move a Git dependency to another revision; changing a Git revision is an explicit manifest/package-manager change. `.raz/` is cache state, while `.raz.git` is reproducibility metadata and should be committed with the project. Because the manifest points at the project-local Git materialization, Git dependencies are suitable for applications and workspaces but are not accepted in published package archives; publishable libraries should depend on registry packages or vendor the dependency source inside the package.

## Registry dependencies

The official Raz registry is the public [`raz-language/packages`](https://github.com/raz-language/packages) repository. No registry configuration is required for normal package use.

Add the latest compatible stable package with:

```text
raz add algorithms
```

Or supply a semantic-version constraint directly:

```text
raz add semver@^1.0.0
```

The explicit registry form remains available when an alias differs from the package name:

```text
raz add versioning registry:semver@^1.0.0
```

Raz fetches `index.txt` from the official raw GitHub registry and resolves package archive paths relative to that repository. Supported constraints are:

- exact: `1.2.3`
- caret: `^1.2.0`
- tilde: `~1.2.0`
- minimum: `>=1.2.0`

The highest compatible stable version is selected deterministically. Prerelease versions are considered only when the constraint explicitly names a prerelease.

For tests, air-gapped environments, or local snapshots, set:

```text
RAZ_REGISTRY_INDEX=/path/to/index.txt
```

For a private or alternate network registry, override the official base URL:

```text
RAZ_REGISTRY_URL=https://packages.example.com
```

Optional mirrors are supplied as a semicolon-separated list:

```text
RAZ_REGISTRY_MIRRORS=https://mirror-a.example.com;https://mirror-b.example.com
```

The primary registry is tried first, followed by mirrors in order. HTTP and HTTPS are supported; HTTPS uses Raz's verified TLS client transport. Each index record contains a package name, semantic version, package source, and complete package-tree checksum. Package sources may be local directories, absolute HTTP/HTTPS archive URLs, or paths relative to the registry base.

## Package archives

Network registry packages use the deterministic Raz package archive format (`.dpk`). An archive begins with `RAZPKG1` and contains path/content records. Paths are validated during extraction; absolute paths, traversal components, and platform-specific escape forms are rejected.

The archive itself is only a transport container. Package identity is the checksum of the complete extracted package tree in deterministic relative-path order. This means changing any source file changes the package identity even when `raz.toml` is unchanged.

## Integrity

The registry checksum is verified against the complete selected package tree before the package can enter the project graph. A checksum mismatch is a hard error. Existing store entries are reverified before reuse.

## Shared package store

Registry packages are materialized into a content-addressed store. Same-name official dependencies use compact Cargo-style constraints, for example `websocket = "^0.2.0"`. Explicit aliases may use `wire = "registry:websocket@^0.2.0"`. `raz.lock` records the exact selected version and verified content checksum; the project loader maps that locked checksum to the configured local store. Legacy `registry:<checksum>` manifest entries remain readable for compatibility.

Store selection is:

1. `RAZ_PACKAGE_STORE`, when set.
2. `RAZ_HOME/store`, when `RAZ_HOME` is set.
3. `~/.raz/store` on Unix-like systems or `%USERPROFILE%\.raz\store` on Windows.
4. `./.raz/store` only when no user-home location is available.

Entries are stored as:

```text
<store>/<content-hash>/
```

Projects therefore reference immutable verified package content rather than mutable registry locations or machine-specific paths. Multiple projects using the same package content reuse the same store entry.

## Offline mode

Set:

```text
RAZ_OFFLINE=1
```

to prohibit network access and registry-source materialization. Raz keeps the last successfully fetched registry index in `.raz.registry-index`; offline resolution can use that snapshot together with integrity-valid entries already present in the shared store.

A missing or corrupt store entry is an error in offline mode. Raz never silently substitutes mutable source content.

## Build-time dependency hydration

Normal project commands automatically ensure exact locked registry dependencies are present before loading the package graph:

```text
raz build
raz check
raz run
raz test
```

A freshly cloned project containing `raz.toml` and `raz.lock` therefore does **not** require a separate `raz fetch` step. Raz reads the lockfile, verifies or materializes the exact registry package checksums in the shared content-addressed store, recursively hydrates locked transitive packages, and only then loads the project graph. Existing valid store entries make this a local integrity check.

`raz fetch` remains useful as an explicit prefetch command for CI image preparation, air-gapped workflows, and dependency warming. `raz update` is the only normal command that intentionally re-resolves compatible registry versions. This keeps ordinary builds lockfile-driven rather than network-version-driven.

The direct compiler boundary remains network-independent; registry access belongs to project/package orchestration rather than language compilation itself.

## Locking and updates

`raz lock` rebuilds the deterministic lockfile from all declared dependency sections. Registry constraints remain unchanged in `raz.toml`; registry lock entries record the exact portable `registry:<content-hash>` path together with `source = "registry"` and the verified checksum, never the local store directory. If a manifest constraint is edited so the current lockfile no longer satisfies it, normal builds fail with guidance to run `raz update`.

`.raz.registry` may be created as derived internal state to accelerate registry update operations. It is regenerated from `raz.toml` before update/discovery work and is never the source of dependency intent. Projects should commit `raz.toml` and `raz.lock`, not `.raz.registry` or `.raz.cache`.

`raz update` re-evaluates tracked registry constraints, preserves each dependency's original section, restores missing pinned Git materializations, and selects the highest compatible registry version currently available. `raz fetch` instead treats `raz.lock` as authoritative: it materializes the exact locked registry versions/checksums and does not upgrade them. If no lockfile exists, `fetch` performs the initial resolution needed to create one. Repeating either operation without input changes produces byte-stable output.

Registry constraints and Git identities are tracked separately from concrete cache paths so dependency intent survives cache cleanup. Removing a dependency removes its registry or Git tracking as well. Lock generation rejects graphs where the same package name resolves to different versions or different package roots; Raz reports the conflict instead of silently choosing whichever dependency happened to be traversed first.

## Discovery and inspection

```text
raz search <query>
raz info <package>
raz outdated
raz metadata
raz graph
raz tree
raz registry <name> <constraint>
```

`search` performs a case-insensitive package-name search over the static registry index and prints each matching package with its latest version. `info` lists the latest and all published versions of one package. Neither command downloads package archives.

`outdated` compares each tracked registry dependency's concrete installed version with the latest published version and also reports the latest version compatible with its recorded constraint when an in-range upgrade exists. `metadata` prints recursive package information. `graph` and its conventional alias `tree` print dependency relationships, while `registry` shows the version and store path selected for a registry constraint.

## Creating package archives

Create a deterministic Raz package archive from the current project:

```text
raz pack
```

The default output is `<name>-<version>.dpk`. You can choose another output path:

```text
raz pack dist/widget-1.2.3.dpk
```

`pack` excludes generated project state (`.git/`, `.raz/`, `.raz.cache`, `.raz.registry`, `.raz.registry-index`, `.raz.git`, `.raz-publish/`, `build/`, `target/`, compiler diagnostics, and existing `.dpk` outputs). It rejects dependency paths that would escape the archive or point into machine-local cache state; published packages must use registry dependencies or vendored paths contained inside the package. The archive uses the same full-tree content hash used by the package store and registry verifier.

## Publishing

The official registry is GitHub-backed and published versions are immutable. From a package directory, run:

```text
raz publish
```

When neither `RAZ_REGISTRY_URL` nor `RAZ_REGISTRY_PUBLISH_DIR` is set, Raz validates and packs the current package and creates a repository-shaped submission under:

```text
.raz-publish/
  index.txt
  packages/
    <name>/
      <version>.dpk
```

The generated archive can be copied into [`raz-language/packages`](https://github.com/raz-language/packages), the index regenerated/validated there, and submitted by pull request. A package path already present on the registry's `main` branch is immutable; fixes require a new semantic version.

Private registries remain supported. Publish directly to an HTTP/HTTPS registry with:

```text
RAZ_REGISTRY_URL=https://packages.example.com \
RAZ_REGISTRY_TOKEN=<token> \
RAZ_REGISTRY_SIGNATURE=<detached-signature> \
raz publish
```

For a filesystem registry, set:

```text
RAZ_REGISTRY_PUBLISH_DIR=/srv/raz-registry raz publish
```

The explicit private-registry paths retain the existing authenticated/idempotent publishing protocol.

### Publishing environment

- `RAZ_REGISTRY_URL` — override the official GitHub registry with a primary HTTP/HTTPS registry base URL.
- `RAZ_REGISTRY_TOKEN` — optional Bearer token used by `raz publish`.
- `RAZ_REGISTRY_SIGNATURE` — optional detached signature sent as `X-Raz-Signature` during publishing.
- `RAZ_REGISTRY_PUBLISH_DIR` — filesystem registry destination for local/private registries; when unset with no URL override, `raz publish` writes `.raz-publish/`.
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

## Installing package tools

Registry packages that expose an executable entry can be installed as user tools:

```text
raz install formatter
raz install formatter@^1.4
raz install tooling
raz install --bins tooling
raz install --bin=razfmt tooling
raz install --list
raz install --update tooling
raz install --force tooling@^1.4
raz uninstall formatter
```

Installation uses the same registry resolver, signature/integrity checks, archive format, and content-addressed store as project dependencies. The executable is built in release mode and written to `RAZ_HOME/bin`, or to the default per-user `.raz/bin` directory when `RAZ_HOME` is unset. Raz records the installed package version, original constraint, checksum, and selected executable name in user-level tool metadata so installed executables remain manageable independently of the shared package cache.

A package may expose additional executable entries with Cargo-style `[[bin]]` records:

```toml
[package]
name = "tooling"
version = "1.0.0"
entry = "src/main.rz"

[[bin]]
name = "razfmt"
entry = "src/bin/razfmt.rz"

[[bin]]
name = "razdoc"
entry = "src/bin/razdoc.rz"
```

Packages may declare multiple Cargo-style `[[bin]]` targets. Plain `raz install tooling` installs every declared binary, while `raz install --bins tooling` makes the same all-binary selection explicit and `raz install --bin=razfmt tooling` compiles only the selected entry without changing the package's normal project entry point. Installed-tool metadata is per binary, so one package can own several executables simultaneously. A managed binary name cannot be stolen by another package, even with `--force`.

`raz install --list` prints the managed tool set. `raz install --update <package>` resolves the newest release that still satisfies the recorded constraint and updates the package's executable targets; supplying `<package>@<constraint>` replaces that constraint for the update. A normal install refuses to overwrite an existing managed executable, while `--force` deliberately rebuilds/replaces executables owned by the same package. `raz uninstall <package>` removes every executable and metadata record owned by that package but does not purge reusable registry/store content.

## Vendored dependencies

For hermetic, air-gapped, or source-controlled dependency trees, run `raz vendor`. Raz first hydrates the exact `raz.lock` graph, then copies content-addressed registry packages to `vendor/registry/` and pinned Git materializations to `vendor/git/`. The `.raz-vendor` marker activates local source replacement for ordinary builds; package resolution is no longer dependent on the global cache or network. `raz vendor --check` performs an integrity-only verification. Re-run vendoring after changing the lockfile with `raz update`.

## Native system libraries

Packages that expose a stable native ABI can declare their linker requirements without adding package-specific compiler code:

```toml
[native]
libraries = ["sqlite3"]
library-paths = ["vendor/lib"]
```

`libraries` contains platform linker library names rather than filenames. On Unix-like targets Raz emits the corresponding `-l<name>` options; MSVC-style Windows links use `<name>.lib`. `library-paths` are resolved relative to the package that declares them and are forwarded as native library search paths.

Native requirements are **transitive**. If an application depends on a Raz package that declares `sqlite3`, the final executable link automatically receives that requirement; the application does not repeat it. Duplicate libraries and search paths are removed while preserving dependency order.

Raz deliberately does not accept arbitrary shell commands or raw linker command strings from package manifests. The first native dependency contract is limited to library identities and search paths so registry packages cannot turn normal dependency resolution into arbitrary command execution.

The native library itself remains an external platform/package-manager responsibility. For example, the `sqlite` package requires SQLite 3 to be installed or otherwise available in a declared `library-paths` directory.
