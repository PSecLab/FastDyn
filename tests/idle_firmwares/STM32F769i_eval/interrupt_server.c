#include <stdint.h>

/* Macro to define the "Hypercall" Stub (same name as your F1 version) */
#define DEFINE_INTERRUPT_HANDLER(handler_name, id)                              \
__attribute__((naked, used)) void handler_name(void) {                          \
    __asm volatile (                                                            \
        "cpsid i            \n" /* prevent nested IRQs while halted */          \
        "movw r2, %c[val]   \n" /* r2 = ID (constant) */                        \
        "1:                 \n"                                                 \
        "mov  r0, r2        \n" /* r0 = ID (tool reads this) */                 \
        "bkpt #0            \n" /* halt here */                                  \
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
/* STM32F769 Peripheral Interrupts (NVIC IRQ Numbers)                          */
/* These IDs match the order in your startup_stm32f769xx.s vector table.       */
/******************************************************************************/

DEFINE_INTERRUPT_HANDLER(WWDG_IRQHandler,                  0)
DEFINE_INTERRUPT_HANDLER(PVD_IRQHandler,                   1)
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
DEFINE_INTERRUPT_HANDLER(CAN1_TX_IRQHandler,              19)
DEFINE_INTERRUPT_HANDLER(CAN1_RX0_IRQHandler,             20)
DEFINE_INTERRUPT_HANDLER(CAN1_RX1_IRQHandler,             21)
DEFINE_INTERRUPT_HANDLER(CAN1_SCE_IRQHandler,             22)
DEFINE_INTERRUPT_HANDLER(EXTI9_5_IRQHandler,              23)
DEFINE_INTERRUPT_HANDLER(TIM1_BRK_TIM9_IRQHandler,        24)
DEFINE_INTERRUPT_HANDLER(TIM1_UP_TIM10_IRQHandler,        25)
DEFINE_INTERRUPT_HANDLER(TIM1_TRG_COM_TIM11_IRQHandler,   26)
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
DEFINE_INTERRUPT_HANDLER(OTG_FS_WKUP_IRQHandler,          42)
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
DEFINE_INTERRUPT_HANDLER(CAN2_TX_IRQHandler,              63)
DEFINE_INTERRUPT_HANDLER(CAN2_RX0_IRQHandler,             64)
DEFINE_INTERRUPT_HANDLER(CAN2_RX1_IRQHandler,             65)
DEFINE_INTERRUPT_HANDLER(CAN2_SCE_IRQHandler,             66)
DEFINE_INTERRUPT_HANDLER(OTG_FS_IRQHandler,               67)
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
DEFINE_INTERRUPT_HANDLER(DSI_IRQHandler,                  98)
DEFINE_INTERRUPT_HANDLER(DFSDM1_FLT0_IRQHandler,          99)
DEFINE_INTERRUPT_HANDLER(DFSDM1_FLT1_IRQHandler,         100)
DEFINE_INTERRUPT_HANDLER(DFSDM1_FLT2_IRQHandler,         101)
DEFINE_INTERRUPT_HANDLER(DFSDM1_FLT3_IRQHandler,         102)
DEFINE_INTERRUPT_HANDLER(SDMMC2_IRQHandler,              103)
DEFINE_INTERRUPT_HANDLER(CAN3_TX_IRQHandler,             104)
DEFINE_INTERRUPT_HANDLER(CAN3_RX0_IRQHandler,            105)
DEFINE_INTERRUPT_HANDLER(CAN3_RX1_IRQHandler,            106)
DEFINE_INTERRUPT_HANDLER(CAN3_SCE_IRQHandler,            107)
DEFINE_INTERRUPT_HANDLER(JPEG_IRQHandler,                108)
DEFINE_INTERRUPT_HANDLER(MDIOS_IRQHandler,               109)
