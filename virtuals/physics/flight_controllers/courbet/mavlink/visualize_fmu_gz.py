import matplotlib.pyplot as plt

# ============================================================
# Measured totals for 100 calls
# ============================================================
num_calls = 100

fmu_total_s = 0.036542
gazebo_total_s = 0.665863

# ============================================================
# Derived metrics
# ============================================================
fmu_total_ms = fmu_total_s * 1000
gazebo_total_ms = gazebo_total_s * 1000

fmu_avg_ms = fmu_total_ms / num_calls
gazebo_avg_ms = gazebo_total_ms / num_calls

speedup = gazebo_total_s / fmu_total_s

# ============================================================
# Figure 1: total time
# ============================================================
fig, ax = plt.subplots(figsize=(8, 6))
labels = ["FMU", "Gazebo"]
totals = [fmu_total_ms, gazebo_total_ms]

bars = ax.bar(labels, totals)
ax.set_ylim(0, max(totals) * 1.2)
ax.set_ylabel("Total Time (ms)")
ax.set_title("Altimeter Model: 100 Calls Total Runtime")

for bar, total_s, avg_ms in zip(
    bars,
    [fmu_total_s, gazebo_total_s],
    [fmu_avg_ms, gazebo_avg_ms]
):
    height = bar.get_height()
    ax.text(
        bar.get_x() + bar.get_width() / 2,
        height,
        f"{total_s:.6f} s\n{avg_ms:.3f} ms/call",
        ha="center",
        va="bottom",
        fontsize=10
    )

ax.text(
    0.5,
    0.93,
    f"Speedup: {speedup:.2f}x",
    transform=ax.transAxes,
    ha="center",
    va="top",
    fontsize=11,
    bbox=dict(boxstyle="round", alpha=0.2)
)

plt.tight_layout()
plt.show()

# ============================================================
# Figure 2: average time per call
# ============================================================
fig, ax = plt.subplots(figsize=(8, 6))
avg_times = [fmu_avg_ms, gazebo_avg_ms]

bars = ax.bar(labels, avg_times)
ax.set_ylabel("Average Time per Call (ms)")
ax.set_title("Altimeter Model: Average Runtime per Call")

for bar, avg_ms in zip(bars, avg_times):
    height = bar.get_height()
    ax.text(
        bar.get_x() + bar.get_width() / 2,
        height,
        f"{avg_ms:.3f} ms",
        ha="center",
        va="bottom",
        fontsize=10
    )

plt.tight_layout()
plt.show()