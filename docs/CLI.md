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

`raz doctor` can run outside a project. It checks the host architecture, native linker driver, and Raz driver location. When a project path is supplied (or a project is discovered), it also validates the package entry/modules and writes `target/doctor/doctor.json`.

## Build and execute

```text
raz build [options] <input> [output]
raz run <input>
raz run <input> -- <program-args...>
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
raz init [path]
raz clean
raz fmt [--check] <file.rz>
raz doc <file.rz> [output.md]
```

`raz test` discovers zero-argument, non-extern functions whose names begin with `test_`. A return value of zero means success; a nonzero result fails the test command.

## Package commands

```text
raz search <query>
raz info <package>
raz add <package>[@<constraint>]
raz add <alias> <path>
raz add <alias> registry:<name>@<constraint>
raz remove <alias>
raz outdated
raz update
raz lock
raz metadata
raz graph
raz registry <name> <constraint>
raz pack [output.dpk]
raz publish
```

`raz search` and `raz info` inspect the static official index without materializing package archives. `raz outdated` compares tracked registry dependencies with the newest published versions and reports the newest version still compatible with each recorded constraint. `raz add <package>` and `raz add <package>@<constraint>` resolve from the official [`raz-language/packages`](https://github.com/raz-language/packages) registry. `raz pack` creates a deterministic `.dpk` archive from the current package. Without a registry override, `raz publish` prepares a `.raz-publish/` submission tree for the official GitHub registry. Explicit HTTP/HTTPS and filesystem registries remain supported for private registries; Bearer authentication uses `RAZ_REGISTRY_TOKEN`.

See [Package management](PACKAGE-MANAGEMENT.md) for the official registry, private registries, mirror fallback, package archives, publishing, the shared content-addressed store, lockfiles, and offline behavior.

## Workspace commands

A root manifest can coordinate multiple member packages:

```toml
[workspace]
members = ["crates/core", "apps/cli"]
```

Run workspace operations from the directory containing that root manifest:

```text
raz build --workspace
raz check --workspace
raz test --workspace
raz update --workspace
raz fetch --workspace
raz lock --workspace
raz metadata --workspace
raz graph --workspace
```

The workspace uses one root `raz.lock`. Build/check/test options such as `--features=...`, `--all-features`, `--no-default-features`, backend selection, and target options are forwarded to each member.

### `raz keygen [prefix]`

Generate an Ed25519 package-signing key pair. The default prefix is `raz`, producing `raz.private.key` and `raz.public.key`.

## Terminal output

Raz keeps normal command output compact and action-oriented. Work is shown at package granularity by default, while `--verbose` adds per-module incremental actions. Packages that are completely fresh do not produce noisy compile lines. `--quiet` suppresses non-error status output.

```text
   Compiling core v0.4.2
   Compiling app v1.0.0
    Finished app [release, host] (3 compiled, 8 fresh) in 412 ms
```

`raz run` adds a final aligned `Running` line before transferring control to the built executable. Arguments after `--` are forwarded unchanged to the program, and the program's exit status is preserved. Status labels remain aligned across build, test, package, install, and tooling commands.

Color is automatic when stdout/stderr is attached to a terminal. It can be controlled explicitly:

```text
raz build --color auto
raz build --color always
raz build --color never
raz build --quiet
```

The standard `NO_COLOR` environment variable disables automatic diagnostic color. `RAZ_COLOR=always|never|auto` can also control compiler diagnostics launched by Raz.

## Command errors and help

Command parsing fails before project discovery or build work begins. Common command and option typos include a close-match suggestion and a short help hint instead of dumping the full help screen.

```text
$ raz biuld
error: no such command: 'biuld'
  help: a similar command exists: 'build'
  help: view all commands with 'raz --help'

$ raz build --relase
error: unexpected argument '--relase'
  help: a similar option exists: '--release'
  help: run 'raz build --help' for command usage
```

General and command-specific help are available through equivalent forms:

```text
raz --help
raz -h
raz help build
raz build --help
raz build -h
raz --version
raz -V
```

Options that require values diagnose a missing value directly. Long value options accept both `--option value` and `--option=value` forms for profile, target, jobs, color, diagnostic format, install prefix, reports, registry paths, and numeric benchmark/fuzz controls.

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
