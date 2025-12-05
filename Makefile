.PHONY: all clean docs

qemu_path ?= ../../qemu
LIBGZ ?= true
LIBHW ?= false
DEV ?= false
DEBUG_PRINT ?= false
LIBPY ?= false

all: clean setup
	meson setup build -Dqemu_path=$(qemu_path) \
	-Denable_libhw=$(LIBHW) -Denable_libgz=$(LIBGZ) \
	-Ddevice_models=$(DEV) -DDEBUG_PRINT=$(DEBUG_PRINT) \
	-Denable_libpy=${LIBPY}
	ninja -C build

setup: clean
	mkdir -p build

docs:
	doxygen Doxyfile

clean:
	rm -rf build docs/html

