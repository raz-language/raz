#!/usr/bin/env python3
# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0

from pathlib import Path
import sys
sys.dont_write_bytecode = True


root = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(root / "tools"))
from compiler_sources import ordered_sources
from path_policy import is_generated_dir, SEMANTIC_TARGET_ROOTS
cpp_extensions = {'.c', '.cc', '.cpp', '.cxx', '.h', '.hh', '.hpp', '.hxx'}
allowed_cpp_roots = (
    Path('src/bootstrap'),
    Path('src/forge'),
    Path('src/runtime'),
    Path('tests/native'),
    Path('benchmarks/reference/c'),
)
ignored_roots = {'build', 'out', 'target', '.git'}
violations = []

for path in root.rglob('*'):
    if not path.is_file() or path.suffix.lower() not in cpp_extensions:
        continue
    rel = path.relative_to(root)
    if rel.parts and rel.parts[0] in ignored_roots:
        continue
    if any(rel == allowed or allowed in rel.parents for allowed in allowed_cpp_roots):
        continue
    violations.append(str(rel))

if violations:
    print('Native C/C++ files must remain inside src/bootstrap, src/forge, src/runtime, tests/native, or benchmarks/reference/c:')
    print('\n'.join(f'  {item}' for item in violations))
    sys.exit(1)
print('repository native-source layout: PASS')

try:
    canonical_sources = ordered_sources(root)
except RuntimeError as error:
    print(f'Canonical production compiler source set is invalid: {error}')
    sys.exit(1)
if canonical_sources[-1] != root / 'compiler' / 'src' / 'main.rz':
    print('Canonical production compiler entrypoint must be compiler/src/main.rz and last in host-source-order.txt')
    sys.exit(1)

forbidden = [
    root / 'frontend.rz',
    root / 'compiler' / 'frontend.rz',
    root / 'bootstrap' / 'compiler' / 'frontend.rz',
    root / 'bootstrap' / 'compiler' / 'src' / 'main.rz',
]
found = [str(path.relative_to(root)) for path in forbidden if path.exists()]
if found:
    print('Production compiler source layout violation:')
    for item in found:
        print(f'  redundant compiler source: {item}')
    sys.exit(1)

# No generated compiler/build state belongs in the source tree.  Use the
# centralized path policy: Forge has real semantic source directories named
# `target`, so basename-only cleanup/checking is forbidden.
generated = []
for path in root.rglob('*'):
    if not path.is_dir():
        continue
    rel = path.relative_to(root)
    if is_generated_dir(rel):
        # Root build/out are expected local workspaces, and .git is repository
        # metadata rather than committed source. Nested package/cache output is
        # still a violation.
        if rel.parts and rel.parts[0] in {'build', 'out', '.git'}:
            continue
        generated.append(str(rel))
if generated:
    print('Generated/cache directories must not be committed:')
    for item in generated:
        print(f'  {item}')
    sys.exit(1)

print(f'compiler source layout: PASS ({len(canonical_sources)} ordered Raz modules)')
