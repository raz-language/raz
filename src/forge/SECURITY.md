# Security policy

## Supported version

Security fixes are applied to the latest released major/minor line. Older development archives are not maintained.

## Reporting a vulnerability

Do not open a public issue for a suspected vulnerability. Report it privately to the project maintainers with:

- Affected Forge version and platform
- Reproduction input or minimal test case
- Expected and observed behavior
- Potential security impact
- Any proposed mitigation

Avoid including secrets, personal data, or unrelated proprietary source code. Maintainers should acknowledge a complete report, reproduce it, prepare a fix and regression test, and coordinate disclosure after patched artifacts are available.

## Security boundaries

Forge treats parsed IR, binary IR, runtime bindings, external symbols, and object-file inputs as untrusted. Release builds are gated by parser/binary fuzz smoke tests, strict verification, deterministic output checks, and ASan/UBSan execution.
