.PHONY: all clean docs setup build_boardrunner

qemu_path    ?= ../qemu
libhw_path   ?= ../libhw
LIBGZ        ?= false
LIBHW        ?= true
DEV          ?= true
DEBUG_PRINT  ?= true
LIBPY        ?= false

BOARD_RUNNER ?= true

# Top-level target: clean, configure with meson, then build with ninja
all: setup
	ninja -C build

# Run meson configuration (after clean), and optionally copy libhw.so
setup: clean
	mkdir -p build
	meson setup build -Dqemu_path=$(qemu_path) \
		-Denable_libhw=$(LIBHW) -Denable_libgz=$(LIBGZ) \
		-Ddevice_models=$(DEV) -DDEBUG_PRINT=$(DEBUG_PRINT) \
		-Denable_libpy=$(LIBPY)
	@if [ "$(BOARD_RUNNER)" = "true" ]; then \
		$(MAKE) build_boardrunner; \
	fi

# Copy libhw.so into the build directory
build_boardrunner:
	cp $(libhw_path)/out/libhw.so build/

docs:
	doxygen Doxyfile

clean:
	rm -rf build docs/html

test:
	pytest

test_versbose:
	pytest -o log_cli=true --log-cli-level=INFO
