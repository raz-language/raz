#!/usr/bin/env python3
# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0

"""Create a clean Raz source archive without deleting or mutating the worktree."""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path
import sys
import zipfile

sys.dont_write_bytecode = True
from path_policy import should_package, SEMANTIC_TARGET_ROOTS

ROOT = Path(__file__).resolve().parents[1]

# Package invariants. These files make the historical Forge target-source loss a
# hard packaging failure rather than a defect that can escape into a release.
REQUIRED_LEGAL_FILES = (Path("LICENSE"), Path("NOTICE"), Path("AUTHORS.md"))

REQUIRED_TARGET_FILES = (
    Path("src/forge/src/target/data_layout.cpp"),
    Path("src/forge/src/target/abi.cpp"),
    Path("src/forge/include/forge/target/data_layout.hpp"),
    Path("src/forge/include/forge/target/abi.hpp"),
    Path("src/forge/include/forge-c/forge.h"),
)


def sha256(path: Path) -> str:
    h=hashlib.sha256()
    with path.open('rb') as f:
        for chunk in iter(lambda:f.read(1024*1024), b''):
            h.update(chunk)
    return h.hexdigest()


def verify_required(root: Path) -> None:
    legal_missing=[p for p in REQUIRED_LEGAL_FILES if not (root/p).is_file()]
    if legal_missing:
        raise SystemExit("REFUSING TO PACKAGE: required legal files are missing:\n" +
                         "\n".join(f"  {p}" for p in legal_missing))
    missing=[p for p in REQUIRED_TARGET_FILES if not (root/p).is_file()]
    if missing:
        raise SystemExit("REFUSING TO PACKAGE: required semantic target sources are missing:\n" +
                         "\n".join(f"  {p}" for p in missing))


def main() -> int:
    ap=argparse.ArgumentParser()
    ap.add_argument('output', type=Path)
    args=ap.parse_args()
    output=args.output.resolve()
    verify_required(ROOT)

    files=[]
    for p in ROOT.rglob('*'):
        if not p.is_file():
            continue
        rel=p.relative_to(ROOT)
        if should_package(rel):
            files.append(rel)
    files.sort(key=lambda p:p.as_posix())

    if output.exists(): output.unlink()
    output.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(output, 'w', compression=zipfile.ZIP_DEFLATED, compresslevel=9) as z:
        for rel in files:
            z.write(ROOT/rel, rel.as_posix())

    # Re-open and enforce semantic-source preservation plus exact content hashes.
    with zipfile.ZipFile(output, 'r') as z:
        names=set(z.namelist())
        legal_missing=[p for p in REQUIRED_LEGAL_FILES if p.as_posix() not in names]
        if legal_missing:
            output.unlink(missing_ok=True)
            raise SystemExit("PACKAGE VERIFICATION FAILED: legal files omitted:\n" +
                             "\n".join(f"  {p}" for p in legal_missing))
        missing=[p for p in REQUIRED_TARGET_FILES if p.as_posix() not in names]
        if missing:
            output.unlink(missing_ok=True)
            raise SystemExit("PACKAGE VERIFICATION FAILED: target sources omitted:\n"+
                             "\n".join(f"  {p}" for p in missing))
        for rel in REQUIRED_TARGET_FILES:
            archived=hashlib.sha256(z.read(rel.as_posix())).hexdigest()
            source=sha256(ROOT/rel)
            if archived != source:
                output.unlink(missing_ok=True)
                raise SystemExit(f"PACKAGE VERIFICATION FAILED: hash mismatch for {rel}")
        forbidden=[n for n in names if n.startswith(('build/','out/','.git/')) or '/.raz/' in '/'+n or '/__pycache__/' in '/'+n]
        if forbidden:
            output.unlink(missing_ok=True)
            raise SystemExit('PACKAGE VERIFICATION FAILED: generated artifacts included')

    print(f'package-source: PASS ({len(files)} files)')
    print(f'semantic target preservation: PASS ({len(REQUIRED_TARGET_FILES)} required files, hash verified)')
    print(output)
    return 0

if __name__ == '__main__':
    raise SystemExit(main())
