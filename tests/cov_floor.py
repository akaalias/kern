#!/usr/bin/env python3
"""Enforce the core line-coverage floor from an llvm-cov export JSON.

Usage: cov_floor.py <summary.json> <floor_percent>

Reads data[0].totals.lines.percent — which is stable across llvm-cov versions,
unlike the `llvm-cov report` table whose column layout differs between the macOS
(Apple) and Linux llvm builds. Parsing that table by column index is exactly how
local (line coverage) and CI (branch coverage) silently disagreed. Exits
non-zero when line coverage is below the floor so the same call gates both."""
import json
import sys

summary_path, floor = sys.argv[1], float(sys.argv[2])
with open(summary_path) as f:
    pct = json.load(f)["data"][0]["totals"]["lines"]["percent"]

print("Core line coverage: %.2f%% (floor %.1f%%)" % (pct, floor))
sys.exit(0 if pct >= floor else 1)
