.PHONY: all clean

qemu_path ?= ../../qemu
LIBGZ ?= false
LIBHW ?= false
DEV ?= false

all: clean setup
	meson setup build -Dqemu_path=$(qemu_path) \
	-Denable_libhw=$(LIBHW) -Denable_libgz=$(LIBGZ) \
	-Ddevice_models=$(DEV)
	ninja -C build

setup: clean
	mkdir -p build

clean:
	rm -rf build
