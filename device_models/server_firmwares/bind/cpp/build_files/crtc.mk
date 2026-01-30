CRTC =./
CRTC_DIR =$(abspath $(CRTC))
#Files for CRT-C
CRTC_FILES += $(KERNEL_DIR)/FreeRTOS_checked_compat.c
#CRTC_FILES += $(KERNEL_DIR)/FreeRTOS_checked_port.c
CRTC_CFLAGS = -DCRTC
