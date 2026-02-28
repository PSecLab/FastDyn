#!/usr/bin/env python3
import time
import gz.transport13 as gz
from gz.msgs10.wind_pb2 import Wind
from gz.msgs10.empty_pb2 import Empty

# world_name = "wind_demo"
world_name = "runway"
topic_wind = f"/world/{world_name}/wind"
service_wind_info = f"/world/{world_name}/wind_info"

# --- Node setup ---
node = gz.Node()
pub_wind = node.advertise(topic_wind, Wind)

# Helper to get current wind via service
def get_current_wind():
    req = Empty()  # service expects an Empty request
    # res = ind()   # service returns a Wind message
    result, response = node.request(
        service_wind_info,
        req,
        Empty,
        Wind,
        timeout=5
    )

    if result:
        print("Current wind:")
        print(f"  Velocity: x={response.linear_velocity.x:.2f}, y={response.linear_velocity.y:.2f}, z={response.linear_velocity.z:.2f}")
        # print(f"  Velocity: x={response.velocity.x:.2f}, y={response.velocity.y:.2f}, z={response.velocity.z:.2f}")
        # print(f"  Turbulence: {response.turbulence:.2f}, Gust: {response.gust:.2f}")
    else:
        print("Failed to get wind info")

# Helper to set new wind
def set_wind(vel_x=1.0, vel_y=0.0, vel_z=0.0, turbulence=0.0, gust=0.0):
    wind_msg = Wind()
    wind_msg.linear_velocity.x = vel_x
    wind_msg.linear_velocity.y = vel_y
    wind_msg.linear_velocity.z = vel_z
    # wind_msg.turbulence = turbulence
    # wind_msg.gust = gust
    pub_wind.publish(wind_msg)
    print(f"Set wind: x={vel_x}, y={vel_y}, z={vel_z}, turbulence={turbulence}, gust={gust}")

# --- Example usage ---
if __name__ == "__main__":
    print("Fetching initial wind...")
    get_current_wind()

    # Example: dynamically change wind in a loop
    try:
        for i in range(5):
            vel_x = 2.0 + i  # just a demo pattern
            vel_y = 0.5 * i
            vel_z = 0.0
            turbulence = 0.1 * i
            gust = 0.0
            set_wind(vel_x, vel_y, vel_z, turbulence, gust)
            time.sleep(1)  # wait a second
            get_current_wind()
    except KeyboardInterrupt:
        print("Exiting wind control script")
