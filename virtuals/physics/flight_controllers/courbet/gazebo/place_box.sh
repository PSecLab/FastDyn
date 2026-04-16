#!/bin/bash

# Check args
if [ "$#" -ne 2 ]; then
    echo "Usage: $0 <x> <y>"
    exit 1
fi

X=$1
Y=$2

# Fixed params
Z=0
SX=0.4
SY=0.4
SZ=1.2
NAME="cone_gate"

# Build JSON string
REQ=$(cat <<EOF
data: "{\"x\":$X,\"y\":$Y,\"z\":$Z,\"sx\":$SX,\"sy\":$SY,\"sz\":$SZ,\"name\":\"$NAME\"}"
EOF
)

# Call gz service
gz service -s /place_box_relative \
  --reqtype gz.msgs.StringMsg \
  --reptype gz.msgs.StringMsg \
  --timeout 2000 \
  --req "$REQ"