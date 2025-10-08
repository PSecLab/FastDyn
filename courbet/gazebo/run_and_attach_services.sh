#!/bin/bash

# Run the Gazebo simulation and attach the necessary services
# Usage: ./run_and_attach_services.sh

# Start Gazebo with the specified world file
gz sim -r r1_rover_runway.sdf &
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