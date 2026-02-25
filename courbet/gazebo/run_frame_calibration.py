"""
Expected magnetometer reading using:
  - Gazebo model pose topic (/model/<name>/pose)
  - Sensor fixed pose from model.sdf (<sensor><pose>)
  - Earth magnetic field from lookup tables (NOT from <magnetic_field>)

World SDF is used ONLY to read (lat, lon) from <spherical_coordinates>.
Sensor pose is read from the model SDF on disk.

Pipeline:
  1) /world/<world>/generate_world_sdf  -> world SDF XML string
  2) world SDF -> <spherical_coordinates> -> (lat, lon)
  3) earth_magnetic_field_from_latlon(lat, lon) -> B_W (ENU, Tesla)
  4) model.sdf (on disk) -> <sensor name="..."><pose> -> sensor RPY
  5) /model/<model>/pose -> quaternion -> R_WB
  6) expected sensor field:
        B_S = (R_WB * R_BS)^T * B_W
"""

from __future__ import annotations

import math
import time
import os
from types import SimpleNamespace
import xml.etree.ElementTree as ET
from typing import Tuple
import numpy as np
from typing import Optional, Callable
import threading
from udp_listener_driver_backend import DriverMagUDPReceiver
from orthogonal_procrustes import kabsch_fit, rotation_to_axis_mapping

from matplotlib import scale
from get_earth_mag import earth_magnetic_field_from_latlon
from dataclasses import dataclass

import numpy as np
from gz.transport13 import Node
from gz.msgs10.pose_v_pb2 import Pose_V
from gz.msgs10.stringmsg_pb2 import StringMsg
from gz.msgs10.sdf_generator_config_pb2 import SdfGeneratorConfig
from gz.msgs10.empty_pb2 import Empty
from gz.msgs10.magnetometer_pb2 import Magnetometer


# ---------------------------------------------------------------------
# Service: generate_world_sdf
# ---------------------------------------------------------------------
def call_generate_world_sdf_xml(node: Node, world_name: str, timeout_ms: int = 2000) -> str:
    """Call /world/<world>/generate_world_sdf -> XML string."""
    service_name = f"/world/{world_name}/generate_world_sdf"
    cfg = SdfGeneratorConfig()
    ok, resp = node.request(service_name, cfg, SdfGeneratorConfig, StringMsg, timeout_ms)
    if not ok:
        raise RuntimeError(f"Failed to call {service_name}")
    return resp.data


# ---------------------------------------------------------------------
# World SDF parsing (XML string): lat/lon, gravity
# ---------------------------------------------------------------------
def read_latlon_from_world_sdf_xml(sdf_xml: str) -> Tuple[float, float]:
    """
    Extract latitude_deg and longitude_deg from world SDF XML string.
    """
    root = ET.fromstring(sdf_xml)

    world = root.find(".//world")
    if world is None:
        raise ValueError("No <world> found in SDF XML")

    sph = world.find(".//spherical_coordinates")
    if sph is None:
        raise ValueError("No <spherical_coordinates> found under <world>")

    lat_text = sph.findtext("latitude_deg")
    lon_text = sph.findtext("longitude_deg")
    if lat_text is None or lon_text is None:
        raise ValueError("Missing <latitude_deg> or <longitude_deg>")

    return float(lat_text.strip()), float(lon_text.strip())


def read_gravity_from_world_sdf_xml(sdf_xml: str) -> Tuple[float, float]:
    """
    Extract gravity field from world SDF XML string.
    """
    root = ET.fromstring(sdf_xml)

    world = root.find(".//world")
    if world is None:
        raise ValueError("No <world> found in SDF XML")

    grav = world.find(".//gravity")
    if grav is None:
        raise ValueError("No <gravity> found under <world>")

    vals = [float(x) for x in grav.text.strip().split()]
    if len(vals) != 3:
        raise ValueError(f"<gravity> must have 3 values, got {len(vals)}")
    return vals[0], vals[1], vals[2]


