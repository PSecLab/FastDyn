from time import sleep
from gz.msgs10.model_pb2 import Model
from gz.msgs10.empty_pb2 import Empty
from gz.msgs10.stringmsg_pb2 import StringMsg
from gz.msgs10.magnetometer_pb2 import Magnetometer
from gz.msgs10.navsat_pb2 import NavSat
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

def main():
    try:
        while True:
            call_step()
    except KeyboardInterrupt:
        print("\nTest Stepper Exiting...")

if __name__ == "__main__":
    main()