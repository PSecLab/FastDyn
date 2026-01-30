FREERTOS_DIR_REL := ../../../FreeRTOS
FREERTOS_DIR := $(abspath $(FREERTOS_DIR_REL))
FRTOS=../../Source/
KERNEL = $(abspath $(FRTOS))
KERNEL_DIR = $(KERNEL)
INCLUDE_KERNEL=$(KERNEL)/include/
INCLUDE_PORT=$(KERNEL)/portable/GCC/ARM_CM7/r0p1
INCLUDE_COMMON=../Common/include/


#Add FreeRTOS Kernel files
FREERTOS_FILES += $(KERNEL_DIR)/tasks.c
FREERTOS_FILES += ./main.c
FREERTOS_FILES +=$(KERNEL_DIR)/list.c
FREERTOS_FILES +=$(KERNEL_DIR)/queue.c
FREERTOS_FILES +=$(KERNEL_DIR)/timers.c
FREERTOS_FILES += $(KERNEL_DIR)/event_groups.c
FREERTOS_FILES +=$(KERNEL)/portable/GCC/ARM_CM4F/port.c
FREERTOS_FILES += $(KERNEL)/portable/MemMang/heap_1.c





#Add FreeRTOS platform/arch files 
#FREERTOS_FILES += ./startup/system_stm32f4xx.c
#FREERTOS_FILES += ./ParTest.c



INCLUDE_DIR += -I /usr/lib/gcc/arm-none-eabi/9.2.1/include -I  /usr/lib/gcc/arm-none-eabi/9.2.1/../../../arm-none-eabi/include/ -I /usr/lib/gcc/arm-none-eabi/9.2.1/include
#FREERTOS_FILES += ./bnch.c


#Include FreeRTOS directories
FREERTOS_INCLUDE_DIRS += -I$(KERNEL_DIR)/include
FREERTOS_INCLUDE_DIRS += -I$(INCLUDE_PORT) -I$(INCLUDE_COMMON)
FREERTOS_INCLUDE_DIRS += -I./
FREERTOS_INCLUDE_DIRS += -I /usr/lib/gcc/arm-none-eabi/9.2.1/include -I  /usr/lib/gcc/arm-none-eabi/9.2.1/../../../arm-none-eabi/include/ 





#Include demo related configuration files. 
ifeq ($(DEMO), Flash)
		include build_files/flash.mk
endif
ifeq ($(DEMO), StreamBuffer)
        include build_files/streambuffer.mk
endif
ifeq ($(DEMO), RECM)
        include build_files/recm.mk
endif
ifeq ($(DEMO), QSET)
        include build_files/qset.mk
endif
ifeq ($(DEMO), CR)
        include build_files/cr.mk
endif
ifeq ($(DEMO), INT)
        include build_files/int.mk
endif
ifeq ($(DEMO), Attack)
        include build_files/att.mk
endif
ifeq ($(DEMO), FreeRTOSSD)
        include build_files/sd.mk #This is the hosted verions of FreeRTOS.
endif



FREERTOS_KLEE_FILES= ./klee_env.c

#Configure CFLAGS based on the required project/demo
FREERTOS_EXE_CFLAGS=-flto=thin --target=arm-none-eabi -mthumb -mcpu=cortex-m4 -mfloat-abi=hard -mfpu=fpv4-sp-d16 -O0 -g -DFREERTOSCONFIG#Currently only STM32


FREERTOS_KLEE_CFLAGS= -flto=thin -g -DKLEE

#Additional Files 
FREERTOS_KLEE_FILES += /home/arslan/projects/LBC/FreeRTOS/FreeRTOS/Source/portable/GCC/ARM_CM7/port.c






#Its OK to use the DEMO switch here , cuz later things will override
#${CFLAGS_${PROJECT}_${DEMO}}
CFLAGS_EC_${DEMO} = $(FREERTOS_EXE_CFLAGS)

#EC_RELATED_FLAGS
CFLAGS_CRTC_${DEMO} = $(FREERTOS_EXE_CFLAGS) -DCRTC

#KLEE Flags
CFLAGS_KLEE_${DEMO} = $(FREERTOS_KLEE_CFLAGS)

#${SOURCE_FILES_${PROJECT}_${DEMO}}
SOURCE_FILES_EC_${DEMO} = $(FREERTOS_FILES)  $(DEMO_FILES) $(EC_FILES)

SOURCE_FILES_CRTC_${DEMO} = $(FREERTOS_FILES)  $(DEMO_FILES) $(CRTC_FILES)

SOURCE_FILES_KLEE_${DEMO}= $(DEMO_FILES) $(FREERTOS_KLEE_FILES)

#${INCLUDE_DIRS_${PROJECT}_${DEMO}}
INCLUDE_DIRS_${PROJECT}_${DEMO} = $(FREERTOS_INCLUDE_DIRS)