# ---------------------------------------------------------------------
# Model SDF parsing (FILE): sensor pose only
# ---------------------------------------------------------------------
@dataclass
class SensorAttitude:
    roll: float = 0.0
    pitch: float = 0.0
    yaw: float = 0.0

@dataclass
class Quaternion:
    w: float = 1.0
    x: float = 0.0
    y: float = 0.0
    z: float = 0.0

def parse_sensor_pose_rpy_from_model_sdf_file(
    model_sdf_path: str,
    *,
    sensor_name: str,
) -> SensorAttitude:
    """
    Parse <sensor name="sensor_name"><pose> ... r p y </pose> from model.sdf file.

    This is used when the world SDF does not inline the full sensor definition.

    Args:
        model_sdf_path: Path to model.sdf (e.g. SITL_Models/Gazebo/models/r1_rover/model.sdf)
        sensor_name: sensor name attribute to locate (e.g. "magnetometer_sensor")

    Returns:
        (roll, pitch, yaw) in radians.
        If <pose> missing, returns (0,0,0).

    Notes:
        - This searches any <sensor> in the model file (under links).
        - If your SDF uses <pose relative_to="...">, this function ignores it.
    """
    abs_path = os.path.abspath(model_sdf_path)
    if not os.path.exists(abs_path):
        raise FileNotFoundError(f"model.sdf not found: {abs_path}")

    tree = ET.parse(abs_path)
    root = tree.getroot()

    model = root.find("model")
    if model is None:
        raise ValueError("No <model> found under <sdf>.")
    
    # model.sdf typically: <sdf><model>...<link>...<sensor>...</sensor>...
    link = model.find("link")
    if link is None:
        raise ValueError("No <link> found under <model>.")

    sensor_el = None
    for s in link.findall("sensor"):
        if s.attrib.get("name") == sensor_name:
            sensor_el = s
            break

    if sensor_el is None:
        raise ValueError(f"Sensor '{sensor_name}' not found in {abs_path}")

    pose_el = sensor_el.find("pose")
    if pose_el is None or pose_el.text is None:
        return SensorAttitude(roll=0.0, pitch=0.0, yaw=0.0)

    vals = [float(v) for v in pose_el.text.split()]
    if len(vals) != 6:
        raise ValueError(f"Sensor <pose> must have 6 values, got {len(vals)}")
    
    return SensorAttitude(roll=vals[3], pitch=vals[4], yaw=vals[5])


# ---------------------------------------------------------------------
# Gazebo transport: model quaternion and actual magnetic field
# ---------------------------------------------------------------------
def get_model_quat_wb_from_pose_topic(
    node: Node,
    *,
    topic: str,
    entity_name: str,
    timeout_s: float = 2.0,
) -> Quaternion:
    """Subscribe Pose_V and return (qw,qx,qy,qz) body->world quaternion."""
    latest = {"msg": None}

    def cb(msg: Pose_V):
        latest["msg"] = msg

    if not node.subscribe(Pose_V, topic, cb):
        raise RuntimeError(f"Failed to subscribe to {topic}")

    t0 = time.time()
    while time.time() - t0 < timeout_s:
        if latest["msg"] is not None and len(latest["msg"].pose) > 0:
            break
        time.sleep(0.01)

    msg = latest["msg"]
    if msg is None or len(msg.pose) == 0:
        raise TimeoutError(f"No Pose_V received on {topic}")

    chosen = None
    for p in msg.pose:
        if getattr(p, "name", "") == entity_name:
            chosen = p
            break
    if chosen is None:
        chosen = msg.pose[0]

    q = chosen.orientation
    return Quaternion(w=q.w, x=q.x, y=q.y, z=q.z)


