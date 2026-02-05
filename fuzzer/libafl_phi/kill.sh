#!/bin/bash

pkill -9 run_and_attach_
pkill -9 python3
pkill -9 ruby
pkill -9 mavproxy.py
pkill -9 services
pkill -SIGINT qemu-system-arm
pkill -9 trace_recorder
