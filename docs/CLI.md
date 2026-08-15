# Raz CLI

`raz` is the compiler and project driver. The default backend is Forge; LLVM can be selected explicitly.

## General

```text
raz --help
raz --version
raz doctor
raz backends
raz targets
```

`raz doctor` reports the host platform, built-in backend availability, and external LLVM/Clang tools when relevant.

## Build and execute

```text
raz build [options] <input> [output]
raz run <input>
raz check <input>
raz test <input>
raz lint <input>
```

Common backend controls:

```text
--backend=forge
--backend=llvm
--forge
--llvm
--opt=0|1|2|3|s|z
```

## Backend commands

```text
raz forge <input> <output.fir>
raz forge --forge-native <input> <output.obj>

raz llvm --emit=llvm <input> <output.ll>
raz llvm --emit=obj --opt=3 <input> <output.obj>
raz llvm --emit=exe <input> <output>
```

Use `raz forge --help` or `raz llvm --help` for backend-specific target, optimization, linkage, and emission options.

## Project commands

```text
raz new <name>
raz init
raz clean
raz fmt [--check] <file.rz>
raz doc <file.rz> [output.md]
```

`raz test` discovers zero-argument, non-extern functions whose names begin with `test_`. A return value of zero means success; a nonzero result fails the test command.

## Package commands

```text
raz add <alias> <path>
raz add <alias> registry:<name>@<constraint>
raz remove <alias>
raz update
raz lock
raz metadata
raz graph
raz registry <name> <constraint>
raz pack [output.dpk]
raz publish
```

`raz pack` creates a deterministic `.dpk` archive from the current package. `raz publish` publishes that archive plus registry metadata to an HTTP/HTTPS or filesystem registry. Bearer authentication uses `RAZ_REGISTRY_TOKEN`; private registries may also require the optional `RAZ_REGISTRY_SIGNATURE` detached-signature hook.

See [Package management](PACKAGE-MANAGEMENT.md) for local/network registries, mirror fallback, package archives, publishing, the shared content-addressed store, lockfiles, and offline behavior.

### `raz keygen [prefix]`

Generate an Ed25519 package-signing key pair. The default prefix is `raz`, producing `raz.private.key` and `raz.public.key`.

## Terminal output

Raz keeps normal command output intentionally compact. A successful build prints a single aligned status line; `--verbose` adds per-module actions and `--quiet` suppresses non-error status output.

```text
  Finished app [release, host] (3 compiled, 8 fresh) in 412 ms
```

Color is automatic when stdout/stderr is attached to a terminal. It can be controlled explicitly:

```text
raz build --color auto
raz build --color always
raz build --color never
raz build --quiet
```

The standard `NO_COLOR` environment variable disables automatic diagnostic color. `RAZ_COLOR=always|never|auto` can also control compiler diagnostics launched by Raz.

## Diagnostics

Compiler diagnostics use stable error codes, exact source locations, source excerpts, caret/range markers, and contextual help where Raz can make a useful suggestion.

```text
error[D2008]: unknown name 'countt'
  --> src/main.rz:7:17
   |
 7 |     i64 count = countt;
   |                 ^~~~~~
  = help: check the spelling, import the symbol, or qualify it with its namespace

error: could not compile 'src/main.rz' due to 1 previous error
```

Error codes are intended to remain searchable/stable even when the wording of a diagnostic improves. Piped/CI output stays plain text unless color is explicitly forced.

### Diagnostic formats

The compiler has three diagnostic renderers backed by the same structured diagnostic model:

```text
raz check --diagnostic-format human
raz check --diagnostic-format short
raz check --diagnostic-format json

razc --check --diagnostic-format human src/main.rz
razc --check --diagnostic-format short src/main.rz
razc --check --diagnostic-format json src/main.rz
```

`human` is the default terminal renderer. `short` emits one compiler-style line per diagnostic, which is useful for simple editor/problem matchers:

```text
src/main.rz:7:17: error[D2008]: unknown name 'countt'
```

`json` is intended for CI, IDEs, and other tools. Direct compiler output uses the `raz-diagnostics-v1` schema. Project checks aggregate isolated module-worker reports into one `raz-project-diagnostics-v1` document. Ranges are zero-based UTF-16 line/character positions and also include original-source byte offsets. Generated semantic compilation units are mapped back to the physical `.rz` source path and original line/byte positions before diagnostics are published.

Structured diagnostics can contain:

- severity and stable diagnostic code;
- category (`lexer`, `parser`, `semantic`, `lowering`, or `backend`);
- primary and secondary source labels;
- notes and help text;
- machine-readable replacement fixes;
- original source path, UTF-16 range, and byte offsets.

### Warning policy

Compiler warnings can be controlled by code or category using ordered policy flags:

```text
raz check --allow D2052
raz check --deny D2052
raz check --deny semantic
raz check --deny warnings --allow D2052
raz check --warn D2052
```

The same flags are available on `razc`. Later flags override earlier ones. `--deny-warnings` remains a shorthand for making warnings errors, while a later `--allow` or `--warn` can override an individual code/category.

Recognized compiler categories are:

```text
lexer      D0000-D0999
parser     D1000-D1999
semantic   D2000-D2999
lowering   D3000-D3999
backend    D4000-D4999
```

### Diagnostic catalog

`raz diagnostics` prints the compiler's diagnostic category/catalog information. The list of known codes is generated from the compiler implementation during configuration so it cannot silently drift from the source.

```text
raz diagnostics
raz diagnostics --diagnostic-format json
```

The JSON form uses the `raz-diagnostic-catalog-v1` schema and is suitable for IDE/tooling discovery.