def get_mag_from_topic(
    node: Node,
    *,
    topic: str,
    entity_name: str,
    timeout_s: float = 2.0,
    convert_to_tesla: bool = True,
) -> Magnetometer:
    """Subscribe Magnetometer and return the latest message."""
    latest = {"msg": None}

    def cb(msg: Magnetometer):
        latest["msg"] = msg

    if not node.subscribe(Magnetometer, topic, cb):
        raise RuntimeError(f"Failed to subscribe to {topic}")

    t0 = time.time()
    while time.time() - t0 < timeout_s:
        if latest["msg"] is not None:
            break
        time.sleep(0.01)

    msg = latest["msg"]
    if msg is None:
        raise TimeoutError(f"No field_tesla received on {topic}")

    B_sensor_meas = msg.field_tesla

    if convert_to_tesla:
        scale = 1e-4  # Gauss to Tesla
    else:
        scale = 1.0

    return scale*np.array([B_sensor_meas.x, B_sensor_meas.y, B_sensor_meas.z], dtype=float)


# ---------------------------------------------------------------------
# Rotation utilities
# ---------------------------------------------------------------------
def rpy_to_R_wb(roll: float, pitch: float, yaw: float) -> np.ndarray:
    """RPY(rad) -> R = Rz(yaw)*Ry(pitch)*Rx(roll)."""
    cr, sr = math.cos(roll), math.sin(roll)
    cp, sp = math.cos(pitch), math.sin(pitch)
    cy, sy = math.cos(yaw), math.sin(yaw)

    return np.array([
        [cy*cp, cy*sp*sr - sy*cr, cy*sp*cr + sy*sr],
        [sy*cp, sy*sp*sr + cy*cr, sy*sp*cr - cy*sr],
        [-sp,   cp*sr,            cp*cr],
    ], dtype=float)


def quat_to_R_wb(qw: float, qx: float, qy: float, qz: float) -> np.ndarray:
    """Quaternion -> R_WB (roataion matrix world -> body, coordinate transform body->world)."""
    n = math.sqrt(qw*qw + qx*qx + qy*qy + qz*qz)
    if n == 0:
        raise ValueError("Zero-norm quaternion")
    qw, qx, qy, qz = qw/n, qx/n, qy/n, qz/n

    return np.array([
        [1 - 2*(qy*qy + qz*qz),     2*(qx*qy - qw*qz),     2*(qx*qz + qw*qy)],
        [    2*(qx*qy + qw*qz), 1 - 2*(qx*qx + qz*qz),     2*(qy*qz - qw*qx)],
        [    2*(qx*qz - qw*qy),     2*(qy*qz + qw*qx), 1 - 2*(qx*qx + qy*qy)],
    ], dtype=float)


# ---------------------------------------------------------------------
# Debug Gazebo magnetometer sensor module issue
# ---------------------------------------------------------------------
def expected_measurement_vector_sensor_frame(
    Ref_enu: np.ndarray,
    body_quat: Quaternion,
    sensor_rpy: SensorAttitude,
) -> np.ndarray:
    """
    V_S = C_bs * C_wb * V_W = (R_wb * R_bs)^T * V_W (all in ENU world basis)
    V_S: expected sensor reading in sensor frame
    V_W: reference vector in world frame
    C_wb: world->body transformation from body pose quaternion
    C_bs: body->sensor transformation from sensor fixed RPY
    """
    C_wb = quat_to_R_wb(body_quat.w, body_quat.x, body_quat.y, body_quat.z).T
    C_bs = rpy_to_R_wb(sensor_rpy.roll, sensor_rpy.pitch, sensor_rpy.yaw).T
    C_ws = C_bs @ C_wb
    return C_ws @ Ref_enu


def repro_magnetometer_vector_sensor_frame(
    Ref_enu: np.ndarray,
    body_quat: Quaternion,
    sensor_rpy: SensorAttitude,
) -> np.ndarray:
    """
    Bug reproduction helper.
    
    Intentionally applies an incorrect ENU->NED conversion to the world magnetic field
    to reproduce the frame-mismatch bug (treating Gazebo's NED magnetic field as if it
    were expressed in ENU).    
    """
    C_wb = quat_to_R_wb(body_quat.w, body_quat.x, body_quat.y, body_quat.z).T
    C_bs = rpy_to_R_wb(sensor_rpy.roll, sensor_rpy.pitch, sensor_rpy.yaw).T
    C_ws = C_bs @ C_wb

    return C_ws @ np.array([[0, 1, 0],[1, 0, 0],[0, 0, -1]]) @ Ref_enu


