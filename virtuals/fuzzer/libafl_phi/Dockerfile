FROM ubuntu:24.04

# Rando dependencies
RUN apt-get update && apt-get install -y \
    curl \
    build-essential \ 
    git \
    lsb-release \
    python3 \
    python3-venv \ 
    python3-pip \
    libstdc++6 \
    libprotobuf-dev \
    protobuf-compiler \
    libfdt1 \ 
    libdw1

# Rustup
RUN curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | sh -s -- -y
ENV PATH="/root/.cargo/bin:${PATH}"

# Gazebo
RUN curl https://packages.osrfoundation.org/gazebo.gpg --output /usr/share/keyrings/pkgs-osrf-archive-keyring.gpg && \
    echo "deb [arch=$(dpkg --print-architecture) signed-by=/usr/share/keyrings/pkgs-osrf-archive-keyring.gpg] http://packages.osrfoundation.org/gazebo/ubuntu-stable $(lsb_release -cs) main" | tee /etc/apt/sources.list.d/gazebo-stable.list > /dev/null && \
    apt-get update && \
    apt-get install -y gz-harmonic

# Populate the root directory with supporting repos
# TODO: In the future we will clone repos instead of copying them
WORKDIR /root
COPY qemu qemu/
COPY banquo banquo/
COPY LibAFL-0.15.4 LibAFL-0.15.4/
COPY ardupilot_gazebo ardupilot_gazebo/
COPY SITL_Models/Gazebo SITL_Models/Gazebo/

# The Dyn™️
WORKDIR /root/FastDyn
COPY FastDyn/virtuals virtuals/ 
COPY FastDyn/build build/

# Setup python dependencies
WORKDIR /root/FastDyn/virtuals/fuzzer/libafl_phi
RUN python3 -m venv /opt/venv
ENV PATH="/opt/venv/bin:$PATH"
RUN pip install --upgrade pip && pip install -r requirements.txt

ENV LD_LIBRARY_PATH="/root/FastDyn/virtuals/physics/physics_engines/gazebo/build/lib"
ENV GZ_VERSION="harmonic"
ENV GZ_SIM_SYSTEM_PLUGIN_PATH="/root/ardupilot_gazebo/build"
ENV GZ_SIM_RESOURCE_PATH="/root/ardupilot_gazebo/models:/root/ardupilot_gazebo/worlds"
ENV GZ_SIM_RESOURCE_PATH="/root/SITL_Models/Gazebo/models:/root/SITL_Models/Gazebo/worlds:${GZ_SIM_RESOURCE_PATH}"

RUN cargo clean && cargo build
CMD ["bash"]