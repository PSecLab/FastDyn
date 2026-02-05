import os
import csv

LOG_DIR = "robustness_logs"

for filename in os.listdir(LOG_DIR):
    if not filename.endswith(".csv"):
        continue

    filepath = os.path.join(LOG_DIR, filename)
    roll = None
    pitch = None

    with open(filepath, newline="") as f:
        reader = csv.DictReader(f)
        for row in reader:
            if row["Formula"] == "Roll_boundary":
                roll = float(row["Robustness"])
            elif row["Formula"] == "Pitch_boundary":
                pitch = float(row["Robustness"])

    if (roll is not None and roll < 0) or (pitch is not None and pitch < 0):
        print(filename)

