# Licensing

Raz is licensed under the **Apache License, Version 2.0**. The complete license text is in the repository-root `LICENSE` file, and project attribution is recorded in `NOTICE` and `AUTHORS.md`.

## Source-file headers

Maintained source, build, and script files carry compact SPDX metadata rather than embedding the full license text:

```text
Copyright 2026 Mario Vinciguerra
SPDX-License-Identifier: Apache-2.0
```

The comment marker follows the file format (`//`, `#`, or `REM`). Shebangs and required batch preambles remain first when necessary.

The repository check `scripts/check-license-headers.py` verifies this policy and is part of CTest. Data formats that do not support comments (for example JSON and lock files), generated/intermediate fixtures, documentation prose, and binary artifacts do not receive source headers.

## Contributions

Unless explicitly stated otherwise, contributions intentionally submitted for inclusion in Raz are accepted under Apache-2.0, consistent with Section 5 of the Apache License. Contributors must have the right to submit their work and must preserve third-party notices and license terms when incorporating external code.

## Forge

Forge is bundled under `src/forge/` and is also Apache-2.0 licensed. Its nested `LICENSE` is retained so Forge remains independently redistributable and its licensing is unambiguous when consumed separately from Raz.
