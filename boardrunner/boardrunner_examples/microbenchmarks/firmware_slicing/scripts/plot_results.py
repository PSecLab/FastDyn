#!/usr/bin/env python3
"""
Bar chart comparing FastDyn against representative baselines on the
firmware-slicing and HITL microbenchmarks. Log-scale Y (seconds per
1000-op run). Baseline bars are hatched gray, FastDyn bars are solid blue.

Update the DATA table below with fresh means/stddevs from the runners:
  * Firmware slicing FastDyn numbers come from
    `run_benchmarks.sh` (ns/op x 1000 ops/run -> seconds/run).
  * Halucinator baseline is the wall time of one bench_start->bench_done
    iteration on the same 1000-op firmware.
  * HITL numbers are (ms/op x 1000 ops/run -> seconds/run).

Usage:
    python3 plot_results.py                       # writes plot.pdf
    python3 plot_results.py -o results/plot.pdf   # custom path
    python3 plot_results.py -o plot.png --dpi 200 # raster instead of vector
"""
import argparse
from pathlib import Path

import matplotlib.pyplot as plt
import matplotlib.ticker as mticker
from matplotlib.patches import Patch

# ----------------------------------------------------------------------------
# Data: (label, group, mean_seconds, stddev_seconds, is_baseline)
# Seconds per one N=1000-op run.
# ----------------------------------------------------------------------------
DATA = [
    # Slicing (PC update): HALucinator's PC-rewrite intercept vs FastDyn's
    # inline modifier (r15 = r14) that jumps back to the caller.
    # n=100 iters each. Per-run seconds = per-op ns * 1000 / 1e9 for FastDyn.
    ("HALucinator",         "Slicing (PC Update)", 40.3546,   0.0041,   True),
    ("FastDyn\n(modifier)", "Slicing (PC Update)", 102.11e-6, 14.64e-6, False),
    # Slicing (Counter update): HALucinator's counter-increment observation
    # vs FastDyn's plugin virtual (bench_tick increments a static counter).
    ("HALucinator",         "Slicing (Counter)",   10.1420,   0.0056,   True),
    ("FastDyn\n(virtual)",  "Slicing (Counter)",   100.89e-6, 13.50e-6, False),
    # HITL Execution: Avatar2 OpenOCD bridge vs FastDyn direct probe.
    # Both baseline and FastDyn are avgs of read+write directions.
    ("Avatar2",             "HITL Execution",      10.0766,   0.0054,   True),
    ("FastDyn",             "HITL Execution",       0.50645,  0.00045,  False),
]

BASELINE_COLOR = "#d9d9d9"
FASTDYN_COLOR  = "#2b6cb0"


def fmt_time(seconds: float) -> str:
    """Human-friendly time label for the bar tops."""
    if seconds >= 1:
        return f"{seconds:.2f}s"
    if seconds >= 1e-3:
        return f"{seconds * 1e3:.2f}ms"
    if seconds >= 1e-6:
        return f"{seconds * 1e6:.2f}us"
    return f"{seconds * 1e9:.2f}ns"


def _draw_group(ax, rows, title):
    """One subplot per benchmark group. Y-limits fit each group's range."""
    xs, means, errs, colors, hatches, xlabels = [], [], [], [], [], []
    for i, (lbl, _, m, s, is_base) in enumerate(rows):
        xs.append(i)
        means.append(m)
        errs.append(s)
        colors.append(BASELINE_COLOR if is_base else FASTDYN_COLOR)
        hatches.append("//" if is_base else "")
        xlabels.append(lbl)

    # Error bars: chunky caps so they read on log scale where +/-10% is a
    # tiny visual delta. Thick lines with visible caps mimic the Figure 8
    # style the reviewers asked for.
    bars = ax.bar(xs, means, width=0.7, yerr=errs, capsize=6,
                  ecolor="black", error_kw={"elinewidth": 1.5, "capthick": 1.5},
                  color=colors, edgecolor="black", linewidth=0.9)
    for bar, hatch in zip(bars, hatches):
        bar.set_hatch(hatch)

    ax.set_yscale("log")
    ax.set_xticks(xs)
    ax.set_xticklabels(xlabels, fontsize=9)
    ax.set_title(title, fontweight="bold", pad=10)

    # Tight y-limits per subplot: one decade below min, two decades above max
    # (extra headroom on top for the value labels).
    lo = min(means) / 10
    hi = max(means) * 100
    ax.set_ylim(lo, hi)
    ax.yaxis.set_major_locator(mticker.LogLocator(base=10, numticks=10))
    ax.grid(True, axis="y", which="major", linestyle="--", alpha=0.4)
    ax.set_axisbelow(True)

    # Bar-top label: mean, plus "+/- sigma" when we have variance data. The
    # +/- carries the info the log-compressed error bar cannot.
    for x, m, s in zip(xs, means, errs):
        if s > 0:
            txt = f"{fmt_time(m)}\n(+/- {fmt_time(s)})"
        else:
            txt = fmt_time(m)
        ax.text(x, m * 1.6, txt, ha="center", va="bottom", fontsize=9)


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("-o", "--output", default="plot.pdf",
                    help="Output path; extension chooses format (pdf/png/svg).")
    ap.add_argument("--dpi", type=int, default=150)
    args = ap.parse_args()

    plt.rcParams.update({
        "font.family":       "serif",
        "font.size":         11,
        "axes.spines.top":   False,
        "axes.spines.right": False,
    })

    # Group rows in DATA order, one subplot per group.
    groups = {}
    for row in DATA:
        groups.setdefault(row[1], []).append(row)
    group_names = list(groups.keys())

    n = len(group_names)
    # Width scales with total bar count; small side-by-side.
    total_bars = sum(len(rs) for rs in groups.values())
    fig, axes = plt.subplots(1, n, figsize=(1.6 * total_bars + 1.5, 4.5),
                             gridspec_kw={"width_ratios": [len(rs) for rs in groups.values()]})
    if n == 1:
        axes = [axes]

    for ax, name in zip(axes, group_names):
        _draw_group(ax, groups[name], name)

    axes[0].set_ylabel("Execution Time (s) - Log Scale")

    # Single legend at the top, shared across subplots.
    fig.legend(
        handles=[
            Patch(facecolor=BASELINE_COLOR, edgecolor="black",
                  hatch="//", label="Baseline"),
            Patch(facecolor=FASTDYN_COLOR, edgecolor="black",
                  label="FastDyn"),
        ],
        loc="upper center", ncol=2, frameon=True,
        bbox_to_anchor=(0.5, 1.02),
    )

    out = Path(args.output)
    out.parent.mkdir(parents=True, exist_ok=True)
    fig.tight_layout(rect=(0, 0, 1, 0.95))
    plt.savefig(out, bbox_inches="tight", dpi=args.dpi)
    print(f"wrote {out}")


if __name__ == "__main__":
    main()
