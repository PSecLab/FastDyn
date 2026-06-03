#!/bin/bash

set -u

VEHICLE_TYPE="${1:-}"
HEADLESS="${2:-}"

if [ -z "$VEHICLE_TYPE" ]; then
    echo "Usage: $0 <vehicle_type> [headless]"
    echo "Example: $0 rover"
    echo "Example: $0 plane headless"
    exit 1
fi

GZ_CMD=(gz sim -r)

if [ "$HEADLESS" == "headless" ]; then
    GZ_CMD+=( -s )
elif [ -n "$HEADLESS" ]; then
    echo "Usage: $0 <vehicle_type> [headless]"
    echo "Example: $0 rover"
    echo "Example: $0 plane headless"
    exit 1
fi

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR" || exit 1

# Keep these relative, but canonicalize before giving them to Gazebo.
SITL_GAZEBO_REL="../../../../../third_party/courbet_deps/SITL_Models/Gazebo"
ARDUPILOT_GAZEBO_BUILD_REL="../../../../../third_party/courbet_deps/ardupilot_gazebo/build"

SITL_GAZEBO="$(realpath "$SCRIPT_DIR/$SITL_GAZEBO_REL")"
ARDUPILOT_GAZEBO_BUILD="$(realpath "$SCRIPT_DIR/$ARDUPILOT_GAZEBO_BUILD_REL")"

MODEL_NAME=""
WORLD_FILE=""

case "$VEHICLE_TYPE" in
    copter-heli)
        WORLD_FILE="worlds/bicopter_runway.sdf"
        MODEL_NAME="bicopter"
        ;;
    copter)
        WORLD_FILE="worlds/iris_runway.sdf"
        MODEL_NAME="iris"
        ;;
    rover)
        WORLD_FILE="worlds/r1_rover_runway.sdf"
        MODEL_NAME="r1_rover"
        ;;
    boat)
        WORLD_FILE="worlds/waves.sdf"
        MODEL_NAME="blueboat"
        ;;
    plane)
        WORLD_FILE="worlds/vtail_runway.sdf"
        MODEL_NAME="vtail_plane"
        ;;
    sub)
        WORLD_FILE="worlds/bluerov2_underwater.world"
        MODEL_NAME="bluerov2"
        ;;
    *)
        echo "Error: Unsupported vehicle type '$VEHICLE_TYPE'"
        echo "Supported types are: copter-heli, copter, rover, boat, plane, sub"
        exit 1
        ;;
esac

if [ ! -d "$SITL_GAZEBO" ]; then
    echo "Error: SITL Gazebo dir not found:"
    echo "  $SITL_GAZEBO"
    exit 1
fi

if [ ! -f "$SITL_GAZEBO/$WORLD_FILE" ]; then
    echo "Error: world file not found:"
    echo "  $SITL_GAZEBO/$WORLD_FILE"
    exit 1
fi

if [ ! -x "$SCRIPT_DIR/build/services" ]; then
    echo "Error: services binary not found:"
    echo "  $SCRIPT_DIR/build/services"
    exit 1
fi

if [ ! -f "$ARDUPILOT_GAZEBO_BUILD/libArduPilotPlugin.so" ]; then
    echo "Warning: libArduPilotPlugin.so not found:"
    echo "  $ARDUPILOT_GAZEBO_BUILD/libArduPilotPlugin.so"
fi

echo "Using SITL Gazebo dir: $SITL_GAZEBO"
echo "Using world file: $SITL_GAZEBO/$WORLD_FILE"
echo "Using model name: $MODEL_NAME"
echo "Using ArduPilot Gazebo plugin dir: $ARDUPILOT_GAZEBO_BUILD"

cleanup() {
    echo "Stopping Gazebo and services..."
    kill -9 "${GAZEBO_PID:-}" "${SERVICES_PID:-}" 2>/dev/null || true
}

trap cleanup SIGINT SIGTERM EXIT

# Start Gazebo with a CLEAN environment.
# Do not append old GZ_SIM_RESOURCE_PATH values.
(
    cd "$SITL_GAZEBO" || exit 1

    unset GZ_SIM_RESOURCE_PATH
    unset IGN_GAZEBO_RESOURCE_PATH
    unset GZ_SIM_SYSTEM_PLUGIN_PATH
    unset IGN_GAZEBO_SYSTEM_PLUGIN_PATH

    export GZ_SIM_RESOURCE_PATH="$SITL_GAZEBO/models:$SITL_GAZEBO/worlds:$SITL_GAZEBO"
    export IGN_GAZEBO_RESOURCE_PATH="$GZ_SIM_RESOURCE_PATH"

    export GZ_SIM_SYSTEM_PLUGIN_PATH="$ARDUPILOT_GAZEBO_BUILD"
    export IGN_GAZEBO_SYSTEM_PLUGIN_PATH="$GZ_SIM_SYSTEM_PLUGIN_PATH"

    echo "Gazebo cwd: $(pwd)"
    echo "GZ_SIM_RESOURCE_PATH=$GZ_SIM_RESOURCE_PATH"
    echo "GZ_SIM_SYSTEM_PLUGIN_PATH=$GZ_SIM_SYSTEM_PLUGIN_PATH"

    "${GZ_CMD[@]}" "$SITL_GAZEBO/$WORLD_FILE"
) &

GAZEBO_PID=$!
echo "Started Gazebo with PID $GAZEBO_PID"

sleep 5

cd "$SCRIPT_DIR/build" || exit 1

./services "$MODEL_NAME" &
SERVICES_PID=$!

echo "Started ArduPilot services with PID $SERVICES_PID"

wait "$GAZEBO_PID" "$SERVICES_PID"
