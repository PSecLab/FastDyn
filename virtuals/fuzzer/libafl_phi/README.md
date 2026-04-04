# CP-Explore Setup

Welcome to CP-Explore, the joint fuzzing/requirements falsification tool used by FastDyn and SimHost to find vulnerabilities in cyber-physical systems! This guide will help you get started on your fuzzing campaigns.

## Building FastDyn
This guide assumes that FastDyn is already cloned and built. TODO: Add the steps here.

## Building the CP-Explore Docker Container
Navigate to the parent folder of your cloned FastDyn repository, and do:

```touch .dockerignore```

Use your favorite text editor to add the following to ```.dockerignore```:

```
FastDyn/*
!FastDyn/virtuals
!FastDyn/build

SITL_Models/*
!SITL_Models/Gazebo
```

Now, build the Docker container:

```docker build -f FastDyn/virtuals/fuzzer/libafl_phi/Dockerfile -t cp_exp .```

## Running a Campaign
When you are ready to run a fuzzing campaign, instantiate the Docker container:

```docker run -it --name YOUR_CONTAINER_NAME cp_exp```

This will open a shell inside of your new CP-Explore container. From here, you may configure the experiment settings before starting the campaign. Please see the following files:
* src/main.rs: General fuzzer and simulation settings
* src/phi_observer.rs: Defining STL formulas

When you're ready to run a campaign, do:

```cargo run --bin baby_fuzzer```

And watch CP-Explore work!

## Observing Campaign Results
To check on CP-Explore's progress during a campaign, open a new terminal window on your host machine and run:

```docker exec -it YOUR_CONTAINER_NAME bash```

From here, you may check the following directories for campaign data:
* ```crashes/```
* ```robustness_logs/```
* ```trace_logs/```