/**
 * @file interrupt_handlers.c
 * @brief Provides interrupt handlers for debugging. Each handler enters a loop
 * that loads its ID into R0 and halts via a breakpoint instruction.
 * @warning This register-only method is not safe for nested interrupts.
 * It will deadlock if a higher-priority interrupt preempts a lower-priority one.
 * The breakpoint will be hit repeatedly until R1 is set to the correct value.
 */

/**
 * @brief Defines an interrupt handler with a repeating breakpoint loop.
 * @param handler_name The name of the interrupt handler function.
 * @param exception_num The exception number to use as the ID.
 */
#define DEFINE_INTERRUPT_HANDLER(handler_name, exception_num) \
void handler_name(void) { \
    __asm volatile ( \
        "1:\n\t"                         /* Label for the start of the loop. */ \
        "mov r0, %[ex_num]\n\t"          /* Load our ID into r0 on every iteration. */ \
        "bkpt #0\n\t"                    /* Breakpoint on every iteration. */ \
        "cmp r1, %[ex_num]\n\t"          /* Compare r1 directly with our literal ID. */ \
        "bne 1b\n\t"                     /* If not equal, loop back and break again. */ \
        : /* No output operands */ \
        : [ex_num] "I" (exception_num)    /* Input is the immediate exception number. */ \
        : "r0", "r1", "memory", "cc"     /* Clobbered registers. */ \
    ); \
}

/******************************************************************************/
/* Cortex-M4 Processor Interruption and Exception Handlers          */
/******************************************************************************/
DEFINE_INTERRUPT_HANDLER(NMI_Handler, 2)
DEFINE_INTERRUPT_HANDLER(HardFault_Handler, 3)
DEFINE_INTERRUPT_HANDLER(MemManage_Handler, 4)
DEFINE_INTERRUPT_HANDLER(BusFault_Handler, 5)
DEFINE_INTERRUPT_HANDLER(UsageFault_Handler, 6)
DEFINE_INTERRUPT_HANDLER(SVC_Handler, 11)
DEFINE_INTERRUPT_HANDLER(DebugMon_Handler, 12)
DEFINE_INTERRUPT_HANDLER(PendSV_Handler, 14)
DEFINE_INTERRUPT_HANDLER(SysTick_Handler, 15)

