# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0

#!/usr/bin/env python3
from __future__ import annotations

import argparse
import os
from pathlib import Path
import shutil
import subprocess


def run(raz: Path, cwd: Path) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        [str(raz), "check"],
        cwd=cwd,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
    )


def rewrite_preserving_metadata(path: Path, old: str, new: str) -> None:
    if len(old) != len(new):
        raise RuntimeError("regression rewrite must preserve byte length")
    before = path.stat()
    text = path.read_text(encoding="utf-8")
    if old not in text:
        raise RuntimeError(f"rewrite source not found in {path}: {old!r}")
    path.write_text(text.replace(old, new, 1), encoding="utf-8")
    os.utime(path, ns=(before.st_atime_ns, before.st_mtime_ns))
    after = path.stat()
    if after.st_size != before.st_size or after.st_mtime_ns != before.st_mtime_ns:
        raise RuntimeError("test platform did not preserve size + modification timestamp")


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--raz", required=True)
    ap.add_argument("--work", required=True)
    args = ap.parse_args()

    raz = Path(args.raz).resolve()
    work = Path(args.work).resolve()
    if work.exists():
        shutil.rmtree(work)
    app = work / "app"
    dep = work / "dep"
    (app / "src").mkdir(parents=True)
    (dep / "src").mkdir(parents=True)

    (dep / "raz.toml").write_text(
        '[package]\nname = "dep"\nversion = "0.1.0"\nkind = "static-library"\nentry = "src/lib.rz"\n\n[dependencies]\n',
        encoding="utf-8",
    )
    dep_source = dep / "src/lib.rz"
    dep_source.write_text(
        'namespace dep;\npublic fn value() -> i64 { return 1; }\n',
        encoding="utf-8",
    )
    manifest = app / "raz.toml"
    manifest.write_text(
        '[package]\nname = "app"\nversion = "0.1.0"\nkind = "executable"\nentry = "src/main.rz"\n\n[dependencies]\ndep = "../dep"\n',
        encoding="utf-8",
    )
    (app / "src/main.rz").write_text(
        'import dep;\nfn main() -> i64 { return 0; }\n',
        encoding="utf-8",
    )

    initial = run(raz, app)
    if initial.returncode != 0:
        raise SystemExit(f"initial project check failed:\n{initial.stdout}")

    # This edit is invisible to a cache that trusts only size + mtime. The
    # dependency body must still be re-read and rejected.
    rewrite_preserving_metadata(dep_source, "return 1;", "return z;")
    invalid = run(raz, app)
    if invalid.returncode == 0:
        raise SystemExit(
            "timestamp-preserving dependency edit restored stale project source:\n" + invalid.stdout
        )

    # Recover using another metadata-identical rewrite. A stale invalid snapshot
    # must not survive either.
    rewrite_preserving_metadata(dep_source, "return z;", "return 2;")
    recovered = run(raz, app)
    if recovered.returncode != 0:
        raise SystemExit(f"project did not recover after dependency rewrite:\n{recovered.stdout}")

    # Configuration inputs use the same correctness boundary. Changing only the
    # package version keeps file size + mtime identical but must refresh project
    # metadata and therefore surface the new package version in command output.
    rewrite_preserving_metadata(manifest, 'version = "0.1.0"', 'version = "0.2.0"')
    config = run(raz, app)
    if config.returncode != 0 or "app v0.2.0" not in config.stdout:
        raise SystemExit(f"timestamp-preserving manifest edit was not observed:\n{config.stdout}")

    print("project-snapshot-invalidation: PASS")
    print("  dependency source and manifest rewrites invalidate even when size + mtime are unchanged")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
