#!/bin/bash

# Run the Gazebo simulation and attach the necessary services
# Usage: ./run_and_attach_services.sh <vehicle_type>

VEHICLE_TYPE=$1
if [ -z "$VEHICLE_TYPE" ]; then
    echo "Usage: $0 <vehicle_type>"
    echo "Example: $0 rover"
    exit 1
fi

MODEL_NAME=""

# Start Gazebo with the specified world file
if [ "$VEHICLE_TYPE" == "copter" ]; then
    # gz sim -r gs_drone_runway.sdf &
    gz sim -r bicopter_runway.sdf &
    # gz sim -r skywalker_x8_quad_runway.sdf &
    # MODEL_NAME="skywalker_x8_quad"
    MODEL_NAME="bicopter"
elif [ "$VEHICLE_TYPE" == "rover" ]; then
    gz sim -r r1_rover_runway.sdf &
    MODEL_NAME="r1_rover"
elif [ "$VEHICLE_TYPE" == "boat" ]; then
    gz sim -r waves.sdf &
    MODEL_NAME="blueboat"
elif [ "$VEHICLE_TYPE" == "plane" ]; then
    gz sim -r vtail_runway.sdf &
    # gz sim -r skywalker_x8_runway.sdf &
    MODEL_NAME="vtail_plane"
else
    echo "Error: Unsupported vehicle type '$VEHICLE_TYPE'"
    echo "Supported types are: copter, rover, boat, plane"
    exit 1
fi
GAZEBO_PID=$!
echo "Started Gazebo with PID $GAZEBO_PID"
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