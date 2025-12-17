from mmiofinder import find_mmio_accesses
from nextpc import next_pcs

blob = open("/data/fastdyn/device_models/server_firmwares/stm32f4/build/RTOSDemo.axf", "rb").read()

hits = find_mmio_accesses(
    blob=blob,
    base_va=0x08000000,
    arch="arm32",
    mode="thumb",
    mmio_regions=[(0x40000000, 0x5FFFFFFF)],
)

for h in hits:
    print(
        f"PC=0x{h.pc:x}  MMIO=0x{h.address:x}  "
        f"{h.instruction} ({h.base_register})"
    )
    pcs = next_pcs(
        blob=blob,
        pc=h.pc,
        base_va=0x08000000,
        arch="arm32",
        mode="thumb",
    )

    if len(pcs) == 1:
        print({hex(p) for p in pcs})
    else:
        print("What the hell!")
