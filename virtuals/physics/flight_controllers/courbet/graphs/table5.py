import matplotlib.pyplot as plt
import numpy as np

# === Data (seconds) ===
metrics = ["Metric A", "Metric B"]

color_ours = "#1f77b4"   # standard matplotlib blue (clean + professional)
color_theirs = "#f0f0f0" # white-ish gray (neutral, less emphasis)

halucinator = np.array([1606.0, 31.561])   # HALucinator times
fastdyn = np.array([19.4, 0.02183])        # Your system times

# === Bar positions ===
x = np.arange(len(metrics))   # [0, 1]
width = 0.35                  # width of each bar

# === Plot ===
fig, ax = plt.subplots(figsize=(7, 4.5))

# Bars for each system (outline edge color added for better visibility on log scale)
bars1 = ax.bar(x - width/2, halucinator, width,
               label="HALucinator",
               color=color_theirs,
               hatch="//", edgecolor="black")   # pattern

bars2 = ax.bar(x + width/2, fastdyn, width,
               label="COURBET",
               color=color_ours)   # different pattern

# === Formatting === labels 14 pt, title 16 pt
ax.set_yscale("log")
ax.set_ylabel("Time (seconds, log scale)", fontsize=14)
ax.set_title("Performance Comparison", fontsize=16)

ax.set_xticks(x)
ax.set_xticklabels(metrics, fontsize=14)

ax.legend()

# Optional: grid helps readability on log scale
# ax.grid(True, which="both", linestyle="--", alpha=0.4)

plt.tight_layout()
# save the figure
plt.savefig("performance_comparison.png", dpi=300)
