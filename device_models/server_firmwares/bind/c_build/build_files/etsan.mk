ETSAN_FILES += ./etsan.c

#These are some hack files, cuz the original HAL couldn't make the trace output work.
ETSAN_FILES += ./extern/libopencm3-examples/libopencm3/lib/stm32/f4/rcc.c
ETSAN_FILES += ./extern/libopencm3-examples/libopencm3/lib/cm3/systick.c
ETSAN_FILES += extern/libopencm3-examples/libopencm3/lib/stm32/common/rcc_common_all.c
ETSAN_FILES += extern/libopencm3-examples/libopencm3/lib/stm32/f4/pwr.c
ETSAN_FILES += extern/libopencm3-examples/libopencm3/lib/stm32/common/flash_common_idcache.c
ETSAN_FILES += extern/libopencm3-examples/libopencm3/lib/stm32/common/flash_common_all.c

ETSAN_INCLUDE_DIRS += -I ./extern/libopencm3-examples/libopencm3/include/
ETSAN_CFLAGS += -DSTM32F4xx -DSTM32F4 -Dlibopencm3
