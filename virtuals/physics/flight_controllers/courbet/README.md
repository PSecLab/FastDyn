# Setting up Courbet

## Gazebo Harmonic Installation

Use the pre-prepared script located in `FastDyn/courbet/utils/install_gazebo_harmonic.sh` to install Gazebo Harmonic on your system.

## ArduPilot Gazebo

If not already done, follow the instructions at the [ArduPilot Gazebo repository](https://github.com/ArduPilot/ardupilot_gazebo) to set up ArduPilot Gazebo.

## Cloning the repository with submodules

To clone the Courbet repository along with its submodules, use the following command:

```bash
git clone --recurse-submodules https://github.com/PSecLab/FastDyn.git
```

This will install the submodules for:
- SITL_Models
- MavLink Headers

## QEMU installation and FastDyn build

Now, navigate to [this branch of QEMU](https://github.com/Arslan8/qemu.git) and clone this repository such that your project structure looks like this:

```
courbet_project/
├── FastDyn/
└── qemu/
```

After cloning, `cd` into the `qemu` directory and build QEMU:

```bash
cd qemu
git checkout fastdyn
mkdir build
cd build
../configure --enable-debug
make qemu-system-arm
```

### Setting up the memory backend

Create a directory for the memory backend:

```bash
mkdir -p qemu/ws/memory
```

Now create a file named `my_m4_ram3` in the `qemu/ws/memory` directory.

```bash
touch qemu/ws/memory/my_m4_ram3
```

### FastDyn plugin setup

Go into the makefile and make sure the only flag set to true is LIBGZ.

```
qemu_path    ?= ../qemu
libhw_path   ?= ../libhw
LIBGZ        ?= true
LIBHW        ?= false
DEV          ?= false
DEBUG_PRINT  ?= false
LIBPY        ?= false
```

Now, before building you need to go into the `courbet/` directory and make the `libgz_wrapper.so` and `services` which is how we communicate with Gazebo.

```bash
cd FastDyn/courbet/gazebo/
mkdir build
cd build
cmake ..
make
```

Now, finally you can build FastDyn:

```bash
// make sure you are in the root directory of FastDyn
make
```

## Using courbet

### Start Gazebo

In a terminal that support GUI applications, use the following command to start Gazebo with the Courbet world:

```bash
cd FastDyn/courbet/gazebo/
./run_and_attach_services.sh
```

### Start mavproxy

In a new terminal that supports GUI applications, start mavproxy GCS:

```bash
cd FastDyn/courbet/mavlink/
./run_mavproxy.sh
```

### Start QEMU with FastDyn

Then in another terminal, start QEMU with the FastDyn plugin:

```bash
cd qemu/build
bash ../fd_rover.sh
```

where `fd_rover.sh` is a script that contains the following:

```bash
./qemu-system-arm --plugin ../../FastDyn/build/libfastdyn.so,dev=classic:0x40000000-0x5FFFFFFF,virtual=../../FastDyn/courbet/unlabeled_conf/virtuals.txt,modifier=../../FastDyn/courbet/unlabeled_conf/modifiers.txt,symbols=../ws/rover_bin/ardurover \
    -d op,in_asm -D qemu.log -machine cortexm,memory-backend=ram0 \
    -monitor telnet:127.0.0.1:5555,server,nowait -S \
    -gdb tcp::1235 \
    -semihosting --semihosting-config enable=on,target=native \
    -qmp unix:/tmp/qmp-sock,server,nowait \
    -device loader,file=../ws/rover_bin/ardurover.bin,addr=0x08004000 \
    -serial stdio -nographic \
    -object memory-backend-file,id=ram0,mem-path=../ws/memory/my_m4_ram3,size=512M,share=on \
    -global cortexm-soc.ram_baseaddr=0x20000000 -cpu cortex-m4 \
    -global armv7m.init-nsvtor=0x08004000 \
```

### Attach GDB (optional)

If you want to debug using GDB, open another terminal and run:

```bash
gdb-multiarch
```

with a .gdbinit file containing:

```
target remote :1235
add-symbol-file rover_bin/ardurover
set substitute-path ../../ /root/rooney/ardupilot/
```

where `/root/rooney/ardupilot/` is the path to your ArduPilot source code and `rover_bin/ardurover` is the path to the binary file built for the rover.

## EXTRA: Adding logging capabilities

If you would like to view the flight logs in [**UAV Log Viewer**](https://plot.ardupilot.org/#/), you just need to create these directory within courbet:

```bash
mkdir -p FastDyn/courbet/flight_logs
cd FastDyn/courbet/flight_logs
mkdir @ROMFS
mkdir @SYS
mkdir APM
mkdir APM/LOGS
```

Then after running your simulation, you can find the logs in `FastDyn/courbet/flight_logs/APM/LOGS/00000001.bin`, download it and open it in UAV Log Viewer.
