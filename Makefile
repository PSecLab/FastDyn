.PHONY: all clean docs setup build_boardrunner

qemu_path    ?= ../qemu
libhw_path   ?= ../libhw
LIBGZ        ?= false
LIBHW        ?= true
LIBFUZZ		 ?= false
DEV          ?= true
DEBUG_PRINT  ?= true
LIBPY        ?= false

BOARD_RUNNER ?= true

# Top-level target: clean, configure with meson, then build with ninja
all: setup
	ninja -C build

# Run meson configuration (after clean), and optionally copy libhw.so
setup: clean fetch
	mkdir -p build
	meson setup build \
	-Dqemu_path=$(abspath $(qemu_path)) \
	-Dlibhw_path=$(abspath $(libhw_path)) \
	-Denable_libhw=$(LIBHW) \
	-Denable_libgz=$(LIBGZ) \
	-Denable_libfuzz=$(LIBFUZZ) \
	-Ddevice_models=$(DEV) \
	-DDEBUG_PRINT=$(DEBUG_PRINT) \
	-Denable_libpy=$(LIBPY)

	@if [ "$(BOARD_RUNNER)" = "true" ]; then \
		$(MAKE) build_boardrunner; \
	fi

# Copy libhw.so into the build directory
build_boardrunner:
	@if [ "$(LIBHW)" = "true" ]; then \
		cp $(libhw_path)/out/libhw.so build/; \
	fi

docs:
	doxygen Doxyfile

clean:
	rm -rf build docs/html

fetch:
	@# Only fetch submodules that are required for the selected features.
	@if [ "$(DEV)" = "true" ] && [ "$(LIBHW)" = "true" ]; then \
		git submodule update --init device_models/elder/inih third_party/cmsis-svd-data; \
	fi
	@if [ "$(LIBGZ)" = "true" ]; then \
		git submodule update --init submodules/mavlink_headers submodules/SITL_Models; \
	fi

test:
	pytest

test_versbose:
	pytest -o log_cli=true --log-cli-level=INFO
