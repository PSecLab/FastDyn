from time import sleep
import math
from gz.msgs10.model_pb2 import Model
from gz.msgs10.empty_pb2 import Empty
from gz.msgs10.stringmsg_pb2 import StringMsg
from gz.msgs10.magnetometer_pb2 import Magnetometer
from gz.msgs10.navsat_pb2 import NavSat
from gz.msgs10.pose_pb2 import Pose
from gz.msgs10.boolean_pb2 import Boolean
from gz.transport13 import Node
from google.protobuf import text_format

def call_step():
    node = Node()
    service_name = "/step_simulation"
    request = Empty()
    timeout = 5000  # ms

    result, response = node.request(
        service_name,
        request,
        Empty,
        StringMsg,
        timeout
    )

    if result:
        pass
    else:
        print("Service call failed or timed out")

def call_get_navsat():
    node = Node()
    service_name = "/get_navsat_reading" # get_trace
    request = Empty()
    timeout = 5000  # ms

    result, response = node.request(
        service_name,
        request,
        Empty,
        NavSat, # stringmsg (JSON)
        timeout
    )

    if result:
        print(f"Result to string: {response}")
        # print(f"Latitude: {response.latitude}, Longitude: {response.longitude}, Altitude: {response.altitude}")
    else:
        print("Service call failed or timed out")

def set_pose(yaw_deg):
    node = Node()

    service_name = "/world/runway/set_pose"

    pose_msg = Pose()
    pose_msg.name = "r1_rover"

    # Position (same as your C++)
    pose_msg.position.x = 0.0
    pose_msg.position.y = 0.0
    pose_msg.position.z = 0.1

    # Yaw-only quaternion
    yaw_rad = math.radians(yaw_deg)
    pose_msg.orientation.x = 0.0
    pose_msg.orientation.y = 0.0
    pose_msg.orientation.z = math.sin(yaw_rad / 2.0)
    pose_msg.orientation.w = math.cos(yaw_rad / 2.0)

    timeout = 5000  # ms

    result, response = node.request(
        service_name,
        pose_msg,
        Pose,
        Boolean,
        timeout
    )

    if not result:
        print("set_pose failed or timed out")
    else:
        print(f"Pose set: yaw={yaw_deg}°")

def read_magnetometer():
    """
    Request: gz.Msgs.Empty
    Response: gz.Msgs.Magnetometer
    """
    node = Node()
    service_name = "/get_mag_reading"

    request = Empty()
    timeout = 5000  # ms

    result, response = node.request(
        service_name,
        request,
        Empty,
        Magnetometer,
        timeout
    )

    if result:
        print(f"Magnetometer reading: X={response.field_tesla.x}, Y={response.field_tesla.y}, Z={response.field_tesla.z}")
    else:
        print("Service call failed or timed out")

def main():
    import sys
    if len(sys.argv) < 2:
        print("Usage: python python_stepper.py <yaw_degrees>")
        return

    initial_yaw_deg = float(sys.argv[1])
    curr_yaw_deg = initial_yaw_deg

    set_pose(0.0)
    sleep(1)  # Allow some time for the pose to be set
    read_magnetometer()

    # while curr_yaw_deg < initial_yaw_deg + 360:
    #     set_pose(curr_yaw_deg)
    #     sleep(1)  # Allow some time for the pose to be set
    #     read_magnetometer()
    #     # call_get_navsat()
    #     # call_step()
    #     curr_yaw_deg += 10  # Increment yaw by 10 degrees


if __name__ == "__main__":
    main()