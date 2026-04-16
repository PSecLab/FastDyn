import math
import threading
import time

import matplotlib.pyplot as plt
from matplotlib.animation import FuncAnimation
from matplotlib.patches import Rectangle, Polygon
from pymavlink import mavutil

# ============================================================
# Config
# ============================================================
MAVLINK_CONNECTION = "udp:127.0.0.1:14560"

# Max range shown on the UI
MAX_DISTANCE_M = 16.0
MAX_DISTANCE_CM = int(MAX_DISTANCE_M * 100)

# We only visualize ArduPilot-style sectors 0..7
SECTORS = list(range(8))

# orientation -> label / display angle
# Angle is only used for placement around the rover
SECTOR_INFO = {
    0: {"label": "Front",       "angle_deg": 90},
    1: {"label": "Front-Right", "angle_deg": 45},
    2: {"label": "Right",       "angle_deg": 0},
    3: {"label": "Back-Right",  "angle_deg": 315},
    4: {"label": "Back",        "angle_deg": 270},
    5: {"label": "Back-Left",   "angle_deg": 225},
    6: {"label": "Left",        "angle_deg": 180},
    7: {"label": "Front-Left",  "angle_deg": 135},
}

# ============================================================
# Shared state
# ============================================================
latest = {
    sector: {
        "distance_cm": MAX_DISTANCE_CM,
        "timestamp": 0.0,
        "sensor_id": None,
    }
    for sector in SECTORS
}

lock = threading.Lock()


# ============================================================
# MAVLink reader
# ============================================================
def mavlink_reader():
    print(f"Connecting to MAVLink stream at {MAVLINK_CONNECTION}")
    master = mavutil.mavlink_connection(MAVLINK_CONNECTION)
    master.wait_heartbeat()
    print("Heartbeat received. Listening for DISTANCE_SENSOR messages...")

    while True:
        msg = master.recv_match(type="DISTANCE_SENSOR", blocking=True, timeout=1)
        if msg is None:
            continue

        orientation = int(msg.orientation)
        if orientation not in SECTORS:
            continue

        distance_cm = int(msg.current_distance)

        # Clamp into [0, MAX_DISTANCE_CM]
        distance_cm = max(0, min(distance_cm, MAX_DISTANCE_CM))

        with lock:
            latest[orientation] = {
                "distance_cm": distance_cm,
                "timestamp": time.time(),
                "sensor_id": int(msg.id),
            }


# ============================================================
# Drawing helpers
# ============================================================
def polar_to_xy(angle_deg, radius):
    rad = math.radians(angle_deg)
    return radius * math.cos(rad), radius * math.sin(rad)


def draw_rover(ax):
    # Rover body centered at origin
    body_w = 1.8
    body_h = 2.6
    ax.add_patch(
        Rectangle(
            (-body_w / 2, -body_h / 2),
            body_w,
            body_h,
            fill=False,
            linewidth=2
        )
    )

    # Wheels
    wheel_w = 0.25
    wheel_h = 0.7
    wheel_positions = [
        (-1.15,  0.8),
        (-1.15, -0.8),
        ( 0.90,  0.8),
        ( 0.90, -0.8),
    ]
    for x, y in wheel_positions:
        ax.add_patch(Rectangle((x, y - wheel_h / 2), wheel_w, wheel_h, fill=False, linewidth=1.5))

    # Arrow showing rover front
    arrow = Polygon(
        [[0.0, body_h / 2 + 0.55], [-0.28, body_h / 2 + 0.15], [0.28, body_h / 2 + 0.15]],
        closed=True,
        fill=False,
        linewidth=2
    )
    ax.add_patch(arrow)

    ax.text(0, 0, "ROVER", ha="center", va="center", fontsize=11, fontweight="bold")


