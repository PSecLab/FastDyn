# Compiler and common flags
CC = gcc
CFLAGS = -g -shared -fPIC -I../../../include/ -I../../../../qemu/include/ -Iapis/include
LDFLAGS = `pkg-config --cflags --libs glib-2.0`
UTILS = /scratch/Fastdyn/FastDyn/utils/utils.c

# All API source files that will be built into a single shared library
API_SOURCES = ./apis/src/devmodels_apis.c ./apis/src/i2c_bus.c ./apis/src/spi_bus.c

# Default target: build the API library and the main device models
all: dma_gen.so gen.so

# --- Main Build Rules ---

# NEW: Rule to create the central shared library for all APIs
libdevapis.so: $(API_SOURCES)
	$(CC) $(CFLAGS) $(API_SOURCES) $(LDFLAGS) -o libdevapis.so

# MODIFIED: Builds the DMA model by linking against the API library
dma: dma_gen.so
dma_gen.so: ./tests/dma.c libdevapis.so
	$(CC) $(CFLAGS) ./tests/dma.c $(UTILS) -o dma_gen.so -L. -ldevapis $(LDFLAGS)

# MODIFIED: Builds the generic model (previously built by i2c/spi rules)
# by linking against the API library
gen.so: ./tests/gen.c libdevapis.so
	$(CC) $(CFLAGS) ./tests/gen.c $(UTILS) -o gen.so -L. -ldevapis $(LDFLAGS)

# Aliases for user convenience, they both now build the generic model correctly
i2c: gen.so
spi: gen.so

# UNCHANGED: This rule seems independent and doesn't use the conflicting APIs
slave:
	$(CC) $(CFLAGS) $(UTILS) ./tests/slave.c -o slave_gen.so $(LDFLAGS)

# UNCHANGED: This rule also appears independent
verify:
	$(CC) -g -shared -fPIC -I../../../include/ -I../../../../qemu/include/ `pkg-config --cflags --libs glib-2.0` ./tests/gen.c ./tests/libtest.c -o gen.so

# MODIFIED: Cleans up all generated shared objects
clean:
	rm -f *.so