/******************************************************************************/
/* Peripheral Interrupt Handlers                                              */
/******************************************************************************/
DEFINE_INTERRUPT_HANDLER(WWDG_IRQHandler, 16)
DEFINE_INTERRUPT_HANDLER(PVD_IRQHandler, 17)
DEFINE_INTERRUPT_HANDLER(TAMP_STAMP_IRQHandler, 18)
DEFINE_INTERRUPT_HANDLER(RTC_WKUP_IRQHandler, 19)
DEFINE_INTERRUPT_HANDLER(FLASH_IRQHandler, 20)
DEFINE_INTERRUPT_HANDLER(RCC_IRQHandler, 21)
DEFINE_INTERRUPT_HANDLER(EXTI0_IRQHandler, 22)
DEFINE_INTERRUPT_HANDLER(EXTI1_IRQHandler, 23)
DEFINE_INTERRUPT_HANDLER(EXTI2_IRQHandler, 24)
DEFINE_INTERRUPT_HANDLER(EXTI3_IRQHandler, 25)
DEFINE_INTERRUPT_HANDLER(EXTI4_IRQHandler, 26)
DEFINE_INTERRUPT_HANDLER(DMA1_Stream0_IRQHandler, 27)
DEFINE_INTERRUPT_HANDLER(DMA1_Stream1_IRQHandler, 28)
DEFINE_INTERRUPT_HANDLER(DMA1_Stream2_IRQHandler, 29)
DEFINE_INTERRUPT_HANDLER(DMA1_Stream3_IRQHandler, 30)
DEFINE_INTERRUPT_HANDLER(DMA1_Stream4_IRQHandler, 31)
DEFINE_INTERRUPT_HANDLER(DMA1_Stream5_IRQHandler, 32)
DEFINE_INTERRUPT_HANDLER(DMA1_Stream6_IRQHandler, 33)
DEFINE_INTERRUPT_HANDLER(ADC_IRQHandler, 34)
DEFINE_INTERRUPT_HANDLER(CAN1_TX_IRQHandler, 35)
DEFINE_INTERRUPT_HANDLER(CAN1_RX0_IRQHandler, 36)
DEFINE_INTERRUPT_HANDLER(CAN1_RX1_IRQHandler, 37)
DEFINE_INTERRUPT_HANDLER(CAN1_SCE_IRQHandler, 38)
DEFINE_INTERRUPT_HANDLER(EXTI9_5_IRQHandler, 39)
DEFINE_INTERRUPT_HANDLER(TIM1_BRK_TIM9_IRQHandler, 40)
#ifndef TRAIN
DEFINE_INTERRUPT_HANDLER(TIM1_UP_TIM10_IRQHandler, 41)
#endif
DEFINE_INTERRUPT_HANDLER(TIM1_TRG_COM_TIM11_IRQHandler, 42)
DEFINE_INTERRUPT_HANDLER(TIM1_CC_IRQHandler, 43)
DEFINE_INTERRUPT_HANDLER(TIM2_IRQHandler, 44)
DEFINE_INTERRUPT_HANDLER(TIM3_IRQHandler, 45)
DEFINE_INTERRUPT_HANDLER(TIM4_IRQHandler, 46)
DEFINE_INTERRUPT_HANDLER(I2C1_EV_IRQHandler, 47)
DEFINE_INTERRUPT_HANDLER(I2C1_ER_IRQHandler, 48)
DEFINE_INTERRUPT_HANDLER(I2C2_EV_IRQHandler, 49)
DEFINE_INTERRUPT_HANDLER(I2C2_ER_IRQHandler, 50)
DEFINE_INTERRUPT_HANDLER(SPI1_IRQHandler, 51)
DEFINE_INTERRUPT_HANDLER(SPI2_IRQHandler, 52)
DEFINE_INTERRUPT_HANDLER(USART1_IRQHandler, 53)
DEFINE_INTERRUPT_HANDLER(USART2_IRQHandler, 54)
DEFINE_INTERRUPT_HANDLER(USART3_IRQHandler, 55)
DEFINE_INTERRUPT_HANDLER(EXTI15_10_IRQHandler, 56)
DEFINE_INTERRUPT_HANDLER(RTC_Alarm_IRQHandler, 57)
DEFINE_INTERRUPT_HANDLER(OTG_FS_WKUP_IRQHandler, 58)
DEFINE_INTERRUPT_HANDLER(TIM8_BRK_TIM12_IRQHandler, 59)
DEFINE_INTERRUPT_HANDLER(TIM8_UP_TIM13_IRQHandler, 60)
DEFINE_INTERRUPT_HANDLER(TIM8_TRG_COM_TIM14_IRQHandler, 61)
DEFINE_INTERRUPT_HANDLER(TIM8_CC_IRQHandler, 62)
DEFINE_INTERRUPT_HANDLER(DMA1_Stream7_IRQHandler, 63)
DEFINE_INTERRUPT_HANDLER(FMC_IRQHandler, 64)
DEFINE_INTERRUPT_HANDLER(SDIO_IRQHandler, 65)
DEFINE_INTERRUPT_HANDLER(TIM5_IRQHandler, 66)
DEFINE_INTERRUPT_HANDLER(SPI3_IRQHandler, 67)
DEFINE_INTERRUPT_HANDLER(UART4_IRQHandler, 68)
DEFINE_INTERRUPT_HANDLER(UART5_IRQHandler, 69)
DEFINE_INTERRUPT_HANDLER(TIM6_DAC_IRQHandler, 70)
DEFINE_INTERRUPT_HANDLER(TIM7_IRQHandler, 71)
DEFINE_INTERRUPT_HANDLER(DMA2_Stream0_IRQHandler, 72)
DEFINE_INTERRUPT_HANDLER(DMA2_Stream1_IRQHandler, 73)
DEFINE_INTERRUPT_HANDLER(DMA2_Stream2_IRQHandler, 74)
DEFINE_INTERRUPT_HANDLER(DMA2_Stream3_IRQHandler, 75)
DEFINE_INTERRUPT_HANDLER(DMA2_Stream4_IRQHandler, 76)
DEFINE_INTERRUPT_HANDLER(ETH_IRQHandler, 77)
DEFINE_INTERRUPT_HANDLER(ETH_WKUP_IRQHandler, 78)
DEFINE_INTERRUPT_HANDLER(CAN2_TX_IRQHandler, 79)
DEFINE_INTERRUPT_HANDLER(CAN2_RX0_IRQHandler, 80)
DEFINE_INTERRUPT_HANDLER(CAN2_RX1_IRQHandler, 81)
DEFINE_INTERRUPT_HANDLER(CAN2_SCE_IRQHandler, 82)
DEFINE_INTERRUPT_HANDLER(OTG_FS_IRQHandler, 83)
DEFINE_INTERRUPT_HANDLER(DMA2_Stream5_IRQHandler, 84)
DEFINE_INTERRUPT_HANDLER(DMA2_Stream6_IRQHandler, 85)
DEFINE_INTERRUPT_HANDLER(DMA2_Stream7_IRQHandler, 86)
DEFINE_INTERRUPT_HANDLER(USART6_IRQHandler, 87)
DEFINE_INTERRUPT_HANDLER(I2C3_EV_IRQHandler, 88)
DEFINE_INTERRUPT_HANDLER(I2C3_ER_IRQHandler, 89)
DEFINE_INTERRUPT_HANDLER(OTG_HS_EP1_OUT_IRQHandler, 90)
DEFINE_INTERRUPT_HANDLER(OTG_HS_EP1_IN_IRQHandler, 91)
DEFINE_INTERRUPT_HANDLER(OTG_HS_WKUP_IRQHandler, 92)
DEFINE_INTERRUPT_HANDLER(OTG_HS_IRQHandler, 93)
DEFINE_INTERRUPT_HANDLER(DCMI_IRQHandler, 94)
DEFINE_INTERRUPT_HANDLER(HASH_RNG_IRQHandler, 96)
DEFINE_INTERRUPT_HANDLER(FPU_IRQHandler, 97)
DEFINE_INTERRUPT_HANDLER(UART7_IRQHandler, 98)
DEFINE_INTERRUPT_HANDLER(UART8_IRQHandler, 99)
DEFINE_INTERRUPT_HANDLER(SPI4_IRQHandler, 100)
DEFINE_INTERRUPT_HANDLER(SPI5_IRQHandler, 101)
DEFINE_INTERRUPT_HANDLER(SPI6_IRQHandler, 102)
DEFINE_INTERRUPT_HANDLER(SAI1_IRQHandler, 103)
DEFINE_INTERRUPT_HANDLER(LTDC_IRQHandler, 104)
DEFINE_INTERRUPT_HANDLER(LTDC_ER_IRQHandler, 105)
DEFINE_INTERRUPT_HANDLER(DMA2D_IRQHandler, 106)
