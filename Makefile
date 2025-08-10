.PHONY: all clean

qemu_path ?= ../../qemu

all: clean setup
	meson setup build -Dqemu_path=$(qemu_path) \
	-Denable_libhw=true -Denable_libgz=true \
	-Ddevice_models=true
	ninja -C build

setup: clean
	mkdir -p build

clean:
	rm -rf build