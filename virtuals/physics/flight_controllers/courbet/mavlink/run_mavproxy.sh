#!/bin/bash

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
FASTDYN_ROOT="$(realpath "$SCRIPT_DIR/../../../../../")"

source "$FASTDYN_ROOT/fastdyn-env/bin/activate"

# Neglect user-site package
export PYTHONNOUSERSITE=1

# Only use wxPython from system packages
export PYTHONPATH=/usr/lib/python3/dist-packages

mavproxy.py --master=udpout:127.0.0.1:14551 --map --console 
