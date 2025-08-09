.PHONY: all clean

qemu_path ?= ../../qemu

all: clean setup
	meson setup build -Dqemu_path=$(qemu_path) \
	-Denable_libhw=false -Denable_libgz=false \
	-Ddevice_models=false
	ninja -C build

setup: clean
	mkdir -p build

clean:
	rm -rf build