def draw_sector_bar(ax, sector, distance_cm):
    info = SECTOR_INFO[sector]
    angle_deg = info["angle_deg"]
    label = info["label"]

    # Convert reading to meters
    distance_m = distance_cm / 100.0
    distance_m = max(0.0, min(distance_m, MAX_DISTANCE_M))

    # "Closeness" fill: closer obstacle -> fuller bar
    closeness = 1.0 - (distance_m / MAX_DISTANCE_M)
    closeness = max(0.0, min(closeness, 1.0))

    # Bar geometry
    bar_length = 2.8
    bar_height = 0.7

    # Position the bar around the rover
    cx, cy = polar_to_xy(angle_deg, 5.2)

    # To orient rectangles around the rover, we use a local axis
    theta = math.radians(angle_deg)
    ux, uy = math.cos(theta), math.sin(theta)         # bar direction
    vx, vy = -math.sin(theta), math.cos(theta)       # perpendicular

    def rect_corners(center_x, center_y, length, height):
        hl = length / 2.0
        hh = height / 2.0
        return [
            (center_x - hl * ux - hh * vx, center_y - hl * uy - hh * vy),
            (center_x + hl * ux - hh * vx, center_y + hl * uy - hh * vy),
            (center_x + hl * ux + hh * vx, center_y + hl * uy + hh * vy),
            (center_x - hl * ux + hh * vx, center_y - hl * uy + hh * vy),
        ]

    # Outline box
    outline = rect_corners(cx, cy, bar_length, bar_height)
    ax.add_patch(Polygon(outline, closed=True, fill=False, linewidth=2))

    # Filled portion
    # Fill from the "far end" toward the rover, so more fill visually implies danger
    fill_length = bar_length * closeness
    if fill_length > 0.001:
        # Shift fill center so it hugs the rover-facing side
        # rover-facing side is opposite the angle direction, so shift backward along u
        fill_center_shift = -(bar_length - fill_length) / 2.0
        fill_cx = cx + fill_center_shift * ux
        fill_cy = cy + fill_center_shift * uy
        fill_poly = rect_corners(fill_cx, fill_cy, fill_length, bar_height)
        ax.add_patch(Polygon(fill_poly, closed=True, fill=True, alpha=0.45))

    # Label
    lx, ly = polar_to_xy(angle_deg, 6.5)
    ax.text(lx, ly + 0.25, f"{sector}: {label}", ha="center", va="center", fontsize=9)

    # Numeric distance
    dx, dy = polar_to_xy(angle_deg, 6.5)
    ax.text(dx, dy - 0.2, f"{distance_m:0.1f} m", ha="center", va="center", fontsize=9)


def draw_legend(ax):
    ax.text(
        -8.8, -8.8,
        "Bar fill meaning:\n"
        "empty = far / clear\n"
        "full = obstacle close\n"
        f"scale: 0 to {MAX_DISTANCE_M:.0f} m",
        fontsize=9,
        va="bottom"
    )


# ============================================================
# Animation update
# ============================================================
def update(_frame):
    ax.clear()
    ax.set_aspect("equal")
    ax.set_xlim(-10, 10)
    ax.set_ylim(-10, 10)
    ax.axis("off")
    ax.set_title("Live Lidar Sector View from MAVLink DISTANCE_SENSOR", fontsize=14)

    # Read a snapshot
    with lock:
        snapshot = {k: v.copy() for k, v in latest.items()}

    # Draw rover
    draw_rover(ax)

    # Draw subtle rings for context
    for r in [3.5, 5.2, 7.0]:
        circle = plt.Circle((0, 0), r, fill=False, linewidth=0.6, alpha=0.35)
        ax.add_patch(circle)

    # Draw sectors
    for sector in SECTORS:
        draw_sector_bar(ax, sector, snapshot[sector]["distance_cm"])

    draw_legend(ax)

    # Last update age
    newest = max(v["timestamp"] for v in snapshot.values())
    age = time.time() - newest if newest > 0 else float("inf")
    age_str = f"{age:0.2f}s" if math.isfinite(age) else "no data"
    ax.text(6.2, -9.0, f"Last data age: {age_str}", fontsize=9, ha="left")


# ============================================================
# Main
# ============================================================
if __name__ == "__main__":
    t = threading.Thread(target=mavlink_reader, daemon=True)
    t.start()

    fig, ax = plt.subplots(figsize=(9, 9))
    ani = FuncAnimation(fig, update, interval=100, cache_frame_data=False)
    plt.show()