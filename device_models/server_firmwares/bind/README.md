# How to build server firmware

1. Install compiler
```bash
sudo apt install -y clang llvm lld
```
2. Download gcc compiler:
```bash
https://developer.arm.com/-/media/Files/downloads/gnu/13.3.rel1/binrel/arm-gnu-toolchain-13.3.rel1-x86_64-arm-none-eabi.tar.xz
```
3. Run
```bash
make bc && make exec
```

You should be good to go!

