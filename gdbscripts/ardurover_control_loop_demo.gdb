set pagination off
set confirm off
set print thread-events off
set style enabled off
set breakpoint pending on
set breakpoint auto-hw on

# ArduRover 4.6.2 CubeBlack control-loop progress demo.
#
# Run QEMU/FastDyn with:
#   fastdyn probe-run -c configs/rover462.toml -o fastdyn_recent_run --rtos-introspect --rtos-introspect-mode events --rtos-introspection-max-events 16384 --run-gdb
#
# Then run this script from the FastDyn repository root:
#   script -q -f fastdyn_work/gdb_control_loop_demo.log -c 'gdb-multiarch -q -x gdbscripts/ardurover_control_loop_demo.gdb'
#
# This intentionally stays low-perturbation: it proves the firmware reaches the
# startup path, IMU bring-up/calibration, and the ArduPilot scheduler/control
# loop. It does not install high-frequency RTOS, FIFO, DMA, or sensor callback
# probes.

file virtuals/physics/flight_controllers/courbet/bin/ardurover_v462

python
import os
import gdb

def _add_directory(path, label):
    if not path:
        return
    if "\n" in path or "\r" in path:
        gdb.write("[input] %s ignored: value contains a newline\n" % label)
        return
    path = os.path.abspath(os.path.expanduser(path.strip()))
    if not os.path.isdir(path):
        gdb.write("[input] %s unavailable: %s\n" % (label, path))
        return
    escaped = path.replace("\\", "\\\\").replace(" ", "\\ ")
    gdb.execute("directory %s" % escaped, to_string=True)
    gdb.write("[input] %s: %s\n" % (label, path))

source_root = os.environ.get("ARDUPILOT_SOURCE_ROOT")
build_root = os.environ.get("ARDUPILOT_BUILD_ROOT")

_add_directory(source_root, "ArduPilot source")
_add_directory(build_root, "ArduPilot build root")
if build_root:
    _add_directory(os.path.join(build_root, "build", "CubeBlack"), "CubeBlack build")
end

target remote localhost:1234

printf "\n[demo] FastDyn ArduRover control-loop progress probe loaded\n"
printf "[demo] target: ArduRover v4.6.2 CubeBlack on STM32F427\n"
printf "[demo] evidence path: setup -> sensor ID -> IMU startup -> scheduler loop\n"
printf "[demo] GDB target: localhost:1234\n"

python
import gdb

TOTAL_MILESTONES = 15
hits = set()

WHOAMI_NAMES = {
    0x68: "MPU6000",
    0x70: "MPU6500",
    0x71: "MPU9250",
    0x73: "MPU9255",
    0xAE: "ICM20608D",
    0xAF: "ICM20608G",
    0x12: "ICM20602",
    0xAC: "ICM20601",
    0x03: "ICM20789",
    0x02: "ICM20789_R1",
    0x98: "ICM20689",
}

def _reg(name):
    try:
        return int(gdb.parse_and_eval("$" + name))
    except Exception:
        return 0

def _hex_reg(name):
    return "0x%08x" % (_reg(name) & 0xFFFFFFFF)

def _whoami_suffix():
    whoami = _reg("r0") & 0xFF
    sensor = WHOAMI_NAMES.get(whoami, "unknown")
    status = "accepted" if sensor != "unknown" else "unexpected"
    return "WHOAMI=0x%02x -> %s %s" % (whoami, sensor, status)

def _gyro_suffix():
    return "rate=%uHz id=0x%08x" % (_reg("r2") & 0xFFFF, _reg("r3") & 0xFFFFFFFF)

def _accel_suffix():
    return "rate=%uHz id=0x%08x" % (_reg("r2") & 0xFFFF, _reg("r3") & 0xFFFFFFFF)

class Milestone(gdb.Breakpoint):
    def __init__(self, index, addr, text, suffix=None, final=False):
        super(Milestone, self).__init__(
            "*0x%08x" % addr,
            type=gdb.BP_BREAKPOINT,
            internal=True,
            temporary=False,
        )
        self.index = index
        self.text = text
        self.suffix = suffix
        self.final = final

    def stop(self):
        if self.index in hits:
            self.enabled = False
            return False
        hits.add(self.index)
        if not self.final:
            self.enabled = False
        detail = ""
        if self.suffix is not None:
            detail = " | " + self.suffix()
        gdb.write("\n[%02d/%02d] %s%s | pc=%s lr=%s\n" % (
            self.index,
            TOTAL_MILESTONES,
            self.text,
            detail,
            _hex_reg("pc"),
            _hex_reg("lr"),
        ))
        if self.final:
            gdb.write("\n[summary] milestones=%d/%d; scheduler/control loop reached\n" % (
                len(hits),
                TOTAL_MILESTONES,
            ))
            gdb.write("[result] FastDyn rehosted ArduRover far enough to enter AP_Scheduler::loop\n")
            gdb.write("[next] Further closed-loop progress needs physics, sensors, RC/MAVLink, and Gazebo/RuMoCA feeds\n")
            gdb.write("\n[backtrace] top runtime frames\n")
            gdb.execute("bt 8")
            return True
        return False

Milestone(1,  0x0807d86c, "vehicle setup started: AP_Vehicle::setup")
Milestone(2,  0x0807bbe8, "scheduler task table initialized: AP_Scheduler::init")
Milestone(3,  0x08043378, "inertial backend discovery started: AP_InertialSensor::detect_backends")
Milestone(4,  0x0811addc, "Invensense SPI probe started")
Milestone(5,  0x0811a94c, "Invensense hardware init started")
Milestone(6,  0x0811a8aa, "sensor identity read", _whoami_suffix)
Milestone(7,  0x08041434, "gyro backend registration requested", _gyro_suffix)
Milestone(8,  0x0804150c, "accelerometer backend registration requested", _accel_suffix)
Milestone(9,  0x0811a878, "IMU periodic callback registered")
Milestone(10, 0x0811a4e6, "Invensense backend start returned")
Milestone(11, 0x080436d2, "inertial backend start completed")
Milestone(12, 0x08042a5c, "gyro initialization and calibration reached")
Milestone(13, 0x08041d08, "inertial sample wait reached")
Milestone(14, 0x0807d574, "vehicle main loop reached: AP_Vehicle::loop")
Milestone(15, 0x0807c278, "CONTROL LOOP REACHED: AP_Scheduler::loop", final=True)
end

printf "[demo] progress probes installed; continuing target\n"
continue
