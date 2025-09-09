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

#### Example of `io.log`
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

### 5. Automata Creation
Create per-device automata for each device and pass them to the LLM for model generation. I recommend using a virtual environment for this.

```bash
python3 -mvenv fastdyn
python3 ./fastdyn/bin/activate
pip3 install -r ./requirements.txt
cd device_models/postmartem
make new # This assumes qemu path, open Makefile to change for now.
```

If everything goes perfectly, you should see prints like this:
```
Analysis starting. Output will be saved in the 'out_min_ctxt' directory.
------------------------------------------------------------
--- Analyzing Global Interrupt Behavior ---
  Analyzing Interrupt Service Routines (ISRs)...
------------------------------------------------------------

--- Analyzing Peripheral: GPIOG ---
  Separating accesses...
  Separation complete. Init accesses: 4, Runtime accesses: 2350
  Initialization context saved to: out_min_ctxt/GPIOG/init.txt
  Full runtime trace saved to: out_min_ctxt/GPIOG/runtime_full_trace.txt
  Finding patterns in runtime accesses...
  Detected pattern saved to: out_min_ctxt/GPIOG/loop_pattern_1.txt
  Detected pattern saved to: out_min_ctxt/GPIOG/loop_pattern_2.txt
  Analyzing stateful behavior...
  Stateful analysis saved to: out_min_ctxt/GPIOG/state.txt
  Analyzing entropy of read values...
  Entropy analysis saved to: out_min_ctxt/GPIOG/entropy.txt

--- Analyzing Peripheral: RCC ---
  Separating accesses...
  No dominant repetitive operation found. Classifying all as initialization.
  Initialization context saved to: out_min_ctxt/RCC/init.txt
  Full runtime trace saved to: out_min_ctxt/RCC/runtime_full_trace.txt
  Finding patterns in runtime accesses...
  Analyzing stateful behavior...
  Stateful analysis saved to: out_min_ctxt/RCC/state.txt
  Analyzing entropy of read values...
  Entropy analysis saved to: out_min_ctxt/RCC/entropy.txt
------------------------------------------------------------
Analysis complete.
```

The out_min_ctxt folder has the output related to each device. 

```
(fastdyn) abk6349@e5-cse-359-01:~/data/fastdyn/device_models/postmartem$ tree ./out_min_ctxt/
./out_min_ctxt/
├── GPIOG
│   ├── entropy.txt
│   ├── init.txt
│   ├── loop_pattern_1.txt
│   ├── loop_pattern_2.txt
│   ├── runtime_full_trace.txt
│   └── state.txt
├── RCC
│   ├── entropy.txt
│   ├── init.txt
│   ├── runtime_full_trace.txt
│   └── state.txt
└── summary.txt

3 directories, 11 files

```

The meaning of each file is as follows:
- entropy.txt: Finds the entropy of each register; high entropy indicates it's a data field.
- init.txt: Finds the initialization patterns for each device.
- loop_pattern_XX: Tells about extracted automaton/recurring patterns for each device.
- state.txt: Tells about stateful registers, indicating device register is stateful.
- summary.txt: Gives an overall summary of the analysis.

### 6. Prompt Creation/Model Generation.
If you have an OPENAI Key add it to prompt.py to automatiaclly run the query. If no, your prompt_gen.py to generate the query for one of the device. 

```
python3 ./prompt_gen.py GPIOG
cat ./prompt.txt
```

### 7. Running verifier.
Since the output of LLM maybe buggy, we run the verifier in a low-cost emulator to replay the learned trace to verify the accuracy of the generated model. If the same trace is replicated, it means the firmware will not know the difference in the IO Model of the device. 
```
cd verifier
```
Paste the code in gen.c 
```
make
python3 ./test.py --lib ./gen.so  --trace /data/qemu/build/io.log --init-func gpiog_init --read-func gpiog_read --write-func gpiog_write --base-addr 0x40021800 #Make sure to fix things accordingly.
```

### 8. Reiterate until no mismatches.
If there are no matches, the verifier will print messages like this:
```
--- Harness Configuration ---
  Library Path: ./gen.so
  Trace File:   /data/qemu/build/io.log
  Base Address: 0x40021800
  Memory Size:  0x1000
  Init Func:    'gpiog_init'
  Read Func:    'gpiog_read'
  Write Func:   'gpiog_write'
---------------------------
Result -> Matches: 1177, Errors: 0, Writes: 1177
```

After this, save the newly learned model (the gen.so file) in the scroll for the elder scroll model. This is how the scroll looks:
``` ini
[gpiog]
libpath = /home/faculty/abk6349/data/fastdyn/device_models/postmartem/verifier/gen.so
base = 0x40021800
size = 0x1000
```

### 9. Run FastDyn with the Elder Scroll device model. 
Use the elder device model, you can tell it about the scroll using an @ seperator, this is what the input to elder model look like:
```
dev=elder:[libhw backend]@[path-to-a scroll]*[input-ranges to listen to]
```
Here is an example:
```
dev=elder:stlink@/home/faculty/abk6349/data/fastdyn/device_models/elder/tests/devices.ini*0x40000000-0x5FFFFFFF~0xE0000000-0xEFFFFFFF
```
Example command for full invocation:
```
run --plugin /data/fastdyn/build/libfastdyn.so,dev=elder:stlink@/home/faculty/abk6349/data/fastdyn/device_models/elder/tests/devices.ini*0x40000000-0x5FFFFFFF~0xE0000000-0xEFFFFFFF,monitor=../ws/monitor.elf,logger=../ws/log_config.txt,virtual=../ws/virtuals.txt,detour=../ws/detours.txt,modifier=../ws/modifiers.txt -d in_asm,op -D qemu.log -machine cortexm,memory-backend=ram0 -monitor telnet:127.0.0.1:5555,server,nowait -semihosting --semihosting-config enable=on,target=native -qmp unix:/tmp/qmp-sock,server,nowait -kernel /home/faculty/abk6349/data/fastdyn/device_models/server_firmwares/stm32f4/RTOSDemo.axf -serial stdio -nographic -object memory-backend-file,id=ram0,mem-path=/dev/shm/my_m4_ram3,size=512M,share=on -object memory-backend-file,id=ram1,mem-path=/dev/shm/my_m4_ram,size=512K,share=on -global cortexm-soc.shram_backend=ram1  -global cortexm-soc.ram_baseaddr=0x20000000  -global cortexm-soc.shram_baseaddr=0x30000000 -qmp unix:/tmp/qmp.sock,server=on,wait=off -chardev socket,id=char0,path=/tmp/usart1.sock,server=on,wait=off -device stm32f2xx-usart,id=usart1,chardev=char0,addr=0x40011000 -cpu cortex-m7 -global armv7m.init-nsvtor=0x08000000 -S -s
```
If everything goes successfully, fastdyn will load your new learned device model:
```
Will use Backend: stlink
and the scroll: /home/faculty/abk6349/data/fastdyn/device_models/elder/tests/devices.ini
Loading device [gpiog] from /home/faculty/abk6349/data/fastdyn/device_models/postmartem/verifier/gen.so
```

Enjoy a faster emulation of the learned GPIO model!!

[Back to Main Page](FastDynDeviceModel.md)

