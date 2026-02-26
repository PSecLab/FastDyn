# How to build server/idle firmware
As we discussed earlier, to run Fastdyn or related projects in HITL, we need a server or idle firmware, to build your own version of this firmware, follow the following instructions for the specific board.

1. Install compiler
```bash
sudo apt install -y clang llvm lld
```

2. Download gcc compiler:
```bash
https://developer.arm.com/-/media/Files/downloads/gnu/13.3.rel1/binrel/arm-gnu-toolchain-13.3.rel1-x86_64-arm-none-eabi.tar.xz
```

3. Now, move to the directory for which you want to build `idle_firmware.axf`. Let's take `STM32F429i_disc1` for example, run the following command:
```bash
cd STM32F429i_disc1
```

4. Update the Makefile with the required changes. Replace the following line with the path where you installed/unzip the compiler.
```Makefile
GCC_INSTALL ?= /scratch/Fastdyn/toolchains/arm-gnu-toolchain-13.3.rel1-x86_64-arm-none-eabi/
```

3. Run
```bash
make bc && make exec
```

You should be good to go!

