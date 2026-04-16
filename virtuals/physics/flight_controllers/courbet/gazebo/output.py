from time import sleep
import math
from gz.msgs10.model_pb2 import Model
from gz.msgs10.empty_pb2 import Empty
from gz.msgs10.stringmsg_pb2 import StringMsg
from gz.msgs10.magnetometer_pb2 import Magnetometer
from gz.msgs10.navsat_pb2 import NavSat
from gz.msgs10.time_pb2 import Time
from gz.msgs10.pose_pb2 import Pose
from gz.msgs10.boolean_pb2 import Boolean
from gz.transport13 import Node
from google.protobuf import text_format

def call_noise_determinism_test():
    node = Node()
    service_name = "/get_deterministic_noise_test"
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
        print(f"Result to string: \n{response.data}")
    else:
        print("Service call failed or timed out")


if __name__ == "__main__":
    call_noise_determinism_test()