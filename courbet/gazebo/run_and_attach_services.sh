#!/bin/bash

# Run the Gazebo simulation and attach the necessary services
# Usage: ./run_and_attach_services.sh <vehicle_type>

VEHICLE_TYPE=$1
if [ -z "$VEHICLE_TYPE" ]; then
    echo "Usage: $0 <vehicle_type>"
    echo "Example: $0 rover"
    exit 1
fi

# check that the vehicle type is rover, copter, plane, or boat
if [ "$VEHICLE_TYPE" != "rover" ] && [ "$VEHICLE_TYPE" != "copter" ] && [ "$VEHICLE_TYPE" != "plane" ] && [ "$VEHICLE_TYPE" != "boat" ];  then
    echo "Error: vehicle_type must be 'rover', 'copter', 'plane', or 'boat'"
    exit 1
fi

# Start Gazebo with the specified world file
if [ "$VEHICLE_TYPE" == "copter" ]; then
    gz sim -r r1_rover_runway.sdf &
elif [ "$VEHICLE_TYPE" == "rover" ]; then
    gz sim -r r1_rover_runway.sdf &
elif [ "$VEHICLE_TYPE" == "boat" ]; then
    echo "Boat world not yet implemented"
elif [ "$VEHICLE_TYPE" == "plane" ]; then
    gz sim -r vtail_runway.sdf &
else
    echo "Error: Unsupported vehicle type '$VEHICLE_TYPE'"
    exit 1
fi
GAZEBO_PID=$!
echo "Started Gazebo with PID $GAZEBO_PID"
sleep 5  # Wait for Gazebo to initialize

# Attach the ArduRover services to the Gazebo simulation
# Assuming the services are compiled and available as executables in build directory
cd build
./services
SERVICES_PID=$!
echo "Started ArduRover services with PID $SERVICES_PID"
sleep 5  # Wait for services to initialize

# Kill Gazebo and services on exit and ctrl-c
trap "echo 'Stopping Gazebo and services...'; kill -9 $GAZEBO_PID $SERVICES_PID; exit" SIGINT SIGTERM EXIT
wait $GAZEBO_PID $SERVICES_PID
echo "Gazebo and services have exited."