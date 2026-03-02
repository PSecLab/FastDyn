#include <stdint.h>

/* Macro to define the "Hypercall" Stub (same name as your F1/F7 version) */
#define DEFINE_INTERRUPT_HANDLER(handler_name, id)                               \
__attribute__((naked, used, retain)) void handler_name(void) {                   \
    __asm volatile (                                                            \
        "cpsid i            \n" /* prevent nested IRQs while halted */          \
        "movw r2, %c[val]   \n" /* r2 = ID (constant) */                        \
        "1:                 \n"                                                 \
        "mov  r0, r2        \n" /* r0 = ID (tool reads this) */                 \
        "bkpt #0            \n" /* halt here */                                 \
        "cmp  r1, r2        \n" /* handshake: tool must write same ID into r1 */\
        "bne  1b            \n"                                                 \
        "cpsie i            \n" /* re-enable IRQs before returning */           \
        "bx   lr            \n" /* return from ISR */                           \
        :                                                                       \
        : [val] "i" ((uint32_t)(id))                                            \
        : "r0", "r1", "r2", "memory", "cc"                                      \
    );                                                                          \
}

/******************************************************************************/
/* Cortex-M7 Processor Exceptions (IDs 2 - 15)                                 */
/******************************************************************************/

DEFINE_INTERRUPT_HANDLER(NMI_Handler,        2)
DEFINE_INTERRUPT_HANDLER(HardFault_Handler,  3)
DEFINE_INTERRUPT_HANDLER(MemManage_Handler,  4)
DEFINE_INTERRUPT_HANDLER(BusFault_Handler,   5)
DEFINE_INTERRUPT_HANDLER(UsageFault_Handler, 6)
/* Reserved 7-10 */
DEFINE_INTERRUPT_HANDLER(SVC_Handler,        11)
DEFINE_INTERRUPT_HANDLER(DebugMon_Handler,   12)
/* Reserved 13 */
DEFINE_INTERRUPT_HANDLER(PendSV_Handler,     14)
DEFINE_INTERRUPT_HANDLER(SysTick_Handler,    15)

/******************************************************************************/
/* STM32H743 Peripheral Interrupts (NVIC IRQ Numbers)                          */
/* These IDs match the order in your startup_stm32h743xx.s vector table.       */
/******************************************************************************/

