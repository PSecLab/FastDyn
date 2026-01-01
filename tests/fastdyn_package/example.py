from fastdyn.fastdyn import *


fastdyn = Fastdyn()

#TODO: update to create_machine
machine0 = fastdyn.create_machine("machine0")
#TODO: Binary should be taken as an object for binary analysis
cpu0 = machine0.add_cpu(
    arch="arm",
    machine="cortexm",
    cpu="cortex-m7",
    binary="boardrunner/boardrunner_examples/examples/STM32F769i-eval/UART/Firmwares/uart.elf",
    init_nsvtor="0x08000000",
    platform="STM32F7x9"
    )

#TODO: Add support for symbol resolution instead of hard coded address support only
vi0 = VirtualInstruction(at= 0x80000000, instruction="raiseirq", args=["0x000000"])

# cpu0.add_virtual_instruction(vi0)

modifier0 = InstructionModifier(at=0x80000000, patch="read")

# cpu0.add_modifier(modifier0)

#Add a memory to the machine
main_ram_size = "512M"
shared_ram_size = "512K"
ram_base_addr = "0x20000000"
shared_ram_base_addr = "0x30000000"

machine0.add_memory(memory_name="ram0",
                memory_start = ram_base_addr,
                memory_size=main_ram_size,
                memory_type="SRAM",
                memory_file="/dev/shm/my_m4_ram3"
                )

machine0.add_memory(memory_name="ram1",
                memory_start = shared_ram_base_addr,
                memory_size=shared_ram_size,
                memory_type="SRAM",
                memory_file="/dev/shm/my_m4_ram"
                )

#TODO: Define functions like list_machines, list_cpus to remove this
for machine in fastdyn.machines:
    print(f"Machine: {fastdyn.machines[machine].name} details::")
    for cpu in fastdyn.machines[machine].cpus:
        print(f"{cpu}")


#-------------Devices-------------------#
#Define the supported Models for the current machine
machine0.add_model(name ="elder")
machine0.add_model(name ="passthrough", backend = "stlink")

#Attach devices to the machine
uart0 = machine0.add_device("uart")
uart0.add_ranges(["0x40011000-0x40011FFF"])
uart0.add_irq_ranges("0-15:20-25:53")
uart0.add_handler(
    name="qemu",
    type="stm32f2xx-usart",
    enabled=False,
    args="device chardev",
)
uart0.add_handler(
    name="passthrough",
    enabled=True,
)
uart0.add_handler(
    name="elder",
    enabled=False,
    scroll="/scratch/Fastdyn/FastDyn/device_models/postmartem/verifier/gen.so"
)

remaining_range = machine0.add_device("unhandled")
remaining_range.add_ranges(["0x40000000-0x40010FFF", "0x40012000-0xE00FFFFF", "0xE0000000-0xEFFFFFFF"])
remaining_range.add_irq_ranges("0-15:20-25:53")
remaining_range.add_handler(
    name="passthrough",
    enabled=True,
)

print("Starting Fastdyn")
fastdyn.run("machine0", out_path="/home/hammad/work/rehosting/FastDyn/fastdyn_work")               #run fastdyn machine that you want to run
fastdyn.shutdown()          #shutdown fastdyn machine :: Wont work rn, just press CTRL+C in terminal