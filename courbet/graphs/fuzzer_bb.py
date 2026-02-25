import re
import matplotlib.pyplot as plt

# --- Read your real log ---
with open("basic_block_log.txt") as f:
    log = f.read()

times_ms = []
blocks = []

for line in log.splitlines():
    m = re.search(r"Unique blocks = (\d+) at time (\d+)", line)
    if m:
        blocks.append(int(m.group(1)))
        times_ms.append(int(m.group(2)))

# Normalize time: first timestamp = 0, convert ms -> hours
t0 = times_ms[0]
times_hours = [(t - t0) / (1000.0 * 60.0 * 60.0) for t in times_ms]

# Plot
plt.figure()
plt.plot(times_hours, blocks, marker="o", linestyle="--", alpha=0.2)
plt.xlabel("Time (hours)")
plt.ylabel("Basic Blocks")
plt.title("Number of Unique Blocks Hit vs. Time")
plt.grid(True)

# Save
plt.savefig("coverage-over-time.png", dpi=200, bbox_inches="tight")
plt.close()