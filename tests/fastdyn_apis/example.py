import sys
from fastdyn.fastdyn import *


fastdyn = Fastdyn()

#TODO: update to create_machine
machine0 = fastdyn.create_machine(machine_name="machine0",
                                  platform="STM32F7x9"
                                  )
#TODO: Binary should be taken as an object for binary analysis
cpu0 = machine0.add_cpu(
    arch="arm",
    machine="cortexm",
    cpu="cortex-m7",
    binary="boardrunner/boardrunner_examples/examples/STM32F769i-eval/UART/Firmwares/uart.elf",
    init_nsvtor="0x08000000"
    )

#Optional: add a platform svd file -- helps for irq resolution in Virtual instructions and modifiers
#Either specify the exact path to the svd for the platform, or just add the path of the svd and add the name of platform used from machine.
machine0.add_cmsis_svd(cmsis_svd="<path/to>/FastDyn/cmsis-svd-data")

#Optional: add map file -- helpful for creating virtual instructions and modifiers just using the symbols instead of hardcoded addresses
cpu0.add_map_file("<path/to>/FastDyn/courbet/rover_v422_map.txt")
# print(machine0.irq_map)

#VI can be created using four options:

#Option 1: Use hex, int addresses and add to cpu0 list of virtual instructions
vi0 = "0x80000000 raise_irq 0x000001"
cpu0.add_virtual_instruction(vi0)

#Option 2: Use hex, int addresses and add to cpu0 list of virtual instructions
vi0 = VirtualInstruction(at= 0x80000000, instruction="raise_irq", args=["0x000001"])
cpu0.add_virtual_instruction(vi0)

#Option 3: Use map file and cmsis-svd (irq map) to solve the symbols and add to the cpu0 list of virtual instructon
vi0 = VirtualInstruction(at= "spi_lld_serve_tx_interrupt", instruction="raise_irq", args=["USART1"])
cpu0.add_virtual_instruction(vi0)

#Option 4: Use map file and cmsis-svd (irq map) to solve the symbols and add to the cpu0 list of virtual instructon
# Here, you can pass a list of virtual instructions or modifiers and add to the cpu list
# as you can see you can use both options: Vi format or just pass as a string!
vi_list = ["spi_lld_serve_tx_interrupt raiseirq USART1",
           "0x80000000 raise_irq 0x000001",
            VirtualInstruction(at= "spi_lld_serve_tx_interrupt", instruction="raise_irq", args=["USART1"]),
            VirtualInstruction(at= "spi_lld_serve_tx_interrupt", instruction="anchor", args=["/tmp/local/uart"])
           ]
cpu0.add_virtual_instruction(vi_list)

print(cpu0.virtuals)

# Modifiers can be created using four options:

# Option 1: Use hex/int addresses directly (string form: "<at> <lhs> <rhs>")
mod0 = "0x80d9182 r15 0x80d9184"
cpu0.add_modifier(mod0)

# Option 2: Use dataclass directly
mod0 = InstructionModifier(at=0x80d9182, patch="r15 0x80d9184")
cpu0.add_modifier(mod0)

# Option 3: Use map symbols for 'at' (requires cpu0.add_map_file(...) already called)
# Example patches that set return value or redirect control flow via regs
mod0 = InstructionModifier(at="_ZN8AP_Param5flushEv", patch="r0 0")
cpu0.add_modifier(mod0)

mod1 = InstructionModifier(at="_ZN8AP_Param5flushEv", patch="r15 r14")
cpu0.add_modifier(mod1)

# Option 4: Pass a list (mix strings + dataclasses)
mod_list = [
    "_ZN8AP_Param5flushEv r0 0",
    "_ZN8AP_Param5flushEv r15 r14",
    "0x8028f2e r15 0x802a1ea",
    InstructionModifier(at="stm32_clock_init", patch="r0 0"),
    InstructionModifier(at="stm32_clock_init", patch="r15 r14"),
]
cpu0.add_modifier(mod_list)

print(cpu0.modifiers)

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

print(machine0.memories)

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
#Device ranges can be added using two options
#Option 1: Pass start and end of the range
uart0.add_ranges(
    start = "0x40000000",
    end = "0x40010FFF"      #you can pass both integer or str for these options
    )
#Option 2: Pass start and size of the range
uart0.add_ranges(
    start = "0x40000000",
    size = "4KB"      #you can pass KB, MB, GB
    )

print(uart0.supported_ranges)


#IRQs can be added using three options:
#option 1: Adding a single irq
uart0.add_irq(1)        #this will add this irq to the uart device

#option 2: Adding multiple irqs
uart0.add_irq([2, 15])  #this will add irqs from 2 to 15 (inclusive)
uart0.add_irq([20, 25])  #this will add irqs from 20 to 25 (inclusive)
uart0.add_irq(53)        #this will add this irq  to the uart device

#option 3: works only if you have given the cmsis svd file above else will throw an error
uart0.add_irq("UART7")

print(uart0.irq_range)
sys.exit(1)

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

sys.exit(1)

remaining_range = machine0.add_device("unhandled")
remaining_range.add_ranges(["0x40000000-0x40010FFF", "0x40012000-0xE00FFFFF", "0xE0000000-0xEFFFFFFF"])
remaining_range.add_irq_ranges("0-15:20-25:53")
remaining_range.add_handler(
    name="passthrough",
    enabled=True,
)

print("Starting Fastdyn")

sys.exit(1)

fastdyn.run(target="qemu",
            machine="machine0",
            out_path="<path/to>/FastDyn/fastdyn_work")               #run fastdyn machine that you want to run
fastdyn.shutdown()          #shutdown fastdyn machine :: Wont work rn, just press CTRL+C in terminal