DEFINE_INTERRUPT_HANDLER(WWDG_IRQHandler,                  0)
DEFINE_INTERRUPT_HANDLER(PVD_AVD_IRQHandler,               1)
DEFINE_INTERRUPT_HANDLER(TAMP_STAMP_IRQHandler,            2)
DEFINE_INTERRUPT_HANDLER(RTC_WKUP_IRQHandler,              3)
DEFINE_INTERRUPT_HANDLER(FLASH_IRQHandler,                 4)
DEFINE_INTERRUPT_HANDLER(RCC_IRQHandler,                   5)
DEFINE_INTERRUPT_HANDLER(EXTI0_IRQHandler,                 6)
DEFINE_INTERRUPT_HANDLER(EXTI1_IRQHandler,                 7)
DEFINE_INTERRUPT_HANDLER(EXTI2_IRQHandler,                 8)
DEFINE_INTERRUPT_HANDLER(EXTI3_IRQHandler,                 9)
DEFINE_INTERRUPT_HANDLER(EXTI4_IRQHandler,                10)
DEFINE_INTERRUPT_HANDLER(DMA1_Stream0_IRQHandler,         11)
DEFINE_INTERRUPT_HANDLER(DMA1_Stream1_IRQHandler,         12)
DEFINE_INTERRUPT_HANDLER(DMA1_Stream2_IRQHandler,         13)
DEFINE_INTERRUPT_HANDLER(DMA1_Stream3_IRQHandler,         14)
DEFINE_INTERRUPT_HANDLER(DMA1_Stream4_IRQHandler,         15)
DEFINE_INTERRUPT_HANDLER(DMA1_Stream5_IRQHandler,         16)
DEFINE_INTERRUPT_HANDLER(DMA1_Stream6_IRQHandler,         17)
DEFINE_INTERRUPT_HANDLER(ADC_IRQHandler,                  18)
DEFINE_INTERRUPT_HANDLER(FDCAN1_IT0_IRQHandler,           19)
DEFINE_INTERRUPT_HANDLER(FDCAN2_IT0_IRQHandler,           20)
DEFINE_INTERRUPT_HANDLER(FDCAN1_IT1_IRQHandler,           21)
DEFINE_INTERRUPT_HANDLER(FDCAN2_IT1_IRQHandler,           22)
DEFINE_INTERRUPT_HANDLER(EXTI9_5_IRQHandler,              23)
DEFINE_INTERRUPT_HANDLER(TIM1_BRK_IRQHandler,             24)
DEFINE_INTERRUPT_HANDLER(TIM1_UP_IRQHandler,              25)
DEFINE_INTERRUPT_HANDLER(TIM1_TRG_COM_IRQHandler,         26)
DEFINE_INTERRUPT_HANDLER(TIM1_CC_IRQHandler,              27)
DEFINE_INTERRUPT_HANDLER(TIM2_IRQHandler,                 28)
DEFINE_INTERRUPT_HANDLER(TIM3_IRQHandler,                 29)
DEFINE_INTERRUPT_HANDLER(TIM4_IRQHandler,                 30)
DEFINE_INTERRUPT_HANDLER(I2C1_EV_IRQHandler,              31)
DEFINE_INTERRUPT_HANDLER(I2C1_ER_IRQHandler,              32)
DEFINE_INTERRUPT_HANDLER(I2C2_EV_IRQHandler,              33)
DEFINE_INTERRUPT_HANDLER(I2C2_ER_IRQHandler,              34)
DEFINE_INTERRUPT_HANDLER(SPI1_IRQHandler,                 35)
DEFINE_INTERRUPT_HANDLER(SPI2_IRQHandler,                 36)
DEFINE_INTERRUPT_HANDLER(USART1_IRQHandler,               37)
DEFINE_INTERRUPT_HANDLER(USART2_IRQHandler,               38)
DEFINE_INTERRUPT_HANDLER(USART3_IRQHandler,               39)
DEFINE_INTERRUPT_HANDLER(EXTI15_10_IRQHandler,            40)
DEFINE_INTERRUPT_HANDLER(RTC_Alarm_IRQHandler,            41)
/* 42 is Reserved in your startup file */
DEFINE_INTERRUPT_HANDLER(TIM8_BRK_TIM12_IRQHandler,       43)
DEFINE_INTERRUPT_HANDLER(TIM8_UP_TIM13_IRQHandler,        44)
DEFINE_INTERRUPT_HANDLER(TIM8_TRG_COM_TIM14_IRQHandler,   45)
DEFINE_INTERRUPT_HANDLER(TIM8_CC_IRQHandler,              46)
DEFINE_INTERRUPT_HANDLER(DMA1_Stream7_IRQHandler,         47)
DEFINE_INTERRUPT_HANDLER(FMC_IRQHandler,                  48)
DEFINE_INTERRUPT_HANDLER(SDMMC1_IRQHandler,               49)
DEFINE_INTERRUPT_HANDLER(TIM5_IRQHandler,                 50)
DEFINE_INTERRUPT_HANDLER(SPI3_IRQHandler,                 51)
DEFINE_INTERRUPT_HANDLER(UART4_IRQHandler,                52)
DEFINE_INTERRUPT_HANDLER(UART5_IRQHandler,                53)
DEFINE_INTERRUPT_HANDLER(TIM6_DAC_IRQHandler,             54)
DEFINE_INTERRUPT_HANDLER(TIM7_IRQHandler,                 55)
DEFINE_INTERRUPT_HANDLER(DMA2_Stream0_IRQHandler,         56)
DEFINE_INTERRUPT_HANDLER(DMA2_Stream1_IRQHandler,         57)
DEFINE_INTERRUPT_HANDLER(DMA2_Stream2_IRQHandler,         58)
DEFINE_INTERRUPT_HANDLER(DMA2_Stream3_IRQHandler,         59)
DEFINE_INTERRUPT_HANDLER(DMA2_Stream4_IRQHandler,         60)
DEFINE_INTERRUPT_HANDLER(ETH_IRQHandler,                  61)
DEFINE_INTERRUPT_HANDLER(ETH_WKUP_IRQHandler,             62)
DEFINE_INTERRUPT_HANDLER(FDCAN_CAL_IRQHandler,            63)
/* 64-67 are Reserved in your startup file */
DEFINE_INTERRUPT_HANDLER(DMA2_Stream5_IRQHandler,         68)
DEFINE_INTERRUPT_HANDLER(DMA2_Stream6_IRQHandler,         69)
DEFINE_INTERRUPT_HANDLER(DMA2_Stream7_IRQHandler,         70)
DEFINE_INTERRUPT_HANDLER(USART6_IRQHandler,               71)
DEFINE_INTERRUPT_HANDLER(I2C3_EV_IRQHandler,              72)
DEFINE_INTERRUPT_HANDLER(I2C3_ER_IRQHandler,              73)
DEFINE_INTERRUPT_HANDLER(OTG_HS_EP1_OUT_IRQHandler,       74)
DEFINE_INTERRUPT_HANDLER(OTG_HS_EP1_IN_IRQHandler,        75)
DEFINE_INTERRUPT_HANDLER(OTG_HS_WKUP_IRQHandler,          76)
DEFINE_INTERRUPT_HANDLER(OTG_HS_IRQHandler,               77)
DEFINE_INTERRUPT_HANDLER(DCMI_IRQHandler,                 78)
/* 79 is Reserved in your startup file */
DEFINE_INTERRUPT_HANDLER(RNG_IRQHandler,                  80)
DEFINE_INTERRUPT_HANDLER(FPU_IRQHandler,                  81)
DEFINE_INTERRUPT_HANDLER(UART7_IRQHandler,                82)
DEFINE_INTERRUPT_HANDLER(UART8_IRQHandler,                83)
DEFINE_INTERRUPT_HANDLER(SPI4_IRQHandler,                 84)
DEFINE_INTERRUPT_HANDLER(SPI5_IRQHandler,                 85)
DEFINE_INTERRUPT_HANDLER(SPI6_IRQHandler,                 86)
DEFINE_INTERRUPT_HANDLER(SAI1_IRQHandler,                 87)
DEFINE_INTERRUPT_HANDLER(LTDC_IRQHandler,                 88)
DEFINE_INTERRUPT_HANDLER(LTDC_ER_IRQHandler,              89)
DEFINE_INTERRUPT_HANDLER(DMA2D_IRQHandler,                90)
DEFINE_INTERRUPT_HANDLER(SAI2_IRQHandler,                 91)
DEFINE_INTERRUPT_HANDLER(QUADSPI_IRQHandler,              92)
DEFINE_INTERRUPT_HANDLER(LPTIM1_IRQHandler,               93)
DEFINE_INTERRUPT_HANDLER(CEC_IRQHandler,                  94)
DEFINE_INTERRUPT_HANDLER(I2C4_EV_IRQHandler,              95)
DEFINE_INTERRUPT_HANDLER(I2C4_ER_IRQHandler,              96)
DEFINE_INTERRUPT_HANDLER(SPDIF_RX_IRQHandler,             97)
DEFINE_INTERRUPT_HANDLER(OTG_FS_EP1_OUT_IRQHandler,       98)
DEFINE_INTERRUPT_HANDLER(OTG_FS_EP1_IN_IRQHandler,        99)
DEFINE_INTERRUPT_HANDLER(OTG_FS_WKUP_IRQHandler,         100)
DEFINE_INTERRUPT_HANDLER(OTG_FS_IRQHandler,              101)
DEFINE_INTERRUPT_HANDLER(DMAMUX1_OVR_IRQHandler,         102)
DEFINE_INTERRUPT_HANDLER(HRTIM1_Master_IRQHandler,       103)
DEFINE_INTERRUPT_HANDLER(HRTIM1_TIMA_IRQHandler,         104)
DEFINE_INTERRUPT_HANDLER(HRTIM1_TIMB_IRQHandler,         105)
DEFINE_INTERRUPT_HANDLER(HRTIM1_TIMC_IRQHandler,         106)
DEFINE_INTERRUPT_HANDLER(HRTIM1_TIMD_IRQHandler,         107)
DEFINE_INTERRUPT_HANDLER(HRTIM1_TIME_IRQHandler,         108)
DEFINE_INTERRUPT_HANDLER(HRTIM1_FLT_IRQHandler,          109)
DEFINE_INTERRUPT_HANDLER(DFSDM1_FLT0_IRQHandler,         110)
DEFINE_INTERRUPT_HANDLER(DFSDM1_FLT1_IRQHandler,         111)
DEFINE_INTERRUPT_HANDLER(DFSDM1_FLT2_IRQHandler,         112)
DEFINE_INTERRUPT_HANDLER(DFSDM1_FLT3_IRQHandler,         113)
DEFINE_INTERRUPT_HANDLER(SAI3_IRQHandler,                114)
DEFINE_INTERRUPT_HANDLER(SWPMI1_IRQHandler,              115)
DEFINE_INTERRUPT_HANDLER(TIM15_IRQHandler,               116)
DEFINE_INTERRUPT_HANDLER(TIM16_IRQHandler,               117)
DEFINE_INTERRUPT_HANDLER(TIM17_IRQHandler,               118)
DEFINE_INTERRUPT_HANDLER(MDIOS_WKUP_IRQHandler,          119)
DEFINE_INTERRUPT_HANDLER(MDIOS_IRQHandler,               120)
DEFINE_INTERRUPT_HANDLER(JPEG_IRQHandler,                121)
DEFINE_INTERRUPT_HANDLER(MDMA_IRQHandler,                122)
/* 123 is Reserved in your startup file */
DEFINE_INTERRUPT_HANDLER(SDMMC2_IRQHandler,              124)
DEFINE_INTERRUPT_HANDLER(HSEM1_IRQHandler,               125)
/* 126 is Reserved in your startup file */
DEFINE_INTERRUPT_HANDLER(ADC3_IRQHandler,                127)
DEFINE_INTERRUPT_HANDLER(DMAMUX2_OVR_IRQHandler,         128)
DEFINE_INTERRUPT_HANDLER(BDMA_Channel0_IRQHandler,       129)
DEFINE_INTERRUPT_HANDLER(BDMA_Channel1_IRQHandler,       130)
DEFINE_INTERRUPT_HANDLER(BDMA_Channel2_IRQHandler,       131)
DEFINE_INTERRUPT_HANDLER(BDMA_Channel3_IRQHandler,       132)
DEFINE_INTERRUPT_HANDLER(BDMA_Channel4_IRQHandler,       133)
DEFINE_INTERRUPT_HANDLER(BDMA_Channel5_IRQHandler,       134)
DEFINE_INTERRUPT_HANDLER(BDMA_Channel6_IRQHandler,       135)
DEFINE_INTERRUPT_HANDLER(BDMA_Channel7_IRQHandler,       136)
DEFINE_INTERRUPT_HANDLER(COMP1_IRQHandler,               137)
DEFINE_INTERRUPT_HANDLER(LPTIM2_IRQHandler,              138)
DEFINE_INTERRUPT_HANDLER(LPTIM3_IRQHandler,              139)
DEFINE_INTERRUPT_HANDLER(LPTIM4_IRQHandler,              140)
DEFINE_INTERRUPT_HANDLER(LPTIM5_IRQHandler,              141)
DEFINE_INTERRUPT_HANDLER(LPUART1_IRQHandler,             142)
/* 143 is Reserved in your startup file */
DEFINE_INTERRUPT_HANDLER(CRS_IRQHandler,                 144)
DEFINE_INTERRUPT_HANDLER(ECC_IRQHandler,                 145)
DEFINE_INTERRUPT_HANDLER(SAI4_IRQHandler,                146)
/* 147-148 are Reserved in your startup file */
DEFINE_INTERRUPT_HANDLER(WAKEUP_PIN_IRQHandler,          149)