# Elder Scroll Device Model

The **elder scroll device model** builds on both the classic and passthrough models. It automates device model generation using **automata learning** and **machine learning**, enabling automatic creation of device models for firmware. This model is particularly useful for advanced firmware analysis and large-scale automation.

##  Stped to Use the Elder Device Model in FastDyn

Before using the Elder device model, you should run the proxy firmware on the board. This firmware ensures we can quickly serve interrupts.

### 1. Build and Flash the Proxy Firmware
```bash
cd device_models/server_firmwares/stm32f4
make bc && make exec
./flash.sh ./build/RTOSDemo.axf
```

### 2. Run FastDyn Passthrough Device Model
Use the following trick to get the GPIO firmware to run in QEMU:
```bash
CFLAG=-DTRAIN_GPIO
```

### 3. Capture I/O Accesses
Once the proxy firmware is flashed, run QEMU with the following device model arguments to capture all I/O accesses in passthrough mode with ST-Link as the interface:
```bash
dev=passthrough:stlink*0x40000000-0x5FFFFFFF~0xE0000000-0xEFFFFFFF
```

### 4. Example Command
```bash
run --plugin /data/fastdyn/build/libfastdyn.so,dev=passthrough:stlink*0x40000000-0x5FFFFFFF~0xE0000000-0xEFFFFFFF,monitor=../ws/monitor.elf,logger=../ws/log_config.txt,virtual=../ws/virtuals.txt,detour=../ws/detours.txt,modifier=../ws/modifiers.txt -d in_asm,op -D qemu.log -machine cortexm,memory-backend=ram0 -monitor telnet:127.0.0.1:5555,server,nowait -semihosting --semihosting-config enable=on,target=native -qmp unix:/tmp/qmp-sock,server,nowait -kernel /home/faculty/abk6349/data/fastdyn/device_models/serve...
```

This will generate I/O logs. Make sure the device logger is enabled in FastDyn:
```c
#define DEV_LOGGER
```

## Example of `io.log`
```
[   14.401824] Read:   address = 0x40023830, size = 4 bytes, value = 0x00100000, pc=0x080006AA
[   14.402171] Write:  address = 0x40023830, size = 4 bytes, value = 0x00100040, pc=0x080006B0
[   14.402564] Read:   address = 0x40021800, size = 4 bytes, value = 0x00000000, pc=0x080006BA
[   14.402846] Write:  address = 0x40021800, size = 4 bytes, value = 0x00000000, pc=0x080006C0
[   14.403220] Read:   address = 0x40021800, size = 4 bytes, value = 0x00000000, pc=0x080006C2
[   14.403495] Write:  address = 0x40021800, size = 4 bytes, value = 0x55000000, pc=0x080006C8
[   14.403893] Read:   address = 0x40021814, size = 4 bytes, value = 0x00000000, pc=0x080006D4
[   14.404155] Write:  address = 0x40021814, size = 4 bytes, value = 0x0000f000, pc=0x080006DA
[   14.407555] Read:   address = 0x40021814, size = 4 bytes, value = 0x0000f000, pc=0x080006D4
[   14.407784] Write:  address = 0x40021814, size = 4 bytes, value = 0x00000000, pc=0x080006DA
```

## Next Steps
Create per-device automata for each device and pass them to the LLM for model generation.

[Back to Main Page](FastDynDeviceModel.md)

