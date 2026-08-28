#!/usr/bin/env python3
# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0

from pathlib import Path
ROOT = Path(__file__).resolve().parents[2]
s = (ROOT / "src/runtime/web_dev.cpp").read_text()
checks = {
  "rich status endpoint": '"/__raz/status.json"' in s and "build_ms" in s and "reload_kind_name" in s,
  "css hot refresh client": "querySelectorAll('link[rel~=stylesheet]')" in s and "s.reload==='css'" in s,
  "timed rebuild": "duration_cast<std::chrono::milliseconds>" in s and "rebuild_millis" in s,
  "last good bundle preserved": "Build failed after %lld ms; serving the last successful bundle" in s,
  "compat text status retained": '"/__raz/status"' in s,
  "dist reload classification": "dist_signature(dist, 1)" in s and "script_before" in s and "html_before" in s,
}
failed=[name for name,ok in checks.items() if not ok]
for name,ok in checks.items(): print(f"  {'PASS' if ok else 'FAIL'} {name}")
if failed: raise SystemExit(1)
print(f"web-dev-runtime: PASS ({len(checks)}/{len(checks)})")
