#!/usr/bin/env python3
# Copyright 2026 Mario Vinciguerra
# SPDX-License-Identifier: Apache-2.0

"""Shared helpers for Raz performance qualification tools."""
from __future__ import annotations

import json
import os
from pathlib import Path
import statistics
import subprocess
import threading
import time
from typing import Any


def _linux_rss_bytes(pid: int) -> int | None:
    try:
        text = Path(f"/proc/{pid}/status").read_text(encoding="utf-8")
    except OSError:
        return None
    for line in text.splitlines():
        if line.startswith("VmRSS:"):
            parts = line.split()
            if len(parts) >= 2:
                return int(parts[1]) * 1024
    return None


def _windows_rss_bytes(pid: int) -> int | None:
    if os.name != "nt":
        return None
    try:
        import ctypes
        from ctypes import wintypes

        PROCESS_QUERY_INFORMATION = 0x0400
        PROCESS_VM_READ = 0x0010

        class PROCESS_MEMORY_COUNTERS(ctypes.Structure):
            _fields_ = [
                ("cb", wintypes.DWORD),
                ("PageFaultCount", wintypes.DWORD),
                ("PeakWorkingSetSize", ctypes.c_size_t),
                ("WorkingSetSize", ctypes.c_size_t),
                ("QuotaPeakPagedPoolUsage", ctypes.c_size_t),
                ("QuotaPagedPoolUsage", ctypes.c_size_t),
                ("QuotaPeakNonPagedPoolUsage", ctypes.c_size_t),
                ("QuotaNonPagedPoolUsage", ctypes.c_size_t),
                ("PagefileUsage", ctypes.c_size_t),
                ("PeakPagefileUsage", ctypes.c_size_t),
            ]

        kernel32 = ctypes.windll.kernel32
        psapi = ctypes.windll.psapi
        handle = kernel32.OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, False, pid)
        if not handle:
            return None
        try:
            counters = PROCESS_MEMORY_COUNTERS()
            counters.cb = ctypes.sizeof(counters)
            if not psapi.GetProcessMemoryInfo(handle, ctypes.byref(counters), counters.cb):
                return None
            return int(counters.WorkingSetSize)
        finally:
            kernel32.CloseHandle(handle)
    except Exception:
        return None


def process_rss_bytes(pid: int) -> int | None:
    if os.name == "nt":
        return _windows_rss_bytes(pid)
    return _linux_rss_bytes(pid)


def run_measured(command: list[str], *, cwd: Path, env: dict[str, str] | None = None, timeout: float | None = None) -> dict[str, Any]:
    start = time.perf_counter()
    process = subprocess.Popen(command, cwd=cwd, env=env, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    peak_rss = 0
    stop = threading.Event()

    def sample() -> None:
        nonlocal peak_rss
        while not stop.is_set() and process.poll() is None:
            rss = process_rss_bytes(process.pid)
            if rss is not None:
                peak_rss = max(peak_rss, rss)
            stop.wait(0.01)

    sampler = threading.Thread(target=sample, daemon=True)
    sampler.start()
    try:
        stdout, stderr = process.communicate(timeout=timeout)
    except subprocess.TimeoutExpired:
        process.kill()
        stdout, stderr = process.communicate()
        stop.set()
        sampler.join(timeout=0.2)
        raise RuntimeError(f"command timed out: {' '.join(command)}")
    stop.set()
    sampler.join(timeout=0.2)
    elapsed = time.perf_counter() - start
    return {
        "command": command,
        "returncode": process.returncode,
        "wall_seconds": elapsed,
        "peak_rss_bytes": peak_rss or None,
        "stdout": stdout,
        "stderr": stderr,
    }


def summarize(samples: list[float]) -> dict[str, float]:
    ordered = sorted(samples)
    return {
        "min": min(ordered),
        "median": statistics.median(ordered),
        "mean": statistics.fmean(ordered),
        "max": max(ordered),
    }


def write_json(path: Path, payload: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(payload, indent=2, sort_keys=True) + "\n", encoding="utf-8")
