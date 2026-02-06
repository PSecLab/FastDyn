# OPTIFUZZ: Finding Cyber-Physical Bugs with Physics Accurate Rehosting of Unmanned Vehicle Firmware

Hello and welcome to the OPTIFUZZ repository! This repository contains the code and resources for the OPTIFUZZ framework, which is designed to identify and analyze cyber-physical bugs in unmanned vehicle firmware through physics-accurate rehosting techniques.

## Table of Contents
- [Introduction](#introduction)
- [Building the Project](#building-the-project)
- [Running OPTIFUZZ](#running-optifuzz)
- [Courbet](#courbet)

## Introduction

## Building the Project

OPTIFUZZ has several dependencies and requires a specific setup to build and run correctly.

```
$PWD/
 |-- optifuzz_ws/
    |-- qemu/
    |-- courbet/
    |-- SITL_Models/
    |-- ArduPilot/
```

### Our QEMU Fork

First, get our qemu fork:

```bash
cd qemu
git checkout fastdyn
mkdir build
cd build
../configure --enable-debug
make qemu-system-arm
```

#### Setting up the memory backend

Create a directory for the memory backend:

```bash
mkdir -p qemu/ws/memory
```

Now create a file named `my_m4_ram3` in the `qemu/ws/memory` directory.

```bash
touch qemu/ws/memory/my_m4_ram3
```

### Setting up Courbet

Pull down the courbet repository linked in the open science section.

```bash
cd courbet
```

Ensure the Makefile looks like this:

```Makefile
qemu_path    ?= ../qemu
libhw_path   ?= ../libhw
LIBGZ        ?= true
LIBHW        ?= false
LIBFUZZ		 ?= false
DEV          ?= false
DEBUG_PRINT  ?= false
LIBPY        ?= false
```

**Note**: you may need our [LibHW](https://anonymous.4open.science/r/libhw-9FB5/README.md) repository if you want to build with `LIBHW` enabled.

Then build the courbet gazebo plugins and library:

```bash
cd courbet/courbet/gazebo
mkdir build
cd build
cmake ..
make
```

Ensure you go to the `libgz_wrapper.so` and add it to your `LD_LIBRARY_PATH`.

Now go back to the courbet directory and build courbet itself:

```bash
cd ../..
make
```

### Setting up SITL Models

First, you will need to install Gazebo Harmonic.

```bash
curl https://packages.osrfoundation.org/gazebo.gpg --output /usr/share keyrings/pkgs-osrf-archive-keyring.gpg

echo "deb [arch=$(dpkg --print-architecture) signed-by=/usr/share/keyrings/pkgs-osrf-archive-keyring.gpg] http://packages.osrfoundation.org/gazebo/ubuntu-stable $(lsb_release -cs) main" | sudo tee /etc/apt/sources.list.d/gazebo-stable.list > /dev/null

sudo apt-get update
sudo apt-get install gz-harmonic
```

All of the gazebo models are in the `SITL_Models` directory. You will also need to install the [ArduPilot Gazebo Plugins](https://github.com/ArduPilot/ardupilot_gazebo) and the [waves simulator](https://github.com/srmainwaring/asv_wave_sim/tree/master) if you plan on testing the boat or sub  if you have not already.

### Setting up ArduPilot
First, clone the ArduPilot repository:

```bash
git clone https://github.com/ArduPilot/ardupilot.git
```

Then follow the instructions to set up SITL on Linux found here:

https://ardupilot.org/dev/docs/setting-up-sitl-on-linux.html

All you need is to be able to run mavproxy with the map and console for our COURBET setup.


## Running Courbet independently of OPTIFUZZ

Start up Gazebo with the desired world file, for example:

```bash
cd courbet/courbet/gazebo
./run_and_attach_services.sh rover
```

Run MAVProxy with the appropriate parameters:

```bash
cd courbet/courbet/mavproxy
./run_mavproxy.sh
```

Then finally run your desired COURBET QEMU script that can be found in the `courbet/courbet/scripts` directory.

```bash
bash ../roverv462.sh
```


## Running OPTIFUZZ

To run OPTIFUZZ, assuming youhave completed all of the steps above, you will need to build the Rust fuzzer located in the `fuzzer/libafl_phi` directory of the COURBET repository.

Install Rust if you haven't already and then build the fuzzer:

```bash
cd courbet/courbet/fuzzer/libafl_phi
cargo build --release
```

Now, you can run the fuzzer with the following command:

```bash
./target/release/baby_fuzzer
```

If you want to specify different parameters that you want to fuzz, you can modify the file `courbet/fuzzer/libafl_phi/param_shim_descriptions.txt` with human readable descriptions of the parameters you want to fuzz like:

```
Max Steering Rate
Max Steering Acceleration
Max Lateral Turn Force (g)
Target Cruise Speed
Waypoint Speed
Nominal Throttle
Max Forward Acceleration
Minimum Turn Radius
```

This will return an output like this:

```
{ATC_STR_RAT_MAX, continuous, (0, 1000)}
{ATC_ACCEL_MAX, continuous, (0.0, 10.0)}
{ATC_TURN_MAX_G, continuous, (0.1, 10)}
{CRUISE_SPEED, continuous, (0, 100)}
{WP_SPEED, continuous, (0, 100)}
{CRUISE_THROTTLE, continuous, (0, 100)}
{ATC_DECEL_MAX, continuous, (0.0, 10.0)}
{TURN_RADIUS, continuous, (0, 10)}
```

Note that you must provide your own API key to use this feature and as a result it is not connected by default. If you would like to enable this feature simply export your API key as an environment variable before running the fuzzer:

```bash
export OPENAI_API_KEY="your_api_key"
```

and change the following line in `courbet/fuzzer/libafl_phi/src/main.rs`:

```rust
let input_library = CPExpInput::new(param_info_vec, env_info_vec);
```
to

```rust
let input_library = CPExpInput::new(generated_param_input, env_info_vec);
```