#! /usr/bin/env python3

# Mission planner for Courbet rover

"""
Given a mission, implement it by sending commands to the rover.
"""

"""
mission: {
    "right turn" @ 3 seconds,
    "left turn" @ 13 seconds,
    "stop" @ 23 seconds
}
"""

def complete_mission(mission_file: str):
    with open(mission_file, 'r') as f:
        mission = f.readlines()

    commands_list = []

    for command in mission:
        # TODO: Time order all commands and put them in list

    while True:
        # polling sim time
        # check whether the sim time >= next command time
        # if so, send command to rover
        # if other actions do those too (sending noise)
        # dequeue
        pass