#!/bin/bash

# Run the Gazebo simulation and attach the necessary services
# Usage: ./run_and_attach_services.sh <vehicle_type>

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../../../.." && pwd)"

export GZ_SIM_SYSTEM_PLUGIN_PATH=$PROJECT_ROOT/third_party/courbet_deps/ardupilot_gazebo/build:$GZ_SIM_SYSTEM_PLUGIN_PATH
export GZ_SIM_RESOURCE_PATH=$PROJECT_ROOT/third_party/courbet_deps/ardupilot_gazebo/models:$PROJECT_ROOT/third_party/courbet_deps/ardupilot_gazebo/worlds:$GZ_SIM_RESOURCE_PATH
export GZ_SIM_RESOURCE_PATH=$PROJECT_ROOT/third_party/courbet_deps/SITL_Models/Gazebo/models:$PROJECT_ROOT/third_party/courbet_deps/SITL_Models/Gazebo/worlds:$GZ_SIM_RESOURCE_PATH

export GZ_PARTITION="courbet"
export GZ_IP=127.0.0.1

VEHICLE_TYPE=$1
if [ -z "$VEHICLE_TYPE" ]; then
    echo "Usage: $0 <vehicle_type> [headless]"
    echo "Example: $0 rover"
    echo "Example: $0 plane headless"
    exit 1
fi

HEADLESS=$2
CMD_STRING="gz sim -r"
if [ "$HEADLESS" == "headless" ]; then
    CMD_STRING="$CMD_STRING -s"
elif [ -n "$HEADLESS" ]; then
    echo "Usage: $0 <vehicle_type> [headless]"
    echo "Example: $0 rover"
    echo "Example: $0 plane headless"
    exit 1
fi

MODEL_NAME=""

# Start Gazebo with the specified world file
if [ "$VEHICLE_TYPE" == "copter-heli" ]; then
    $CMD_STRING bicopter_runway.sdf &
    MODEL_NAME="bicopter"
elif [ "$VEHICLE_TYPE" == "copter" ]; then
    # gz sim -r gs_drone_runway.sdf &
    # MODEL_NAME="gs_drone"
    $CMD_STRING iris_runway.sdf &
    MODEL_NAME="iris"
elif [ "$VEHICLE_TYPE" == "rover" ]; then
    $CMD_STRING r1_rover_runway.sdf &
    MODEL_NAME="r1_rover"
elif [ "$VEHICLE_TYPE" == "boat" ]; then
    $CMD_STRING waves.sdf &
    MODEL_NAME="blueboat"
elif [ "$VEHICLE_TYPE" == "plane" ]; then
    $CMD_STRING vtail_runway.sdf &
    MODEL_NAME="vtail_plane"
elif [ "$VEHICLE_TYPE" == "sub" ]; then
    $CMD_STRING bluerov2_underwater.world &
    MODEL_NAME="bluerov2"
else
    echo "Error: Unsupported vehicle type '$VEHICLE_TYPE'"
    echo "Supported types are: copter-heli, copter, rover, boat, plane, sub"
    exit 1
fi
GAZEBO_PID=$!
echo "Started Gazebo with PID $GAZEBO_PID"
echo "Using model name: $MODEL_NAME"
sleep 5  # Wait for Gazebo to initialize

# Attach the ArduRover services to the Gazebo simulation
# Assuming the services are compiled and available as executables in build directory
cd build
./services "$MODEL_NAME"
SERVICES_PID=$!
echo "Started ArduRover services with PID $SERVICES_PID"
sleep 5  # Wait for services to initialize

# Kill Gazebo and services on exit and ctrl-c
trap "echo 'Stopping Gazebo and services...'; kill -9 $GAZEBO_PID $SERVICES_PID; exit" SIGINT SIGTERM EXIT
wait $GAZEBO_PID $SERVICES_PID
echo "Gazebo and services have exited."
