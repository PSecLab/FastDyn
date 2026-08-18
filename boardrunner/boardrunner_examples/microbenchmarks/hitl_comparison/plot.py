#!/usr/bin/env python3
"""Render an Avatar2-vs-FastDyn HITL bar chart from a run_all.sh CSV.

Two bars per chart: avatar2, fastdyn-passthrough. Log y-scale when the
ratio would otherwise crush the shorter bar to invisibility. Matches the
schema emitted by bench_avatar2.py and bench_fastdyn.sh."""
from __future__ import annotations

import argparse
import csv
import sys
from pathlib import Path

import matplotlib
matplotlib.use("Agg")  # no display required
import matplotlib.pyplot as plt

TOOLS_IN_ORDER = ("avatar2", "fastdyn-passthrough")


def _load(csv_path: Path) -> dict[tuple[str, str], float]:
    """Return {(tool, direction): per_op_ms} using the LAST row per key."""
    rows: dict[tuple[str, str], float] = {}
    with csv_path.open() as f:
        for row in csv.DictReader(f):
            rows[(row["tool"], row["direction"])] = float(row["per_op_ms"])
    return rows


def _bar(rows: dict, direction: str, out_png: Path, n: int) -> None:
    values = []
    for tool in TOOLS_IN_ORDER:
        key = (tool, direction)
        if key not in rows:
            raise KeyError(f"CSV missing row for tool={tool} direction={direction}")
        values.append(rows[key])

    fig, ax = plt.subplots(figsize=(4.2, 3.4))
    bars = ax.bar(TOOLS_IN_ORDER, values, color=["#4477aa", "#228833"])
    ax.set_ylabel("per-op cost (ms, log scale)")
    ax.set_title(f"HITL {direction}: cost per emulated MMIO op (N={n})")
    # Log-scale when the tall bar dominates the short one; otherwise linear.
    if max(values) / max(min(values), 1e-9) > 10:
        ax.set_yscale("log")
    for bar_, v in zip(bars, values):
        ax.text(bar_.get_x() + bar_.get_width() / 2, v,
                f"{v:.3f} ms", ha="center", va="bottom", fontsize=9)
    ax.margins(y=0.15)
    fig.tight_layout()
    fig.savefig(out_png, dpi=150)
    plt.close(fig)


def main() -> int:
    p = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    p.add_argument("--csv", required=True, type=Path, help="run_all.sh CSV")
    p.add_argument("--out-dir", type=Path, default=Path("."),
                   help="dir for reads.png / writes.png")
    args = p.parse_args()

    if not args.csv.is_file():
        print(f"plot.py: no such CSV: {args.csv}", file=sys.stderr)
        return 2
    args.out_dir.mkdir(parents=True, exist_ok=True)

    rows = _load(args.csv)
    # Emit one chart per direction present in the CSV.
    directions = {d for (_, d) in rows.keys()}
    # Infer N from any row (all rows in a run_all.sh CSV share it).
    n = 0
    with args.csv.open() as f:
        for row in csv.DictReader(f):
            n = int(row["iters"]); break
    for d in sorted(directions):
        out = args.out_dir / f"{d}.png"
        _bar(rows, d, out, n)
        print(f"wrote {out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
