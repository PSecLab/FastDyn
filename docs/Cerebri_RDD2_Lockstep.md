# Cerebri RDD2 FastDyn lockstep

RDD2 lockstep does not use Zenoh, Ethernet, or the CSyn/ZROS bridge to pace
the controller. The Rust mission runner maps FastDyn's file-backed QEMU RAM,
resolves `rdd2_fastdyn_lockstep_shared` and `_image_ram_start` from the firmware
ELF, and exchanges generated `synapse_fbs` v0.6 payload structs through that
shared block. There are no fixed firmware addresses or handwritten message
decoders in the host bridge.

The default FastDyn mission uses a 20 ms plant macro-step. Every macro-step
still advances all 32 RDD2 controller ticks at 1,600 Hz. This reduces
cross-process synchronization from 4,000 to 1,000 round trips during the
20-second flight. CI requires the mission-loop speed to be at least 10x
realtime and separately reports full launch-to-exit speed, which includes
firmware boot and FastDyn/QEMU startup.

Run the local smoke test after building the firmware and Rust runner:

```sh
CEREBRI_RDD2_ROOT=../cerebri_rdd2 \
RDD2_WORKSPACE_ROOT=.. \
tests/integration/cerebri_rdd2_mission_smoke.sh
```

For maximum speed, the default CI image omits unused network services. Ethernet
remains available during lockstep through the communications profile: merge
`tests/integration/cerebri_rdd2_fastdyn_comms.conf` after the base FastDyn
config. That enables the ENET stack, CSyn/ZROS, and Zenoh as an asynchronous
side-channel; direct shared memory remains the only lockstep pacing path.

The smoke script likewise leaves TAP setup and Zenoh disabled by default. Set
`FASTDYN_RDD2_NETWORK_SETUP=true` only when running the communications profile.

```sh
extra_conf="$(realpath tests/integration/cerebri_rdd2_fastdyn.conf);$(realpath tests/integration/cerebri_rdd2_fastdyn_comms.conf)"
```
