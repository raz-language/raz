# Security policy

Raz 1.0 is a stable language release, but a systems compiler, runtime, package manager, linker, and native backend all process complex inputs and deserve careful security review. Treat code that crosses `unsafe`, FFI, package, object-file, or operating-system boundaries with the same care you would in other native toolchains.

## Reporting a vulnerability

Please do not post exploit details in a public issue. Use GitHub private vulnerability reporting when it is available for the repository, or contact the repository owner privately. Include enough information to reproduce the problem, the affected platform and Raz version, and any known impact.

A public issue is fine for ordinary correctness bugs that do not expose a security boundary. If you are unsure, report privately first.

## Security-sensitive areas

Reports are especially useful around:

- parser and diagnostic handling of untrusted source;
- ownership, lifetime, bounds, and unsafe/raw-pointer validation;
- C interoperability and other native ABI boundaries;
- filesystem, process, network, TLS, and cryptographic runtime primitives;
- package archives, registries, lockfiles, paths, and integrity verification;
- Forge object emission and native code generation; and
- ObLink object parsing, relocation, import handling, and executable construction.

The project will coordinate disclosure for confirmed vulnerabilities and document fixes in the relevant release notes or changelog.
