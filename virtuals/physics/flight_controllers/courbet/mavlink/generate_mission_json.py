import json

VEHICLE_TYPE = "rover"

def generate_mission_items(simple_plan, start_seq=0):
    """Convert a simplified mission plan to MAVLink MISSION_ITEM JSON format."""
    mission = []
    seq = start_seq

    for i, step in enumerate(simple_plan):
        cmd = step["cmd"].lower()
        mission_item = {
            "seq": seq,
            "frame": 3,  # MAV_FRAME_GLOBAL_RELATIVE_ALT
            "command": None,
            "current": 1 if seq == 0 else 0,
            "autocontinue": 1,
            "param1": 0, "param2": 0, "param3": 0, "param4": 0,
            "x": 0, "y": 0, "z": 0
        }
        if cmd in ("start", "stop"):
            # Optional symbolic steps — ignore or handle as metadata
            continue
        if cmd == "takeoff":
            mission_item["command"] = 22  # MAV_CMD_NAV_TAKEOFF
            mission_item["z"] = step.get("alt", 10)
        elif cmd == "waypoint":
            mission_item["command"] = 16  # MAV_CMD_NAV_WAYPOINT
            mission_item["x"] = step["lat"]
            mission_item["y"] = step["lon"]
            if VEHICLE_TYPE != "rover":
                mission_item["z"] = step["alt"]
        elif cmd == "rtl":
            mission_item["command"] = 20  # MAV_CMD_NAV_RETURN_TO_LAUNCH
        elif cmd == "land":
            mission_item["command"] = 21  # MAV_CMD_NAV_LAND
            mission_item["x"] = step.get("lat", 0)
            mission_item["y"] = step.get("lon", 0)
            if VEHICLE_TYPE != "rover":
                mission_item["z"] = step.get("alt", 0)
        else:
            raise ValueError(f"Unknown command: {cmd}")

        mission.append(mission_item)
        seq += 1

    return mission

if __name__ == "__main__":
    # Example usage
    simple_rover_triangle_mission = [
        {"cmd": "start", "lat": 39.79480109851339, "lon": -84.08631702793322},  # Starting point
        {"cmd": "waypoint", "lat": 39.794901, "lon": -84.086317},  # ~11m North
        {"cmd": "waypoint", "lat": 39.794851, "lon": -84.086417},  # ~11m West
        {"cmd": "waypoint", "lat": 39.794751, "lon": -84.086317},  # ~11m South
        {"cmd": "waypoint", "lat": 39.79480109851339, "lon": -84.08631702793322},  # Return to start
        {"cmd": "stop"}
    ]

    mission_json = generate_mission_items(simple_rover_triangle_mission)
    # print(json.dumps(mission_json, indent=2))
    with open("mission.json", "w") as f:
        json.dump(mission_json, f, indent=2)

    print("Mission JSON generated successfully.")