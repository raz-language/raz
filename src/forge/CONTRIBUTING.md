# Contributing to Forge

Thank you for helping improve Forge. Contributions should preserve deterministic output, verification boundaries, and the interpreter/JIT differential contract.

## Before opening a pull request

```sh
./scripts/release-gate.sh
./scripts/sanitizer-gate.sh
```

Windows contributors can run:

```powershell
.\scripts\release-gate.ps1
.\scripts\sanitizer-gate.ps1
```

## Contribution requirements

- Build cleanly with warnings treated as errors.
- Add focused regression coverage for behavioral changes.
- Preserve deterministic canonical IR and object output.
- Verify IR or machine IR after structural transformations.
- Add or tighten a code-quality baseline for intentional backend improvements.
- Do not weaken a regression threshold merely to make a change pass.
- Update public documentation when changing supported behavior or APIs.

## Pull requests

Keep changes focused. Describe the problem, design, user-visible behavior, compatibility impact, and validation performed. Large architectural changes should include a short design note under `docs/`.

## Style

The repository includes `.clang-format` and `.editorconfig`. Keep generated files and build products outside the source tree.

## Reporting bugs

Include the Forge version, platform/toolchain, smallest reproducer, expected behavior, observed behavior, and relevant diagnostics.

## Source license headers

Every maintained source, header, build script, test source, and example source must begin with the project license notice:

```text
Copyright 2026 Mario Vinciguerra
SPDX-License-Identifier: Apache-2.0
```

Use the comment syntax appropriate for the file type and preserve a shebang as the first line of executable scripts. The repository hygiene test enforces this requirement.