def debug_magnetometer_vector_sensor_frame(
    Ref_sensor: np.ndarray,
    body_quat: Quaternion,
    sensor_rpy: SensorAttitude,
) -> np.ndarray:
    """
    Debug function.

    Applies the NED->ENU conversion to the world magnetic field to show that it matches
    Rotation matrix from world to sensor frame from model pose quaternion and sensor fixed RPY 
    """
    C_wb = quat_to_R_wb(body_quat.w, body_quat.x, body_quat.y, body_quat.z).T
    C_bs = rpy_to_R_wb(sensor_rpy.roll, sensor_rpy.pitch, sensor_rpy.yaw).T
    C_ws = C_bs @ C_wb

    return C_ws @ np.array([[0, 1, 0],[1, 0, 0],[0, 0, -1]]) @ (C_ws.T @ Ref_sensor) 


# ---------------------------------------------------------------------
# Frame calibration
# ---------------------------------------------------------------------
class GazeboMagTopicReceiver:
    def __init__(self, node: Node, topic: str, *, convert_to_tesla: bool = True):
        self._node = node
        self._topic = topic
        self._convert_to_tesla = convert_to_tesla
        self._lock = threading.Lock()
        self._latest = np.zeros(3, dtype=float)
        self._latest_time = 0.0
        self._sub_ok = False

    def start(self):
        if self._sub_ok:
            return

        def cb(msg: Magnetometer):
            B = msg.field_tesla     # NOTE: Gazebo msg field name is field_tesla but content may be Gauss depending on plugin/version
            v = np.array([B.x, B.y, B.z], dtype=float)
            if self._convert_to_tesla:
                v = 1e-4 * v        # Gauss -> Tesla
            with self._lock:
                self._latest = v
                self._latest_time = time.time()

        if not self._node.subscribe(Magnetometer, self._topic, cb):
            raise RuntimeError(f"Failed to subscribe to {self._topic}")
        self._sub_ok = True

    def latest(self) -> tuple[np.ndarray, float]:
        with self._lock:
            return self._latest.copy(), float(self._latest_time)


class GazeboPoseTopicReceiver:
    """
    Subscribe to a Gazebo Pose_V topic and provide the latest body->world quaternion.

    Typical pose topics:
      - "/model/<model_name>/pose"  (Pose_V)
      - "/world/<world>/dynamic_pose/info" (Pose_V, may include many entities)

    This receiver tries to pick the pose for `entity_name` if present.
    """

    def __init__(
        self,
        node: Node,
        topic: str,
        *,
        entity_name: str,
    ):
        self._node = node
        self._topic = topic
        self._entity_name = entity_name

        self._lock = threading.Lock()
        self._running = False
        self._subscribed = False

        self._latest_quat: Optional[Quaternion] = None
        self._latest_t: float = 0.0

    def start(self) -> None:
        if self._running:
            return
        self._running = True

        ok = self._node.subscribe(Pose_V, self._topic, self._cb)
        if not ok:
            self._running = False
            raise RuntimeError(f"Failed to subscribe Pose_V to topic: {self._topic}")
        self._subscribed = True

    def stop(self) -> None:
        # gz.transport python API doesn't always expose unsubscribe; we just stop updating.
        self._running = False

    def latest_quat_wb(self) -> Tuple[Quaternion, float]:
        """
        Returns (Quaternion(w,x,y,z), timestamp_seconds).
        timestamp_seconds is wall-clock time if msg stamp is not available.
        """
        with self._lock:
            if self._latest_quat is None:
                # Return a safe default; caller should check t>0
                return Quaternion(1.0, 0.0, 0.0, 0.0), 0.0
            return self._latest_quat, self._latest_t

    # ---------- internal helpers ----------

    @staticmethod
    def _extract_stamp_seconds(msg: Pose_V) -> Optional[float]:
        """
        Try to read header.stamp.{sec,nsec} if present.
        Different gz versions may differ; fall back if missing.
        """
        try:
            # Pose_V usually has header.stamp in recent gz-msgs
            hdr = msg.header
            st = hdr.stamp
            sec = getattr(st, "sec", None)
            nsec = getattr(st, "nsec", None)
            if sec is None or nsec is None:
                return None
            return float(sec) + float(nsec) * 1e-9
        except Exception:
            return None

    def _select_pose_index(self, msg: Pose_V) -> int:
        """
        Choose which pose entry in Pose_V to use.
        Prefer matching by name if available; else use pose(0).
        """
        # Many Pose messages in gz have fields: name, id
        # Pose_V contains repeated Pose messages.
        if msg.pose is None or len(msg.pose) == 0:
            return -1

        # Try match by name
        for i, p in enumerate(msg.pose):
            try:
                # Some variants: p.name, p.has_field('name'), etc.
                if hasattr(p, "name") and p.name == self._entity_name:
                    return i
            except Exception:
                pass

        # If topic is "/model/<name>/pose", it's usually pose(0)
        return 0

    def _cb(self, msg: Pose_V) -> None:
        if not self._running:
            return

        idx = self._select_pose_index(msg)
        if idx < 0:
            return

        p = msg.pose[idx]
        qmsg = p.orientation

        # Extract quaternion body->world (assuming Gazebo pose orientation is in world frame for the entity)
        qw = float(qmsg.w)
        qx = float(qmsg.x)
        qy = float(qmsg.y)
        qz = float(qmsg.z)

        t = time.time()

        with self._lock:
            self._latest_quat = Quaternion(qw, qx, qy, qz)
            self._latest_t = float(t)


