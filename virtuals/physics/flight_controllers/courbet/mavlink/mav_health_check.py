#!/usr/bin/env python3
"""Wait for an ArduPilot heartbeat and a short stream of telemetry."""

from __future__ import annotations

import argparse
from contextlib import contextmanager
import time

from pymavlink import mavutil

try:
    from fastdyn import timing
except Exception:  # pragma: no cover
    class _NoTiming:
        @staticmethod
        @contextmanager
        def phase(*_args, **_kwargs):
            yield

        @staticmethod
        def mark(*_args, **_kwargs):
            return None

    timing = _NoTiming()


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--connect", default="udpin:127.0.0.1:14552")
    parser.add_argument("--heartbeat-timeout", type=float, default=120.0)
    parser.add_argument("--monitor-sec", type=float, default=10.0)
    parser.add_argument("--min-position-messages", type=int, default=1)
    parser.add_argument("--min-gps-messages", type=int, default=1)
    return parser.parse_args()


def connect(endpoint: str, timeout: float) -> mavutil.mavfile:
    print(f"[health] connecting to {endpoint}", flush=True)
    mav = mavutil.mavlink_connection(endpoint)
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        msg = mav.recv_match(type="HEARTBEAT", blocking=True, timeout=1.0)
        if msg is None:
            continue
        if int(getattr(msg, "type", -1)) == mavutil.mavlink.MAV_TYPE_GCS:
            continue
        if int(msg.get_srcSystem()) == 0:
            continue
        mav.target_system = int(msg.get_srcSystem())
        mav.target_component = int(msg.get_srcComponent())
        print(
            f"[health] heartbeat from system={mav.target_system} component={mav.target_component}",
            flush=True,
        )
        timing.mark(
            "health.heartbeat",
            echo=True,
            system=mav.target_system,
            component=mav.target_component,
        )
        mav.mav.heartbeat_send(
            mavutil.mavlink.MAV_TYPE_GCS,
            mavutil.mavlink.MAV_AUTOPILOT_INVALID,
            0,
            0,
            mavutil.mavlink.MAV_STATE_ACTIVE,
        )
        for stream_id in (
            mavutil.mavlink.MAV_DATA_STREAM_POSITION,
            mavutil.mavlink.MAV_DATA_STREAM_EXTENDED_STATUS,
            mavutil.mavlink.MAV_DATA_STREAM_EXTRA1,
        ):
            mav.mav.request_data_stream_send(
                mav.target_system,
                mav.target_component,
                stream_id,
                4,
                1,
            )
        return mav
    raise TimeoutError(f"timed out waiting for heartbeat on {endpoint}")


def monitor(mav: mavutil.mavfile, duration: float, min_position: int, min_gps: int) -> None:
    deadline = time.monotonic() + duration
    position_count = 0
    gps_count = 0
    last_position_print = 0.0
    last_mode: str | None = None

    print(f"[health] monitoring for {duration:g}s", flush=True)
    while time.monotonic() < deadline:
        msg = mav.recv_match(blocking=True, timeout=1.0)
        if msg is None:
            continue
        msg_type = msg.get_type()
        if msg_type == "STATUSTEXT":
            text = getattr(msg, "text", "").strip()
            if text:
                print(f"[health] STATUSTEXT: {text}", flush=True)
        elif msg_type == "HEARTBEAT" and int(msg.get_srcSystem()) == mav.target_system:
            mode = mavutil.mode_string_v10(msg)
            if mode != last_mode:
                print(f"[health] mode={mode}", flush=True)
                last_mode = mode
        elif msg_type == "GLOBAL_POSITION_INT":
            position_count += 1
            now = time.monotonic()
            if now - last_position_print >= 2.0:
                lat = msg.lat / 1.0e7
                lon = msg.lon / 1.0e7
                rel_alt = msg.relative_alt / 1000.0
                print(f"[health] position lat={lat:.7f} lon={lon:.7f} rel_alt={rel_alt:.1f}m", flush=True)
                last_position_print = now
        elif msg_type == "GPS_RAW_INT":
            if int(getattr(msg, "fix_type", 0)) >= 3:
                gps_count += 1

        if position_count >= min_position and gps_count >= min_gps:
            print(
                f"[health] telemetry observed: position={position_count} gps={gps_count}",
                flush=True,
            )
            timing.mark(
                "health.telemetry_observed",
                echo=True,
                position=position_count,
                gps=gps_count,
            )
            return

    raise TimeoutError(
        f"timed out waiting for telemetry: position={position_count}/{min_position} gps={gps_count}/{min_gps}"
    )


def main() -> int:
    args = parse_args()
    with timing.phase("health.total"):
        with timing.phase("health.connect"):
            mav = connect(args.connect, args.heartbeat_timeout)
        with timing.phase("health.monitor"):
            monitor(
                mav,
                args.monitor_sec,
                args.min_position_messages,
                args.min_gps_messages,
            )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
