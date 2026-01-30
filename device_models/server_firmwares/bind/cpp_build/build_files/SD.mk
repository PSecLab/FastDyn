SOURCE_FILES_${PROJECT}_SD = FatFs_uSD/Src/stm32f4xx_it.c
SOURCE_FILES_${PROJECT}_SD += FatFs_uSD/Src/main.c
SOURCE_FILES_${PROJECT}_SD += FatFs_uSD/Src/sd_diskio.c
SOURCE_FILES_${PROJECT}_SD += FatFs_uSD/Src/system_stm32f4xx.c
SOURCE_FILES_${PROJECT}_SD += Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal.c
SOURCE_FILES_${PROJECT}_SD += Drivers/BSP/STM32469I-Discovery/stm32469i_discovery_sd.c
SOURCE_FILES_${PROJECT}_SD += Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_dma.c
SOURCE_FILES_${PROJECT}_SD += Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_sd.c
SOURCE_FILES_${PROJECT}_SD += Drivers/BSP/STM32469I-Discovery/stm32469i_discovery.c
SOURCE_FILES_${PROJECT}_SD += Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_cortex.c
SOURCE_FILES_${PROJECT}_SD += Middlewares/Third_Party/FatFS/src/ff.c
SOURCE_FILES_${PROJECT}_SD += Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_ll_sdmmc.c
SOURCE_FILES_${PROJECT}_SD += Middlewares/Third_Party/FatFS/src/diskio.c
SOURCE_FILES_${PROJECT}_SD += Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_gpio.c
SOURCE_FILES_${PROJECT}_SD += Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_i2c.c
SOURCE_FILES_${PROJECT}_SD += Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_rcc_ex.c
SOURCE_FILES_${PROJECT}_SD += Middlewares/Third_Party/FatFS/src/ff_gen_drv.c
SOURCE_FILES_${PROJECT}_SD += Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_rcc.c
SOURCE_FILES_${PROJECT}_SD += Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_pwr_ex.c
SOURCE_FILES_${PROJECT}_SD += ./rtmk.c
SOURCE_FILES_${PROJECT}_SD += ./bnch.c



INCLUDE_DIRS_${PROJECT}_SD = -I FatFs_uSD/Inc/ -I /usr/lib/gcc/arm-none-eabi/9.2.1/include -I  /usr/lib/gcc/arm-none-eabi/9.2.1/../../../arm-none-eabi/include/ -I /usr/lib/gcc/arm-none-eabi/9.2.1/include
INCLUDE_DIRS_${PROJECT}_SD += -I ./Drivers/STM32F4xx_HAL_Driver/Inc/
INCLUDE_DIRS_${PROJECT}_SD += -I ./Drivers/CMSIS/Device/ST/STM32F4xx/Include/
INCLUDE_DIRS_${PROJECT}_SD += -I ./Drivers/CMSIS/Include/
INCLUDE_DIRS_${PROJECT}_SD += -I ./Drivers/BSP/STM32469I-Discovery/ -I ./Middlewares/Third_Party/FatFS/src/ -I ./


CFLAGS_${PROJECT}_${SD} = -DSTM32F469x -DBAREMTL