def frame_calibration_auto(
    *,
    gazebo_rx: "GazeboMagTopicReceiver",
    driver_rx: "DriverMagUDPReceiver",
    pose_rx: "GazeboPoseTopicReceiver",          # NEW: body quaternion source
    sensor_rpy: "SensorAttitude",               # NEW: fixed sensor rpy
    n_samples: int = 30,
    sample_period_s: float = 0.05,
    max_pair_dt_s: float = 0.10,
    driver_to_gazebo_scale: float = 1e-7,  # mG -> Tesla : (mG -> G = 1e-3) and (G -> T = 1e-4) => 1e-7
    brute_kabsch: bool = True,
    min_samples: int = 3,
) -> dict:
    """
    Collect paired samples automatically and solve x ≈ s R y + b.

    x: Gazebo topic vector (Tesla recommended)
    y: Driver UDP vector (milliGauss) scaled to same unit as x

    Pairing rule:
      - accept pair only if both streams have recent timestamps and |t_x - t_y| <= max_pair_dt_s
    """
    xs: list[np.ndarray] = []
    ys: list[np.ndarray] = []

    t_start = time.time()
    last_accept_t = 0.0

    while len(xs) < n_samples:
        x, tx = gazebo_rx.latest()
        y_mg, ty = driver_rx.latest_milli_gauss()

        body_quat, tq = pose_rx.latest_quat_wb()

        now = time.time()
        # require both streams have at least 1 update
        if tx <= 0.0 or ty <= 0.0 or tq <= 0.0:
            time.sleep(sample_period_s)
            continue

        dt_xy = abs(tx - ty)
        dt_xq = abs(tx - tq)

        if dt_xy <= max_pair_dt_s and dt_xq <= max_pair_dt_s:
            if now - last_accept_t >= sample_period_s:
                y = driver_to_gazebo_scale * y_mg

                # --- KEY CHANGE: use debug-corrected x ---
                x_debug = debug_magnetometer_vector_sensor_frame(
                    np.asarray(x, dtype=float).reshape(3),
                    body_quat,
                    sensor_rpy,
                )

                xs.append(np.asarray(x_debug, dtype=float).reshape(3))
                ys.append(np.asarray(y, dtype=float).reshape(3))
                last_accept_t = now

                if len(xs) % 5 == 0 or len(xs) == 1:
                    print(
                        f"[calib] paired {len(xs)}/{n_samples}  "
                        f"dt_xy={dt_xy:.3f}s dt_xq={dt_xq:.3f}s  "
                        f"x_raw={x}  x_dbg={x_debug}  y(mG)={y_mg}"
                    )

        time.sleep(sample_period_s)

        if now - t_start > 30.0 and len(xs) < min_samples:
            raise TimeoutError("Not enough paired samples collected (check topics/UDP and timing).")

    X = np.vstack(xs)
    Y = np.vstack(ys)

    out = {"N": len(xs)}

    # solve orthogonal Procrustes using Kabsch-Umeyama method
    best = kabsch_fit(X, Y, estimate_scale=False, estimate_bias=False, brute_force=brute_kabsch)
    
    return best


