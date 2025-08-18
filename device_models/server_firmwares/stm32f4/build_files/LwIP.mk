SOURCE_FILES_LwIP = ./emul_libc.c
SOURCE_FILES_LwIP += ./bnch.c
SOURCE_FILES_LwIP += LwIP_HTTP_Server_Raw/Src/app_ethernet.c
SOURCE_FILES_LwIP += LwIP_HTTP_Server_Raw/Src/stm32f7xx_it.c
SOURCE_FILES_LwIP += LwIP_HTTP_Server_Raw/Src/main.c
SOURCE_FILES_LwIP += LwIP_HTTP_Server_Raw/Src/system_stm32f7xx.c
SOURCE_FILES_LwIP += LwIP_HTTP_Server_Raw/Src/ethernetif.c
#SOURCE_FILES_LwIP += LwIP_HTTP_Server_Raw/Src/httpd_cgi_ssi.c  This is for HTTPS Server 
SOURCE_FILES_LwIP += LwIP_HTTP_Server_Raw/Src/fsdata_custom.c
SOURCE_FILES_LwIP += Drivers/STM32F7xx_HAL_Driver/Src/stm32f7xx_hal.c
SOURCE_FILES_LwIP += Drivers/BSP/STM32F769I_EVAL/stm32f769i_eval.c
SOURCE_FILES_LwIP += Middlewares/Third_Party/LwIP/src/core/ipv4/dhcp.c
SOURCE_FILES_LwIP += Middlewares/Third_Party/LwIP/src/core/init.c
SOURCE_FILES_LwIP += Middlewares/Third_Party/LwIP/src/core/netif.c
SOURCE_FILES_LwIP += Drivers/STM32F7xx_HAL_Driver/Src/stm32f7xx_hal_rcc.c
SOURCE_FILES_LwIP += Drivers/STM32F7xx_HAL_Driver/Src/stm32f7xx_hal_eth.c
SOURCE_FILES_LwIP += Drivers/BSP/Components/dp83848/dp83848.c
SOURCE_FILES_LwIP += Middlewares/Third_Party/LwIP/src/core/pbuf.c
SOURCE_FILES_LwIP += Drivers/STM32F7xx_HAL_Driver/Src/stm32f7xx_hal_gpio.c
SOURCE_FILES_LwIP += Drivers/STM32F7xx_HAL_Driver/Src/stm32f7xx_hal_uart.c
SOURCE_FILES_LwIP += Drivers/STM32F7xx_HAL_Driver/Src/stm32f7xx_hal_cortex.c
SOURCE_FILES_LwIP += Middlewares/Third_Party/LwIP/src/core/timeouts.c
SOURCE_FILES_LwIP += Drivers/STM32F7xx_HAL_Driver/Src/stm32f7xx_hal_i2c.c
SOURCE_FILES_LwIP += Middlewares/Third_Party/LwIP/src/core/def.c
SOURCE_FILES_LwIP += Drivers/STM32F7xx_HAL_Driver/Src/stm32f7xx_hal_pwr_ex.c
SOURCE_FILES_LwIP += Middlewares/Third_Party/LwIP/src/netif/ethernet.c
SOURCE_FILES_LwIP += Middlewares/Third_Party/LwIP/src/core/mem.c    
SOURCE_FILES_LwIP += Middlewares/Third_Party/LwIP/src/core/udp.c
SOURCE_FILES_LwIP += Middlewares/Third_Party/LwIP/src/core/ipv4/ip4_addr.c
SOURCE_FILES_LwIP += Middlewares/Third_Party/LwIP/src/core/tcp.c
SOURCE_FILES_LwIP += Middlewares/Third_Party/LwIP/src/core/ipv4/etharp.c
SOURCE_FILES_LwIP += Drivers/STM32F7xx_HAL_Driver/Src/stm32f7xx_hal_dma.c
SOURCE_FILES_LwIP += Middlewares/Third_Party/LwIP/src/core/ipv4/icmp.c
SOURCE_FILES_LwIP += Middlewares/Third_Party/LwIP/src/core/ip.c
SOURCE_FILES_LwIP += Middlewares/Third_Party/LwIP/src/core/memp.c
SOURCE_FILES_LwIP += Drivers/STM32F7xx_HAL_Driver/Src/stm32f7xx_hal_adc.c
SOURCE_FILES_LwIP += Middlewares/Third_Party/LwIP/src/core/tcp_out.c
SOURCE_FILES_LwIP += Middlewares/Third_Party/LwIP/src/core/ipv4/ip4.c
SOURCE_FILES_LwIP += Middlewares/Third_Party/LwIP/src/core/inet_chksum.c
SOURCE_FILES_LwIP += Middlewares/Third_Party/LwIP/src/core/ipv4/ip4_frag.c
SOURCE_FILES_LwIP += Drivers/STM32F7xx_HAL_Driver/Src/stm32f7xx_hal_adc_ex.c
SOURCE_FILES_LwIP += Middlewares/Third_Party/LwIP/src/core/tcp_in.c
SOURCE_FILES_LwIP += ./LwIP_HTTP_Server_Raw/Src/tcp_echoserver.c 

INCLUDE_DIRS_LwIP = -I ./Middlewares/Third_Party/LwIP/src/include/ -I ./Drivers/CMSIS/Device/ST/STM32F7xx/Include/ -I ./Drivers/CMSIS/Include/ -I ./ -I /usr/lib/gcc/arm-none-eabi/9.2.1/include -I  /usr/lib/gcc/arm-none-eabi/9.2.1/../../../arm-none-eabi/include/ -I /usr/lib/gcc/arm-none-eabi/9.2.1/include -I Middlewares/Third_Party/LwIP/src/include/ -I ./LwIP_HTTP_Server_Raw/Inc/ -I ./Middlewares/Third_Party/LwIP/system/ -I ./Drivers/STM32F7xx_HAL_Driver/Inc/ -I ./Drivers/BSP/STM32F769I_EVAL/ -I ./Middlewares/Third_Party/LwIP/src/apps/http/

#${CFLAGS_${PROJECT}_${DEMO}}
CFLAGS_EC_LwIP = -flto=thin --target=arm-none-eabi -mthumb -mcpu=cortex-m4 -mfloat-abi=hard -mfpu=fpv4-sp-d16 -O0 -g -DSTM32F769xx -DCONFIG_CPU_CORTEX_M7 -DBAREMTL 

#EC_RELATED_FLAGS
CFLAGS_CRTC_LwIP = $(FREERTOS_EXE_CFLAGS) -DCRTC


SOURCE_FILES_EC_LwIP = $(SOURCE_FILES_LwIP) $(EC_FILES)


INCLUDE_DIRS_${PROJECT}_LwIP = $(INCLUDE_DIRS_LwIP)
