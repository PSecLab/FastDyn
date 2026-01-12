import sys
from fastdyn.fastdyn import *


fastdyn = Fastdyn()

machine0 = fastdyn.create_machine(machine_name="machine0",
                                  platform="STM32F429"
                                  )

cpu0 = machine0.add_cpu(
    arch="arm",
    machine="cortexm",
    cpu="cortex-m4",
    binary="boardrunner/boardrunner_examples/examples/STM32F429i-disc1/gpio/firmwares/gpio.axf",
    init_nsvtor="0x08000000"
    )

machine0.add_cmsis_svd(cmsis_svd="/home/hammad/work/rehosting/FastDyn/cmsis-svd-data")

cpu0.add_map_file("/home/hammad/work/rehosting/FastDyn/courbet/rover_v422_map.txt")

#Add Memories to the machine

#Required: First add the main memory and then any other memory! : goes to -machine ... memory-backend=<id>
machine0.add_memory(memory_name="main",
                    memory_id = "ram0",
                    memory_start = "0x20000000",
                    memory_size="512M",
                    memory_type="SRAM",
                    backend      = "file",          # file | ram | memfd
                    memory_file="/dev/shm/my_m4_ram3",
                    share = True,
                    )

machine0.add_memory(memory_name="region1",
                    memory_id = "ram1",
                    memory_start = "0x30000000",
                    memory_size="512K",
                    memory_type="SRAM",
                    backend      = "file",          # file | ram | memfd
                    memory_file="/dev/shm/my_m4_ram",
                    share = True,
                    )

#-------------Devices-------------------#
#Define the supported Models for the current machine
machine0.add_model(name ="elder")
machine0.add_model(name ="passthrough", backend = "stlink")

#Attach devices to the machine
uart0 = machine0.add_device("uart")
uart0.add_ranges(
    start = "0x40021800",
    end = "0x40021BFF"
    )
uart0.add_irq([1, 20])

uart0.add_handler(
    name="passthrough",
    enabled=True,
)
uart0.add_handler(
    name="elder",
    enabled=False,
    scroll="boardrunner/boardrunner_sdk/build/model.so"
)

#device 2 -- remaining range
remaining_range = machine0.add_device("unhandled")

remaining_range.add_ranges(
    start= "0x40000000",
    end= "0x400217FF"
)
remaining_range.add_ranges(
    start= "0x40021C00",
    end= "0xE00FFFFF"
)
remaining_range.add_irq([1,20])
remaining_range.add_handler(
    name="passthrough",
    enabled=True,
)

print("Starting Fastdyn")

fastdyn.run(target="qemu",
            machine_name="machine0",
            out_path="/home/hammad/work/rehosting/FastDyn/fastdyn_work")               #run fastdyn machine that you want to run

fastdyn.shutdown()          #shutdown fastdyn machine :: Wont work rn, just press CTRL+C in terminal