# ---------------------------------------------------------------------
# MAIN
# ---------------------------------------------------------------------
def main():
    """
    Compute expected sensor measurement vector:
      - lat/lon from world SDF (service)
      - sensor pose from model.sdf on disk
      - sensor value using:
        - gravity direction from sdf
        - earth magnetic field from lookup tables
      - model quaternion from /model/<name>/pose
    """
    WORLD_NAME = "runway"
    # MODEL_NAME = "r1_rover"
    MODEL_NAME = "skywalker_x8"
    SENSOR_NAME = "magnetometer_sensor"
    POSE_TOPIC = f"/model/{MODEL_NAME}/pose"
    SENSOR_TOPIC = f"/world/runway/model/{MODEL_NAME}/link/base_link/sensor/{SENSOR_NAME}/{SENSOR_NAME.removesuffix("_sensor")}"
    DEBUG_MODE = True
    CALIBRATION_MODE = False

    # model.sdf path
    MODEL_SDF_PATH = "submodules/SITL_Models/Gazebo/models/skywalker_x8/model.sdf"

    # lookup-table 옵션
    PATCH_ROUND_ERROR = False

    node = Node()

    if SENSOR_NAME == "magnetometer_sensor":

        if DEBUG_MODE:
            # 1) world SDF XML from service
            sdf_xml = call_generate_world_sdf_xml(node, WORLD_NAME)

            # 2) lat/lon from world SDF
            lat_deg, lon_deg = read_latlon_from_world_sdf_xml(sdf_xml)

            # 3) obtain reference field vector(s)
            # Earth field from lookup tables (ENU, Tesla)
            res = earth_magnetic_field_from_latlon(
                lat_deg,
                lon_deg,
                use_units_gauss=False,      # Tesla
                use_earth_frame_ned=False,  # ENU
                patch_round_error=PATCH_ROUND_ERROR,
            )
            B_world = np.array([res.Bx, res.By, res.Bz], dtype=float)

            # 4) sensor pose from model.sdf (FILE)
            sensor_rpy = parse_sensor_pose_rpy_from_model_sdf_file(
                MODEL_SDF_PATH,
                sensor_name=SENSOR_NAME,
            )

            # 5) model quaternion from pose topic
            body_quat = get_model_quat_wb_from_pose_topic(
                node,
                topic=POSE_TOPIC,
                entity_name=MODEL_NAME,
                timeout_s=2.0,
            )
        
            # debugging Gazebo magnetic sensor model issue
            # 6-1) expected sensor field
            B_sensor_exp = expected_measurement_vector_sensor_frame(
                B_world, body_quat, sensor_rpy
            )

            # 6-2) measured sensor field from service
            B_sensor_meas = get_mag_from_topic(
                node,
                topic=SENSOR_TOPIC,
                entity_name=MODEL_NAME,
                timeout_s=2.5,
                convert_to_tesla=True,
            )

            # 6-3) error reproduction
            B_sensor_repro = repro_magnetometer_vector_sensor_frame(
                B_world, body_quat, sensor_rpy
            )

            # 6-4) debug
            B_sensor_debug = debug_magnetometer_vector_sensor_frame(
                B_sensor_meas, body_quat, sensor_rpy
            )

        if CALIBRATION_MODE:
            # 1) find out driver level conversion
            sensor_rpy = parse_sensor_pose_rpy_from_model_sdf_file(
                MODEL_SDF_PATH,
                sensor_name=SENSOR_NAME,
            )

            driver_rx = DriverMagUDPReceiver(bind_ip="127.0.0.1", port=15150)
            driver_rx.start()

            gazebo_rx = GazeboMagTopicReceiver(node, SENSOR_TOPIC, convert_to_tesla=True)
            gazebo_rx.start()

            pose_rx = GazeboPoseTopicReceiver(node, POSE_TOPIC, entity_name=MODEL_NAME)
            pose_rx.start()

            try:
                calib = frame_calibration_auto(
                    gazebo_rx=gazebo_rx,
                    driver_rx=driver_rx,
                    pose_rx=pose_rx,
                    sensor_rpy=sensor_rpy,
                    n_samples=40,
                    sample_period_s=0.05,
                    max_pair_dt_s=0.10,
                    driver_to_gazebo_scale=1e-7,  # mG -> Tesla
                    brute_kabsch=True,
                )

            finally:
                driver_rx.stop()

    elif SENSOR_NAME == "imu_sensor":
        # IMU accelerometer/gyroscope sensor  fill here
        res = read_gravity_from_world_sdf_xml(sdf_xml)


    # result print
    if SENSOR_NAME == "magnetometer_sensor":
        if DEBUG_MODE:
            np.set_printoptions(precision=9, suppress=False)
            print("\n=== Expected Magnetometer (Earth field from lookup tables) ===")
            print(f"lat, lon (deg): {lat_deg:.8f}, {lon_deg:.8f}")
            print(f"decl (deg): {res.declination_deg:.3f}, incl (deg): {res.inclination_deg:.3f}")
            print(f"|B_W| (Tesla): {math.sqrt(B_world @ B_world):.9e}")
            print("B_W (ENU, Tesla):", B_world)

            print("\nSensor RPY (rad) from model.sdf relative to parent link:")
            print(f"  roll={sensor_rpy.roll:.6f}, pitch={sensor_rpy.pitch:.6f}, yaw={sensor_rpy.yaw:.6f}")
            print(f"  model.sdf: {os.path.abspath(MODEL_SDF_PATH)}")

            print("\nModel quaternion (body->world) from pose topic:")
            print(f"  qw={body_quat.w:.6f}, qx={body_quat.x:.6f}, qy={body_quat.y:.6f}, qz={body_quat.z:.6f}")

            print("\nExpected B in SENSOR frame (Tesla, FLU):")
            print("  B_S:", B_sensor_exp)
            print(f"  |B_S| (Tesla): {math.sqrt(B_sensor_exp @ B_sensor_exp):.9e}")

            print("\nMeasured B in SENSOR frame (after scaling if needed):")
            print("  B_S:", B_sensor_meas)
            print(f"  |B_S| (Tesla): {math.sqrt(B_sensor_meas @ B_sensor_meas):.9e}")

            print("\nReproduced B in SENSOR frame (after adding ENU->NED conversion in world magnetic field):")
            print("  B_S:", B_sensor_repro)
            print(f"  |B_S| (Tesla): {math.sqrt(B_sensor_repro @ B_sensor_repro):.9e}")

            print("\nDebug B in SENSOR frame (using rover pose and sensor pose):")
            print("  B_S:", B_sensor_debug)
            print(f"  |B_S| (Tesla): {math.sqrt(B_sensor_debug @ B_sensor_debug):.9e}")

            print("\nNotes:")
            print("- World <magnetic_field> is NOT used.")
            print("- Only world lat/lon is used; sensor pose comes from model.sdf.")
            print("- If measured magnetometer is in Gauss, multiply Tesla by 1e4.\n")
            print("- Debug Note: Gazebo percieves magneic field represented in NED as ENU.")

        if CALIBRATION_MODE:
            print("\n=== Kabsch (continuous rotation) algorithm result ===")
            print("det(R):", float(calib.detR))
            print("scale s:", float(calib.s))
            print("bias b:", np.asarray(calib.b, dtype=float))
            print("rmse:", float(calib.rmse))
            print("R axis-ish mapping:", rotation_to_axis_mapping(calib.R))
            print("Reverse mapping for bridge:", rotation_to_axis_mapping(calib.R.T))


if __name__ == "__main__":
    main()
