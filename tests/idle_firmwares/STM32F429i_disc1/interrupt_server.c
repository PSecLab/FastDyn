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

/*
 * IMPORTANT: These vectors are Cortex-M architectural exceptions, NOT NVIC
 * external IRQs. They have dedicated vector-table slots and must NOT be
 * forwarded to QEMU as external IRQs (libhw would compute R0+16 → wrong
 * vector).
 *
 * Specifically, the firmware-under-emulation calls HAL_Init() which
 * configures SysTick on the real chip via passthrough writes. SysTick then
 * fires every 1ms. If SysTick_Handler did the BKPT-storing trick, the
 * libhw poll thread would saturate on those spurious halts and never catch
 * real peripheral IRQs (USART1, TIMx, etc.).
 *
 * Solution: provide empty no-op handlers for Cortex-M exceptions. They
 * still execute on each fire but return immediately without halting the
 * CPU. HardFault/MemManage/BusFault/UsageFault remain tight infinite loops
 * so genuine MCU crashes during firmware-under-emulation bring-up are still
 * trappable via the debugger.
 *
 * Mirrors the fix applied to tests/idle_firmwares/Nucleo_F103RB/interrupt_server.c.
 */

void NMI_Handler(void)        {}
void HardFault_Handler(void)  { while (1); }
void MemManage_Handler(void)  { while (1); }
void BusFault_Handler(void)   { while (1); }
void UsageFault_Handler(void) { while (1); }
void SVC_Handler(void)        {}
void DebugMon_Handler(void)   {}
void PendSV_Handler(void)     {}
void SysTick_Handler(void)    {}

/******************************************************************************/
/* Peripheral Interrupt Handlers                                              */
/******************************************************************************/
DEFINE_INTERRUPT_HANDLER(WWDG_IRQHandler, 0)
DEFINE_INTERRUPT_HANDLER(PVD_IRQHandler, 1)
DEFINE_INTERRUPT_HANDLER(TAMP_STAMP_IRQHandler, 2)
DEFINE_INTERRUPT_HANDLER(RTC_WKUP_IRQHandler, 3)
DEFINE_INTERRUPT_HANDLER(FLASH_IRQHandler, 4)
DEFINE_INTERRUPT_HANDLER(RCC_IRQHandler, 5)
DEFINE_INTERRUPT_HANDLER(EXTI0_IRQHandler, 6)
DEFINE_INTERRUPT_HANDLER(EXTI1_IRQHandler, 7)
DEFINE_INTERRUPT_HANDLER(EXTI2_IRQHandler, 8)
DEFINE_INTERRUPT_HANDLER(EXTI3_IRQHandler, 9)
DEFINE_INTERRUPT_HANDLER(EXTI4_IRQHandler, 10)
DEFINE_INTERRUPT_HANDLER(DMA1_Stream0_IRQHandler, 11)
DEFINE_INTERRUPT_HANDLER(DMA1_Stream1_IRQHandler, 12)
DEFINE_INTERRUPT_HANDLER(DMA1_Stream2_IRQHandler, 13)
DEFINE_INTERRUPT_HANDLER(DMA1_Stream3_IRQHandler, 14)
DEFINE_INTERRUPT_HANDLER(DMA1_Stream4_IRQHandler, 15)
DEFINE_INTERRUPT_HANDLER(DMA1_Stream5_IRQHandler, 16)
DEFINE_INTERRUPT_HANDLER(DMA1_Stream6_IRQHandler, 17)
DEFINE_INTERRUPT_HANDLER(ADC_IRQHandler, 18)
DEFINE_INTERRUPT_HANDLER(CAN1_TX_IRQHandler, 19)
DEFINE_INTERRUPT_HANDLER(CAN1_RX0_IRQHandler, 20)
DEFINE_INTERRUPT_HANDLER(CAN1_RX1_IRQHandler, 21)
DEFINE_INTERRUPT_HANDLER(CAN1_SCE_IRQHandler, 22)
DEFINE_INTERRUPT_HANDLER(EXTI9_5_IRQHandler, 23)
DEFINE_INTERRUPT_HANDLER(TIM1_BRK_TIM9_IRQHandler, 24)
#ifndef TRAIN_TIMER
DEFINE_INTERRUPT_HANDLER(TIM1_UP_TIM10_IRQHandler, 25)
#endif
DEFINE_INTERRUPT_HANDLER(TIM1_TRG_COM_TIM11_IRQHandler, 26)
DEFINE_INTERRUPT_HANDLER(TIM1_CC_IRQHandler, 27)
DEFINE_INTERRUPT_HANDLER(TIM2_IRQHandler, 28)
DEFINE_INTERRUPT_HANDLER(TIM3_IRQHandler, 29)
DEFINE_INTERRUPT_HANDLER(TIM4_IRQHandler, 30)
DEFINE_INTERRUPT_HANDLER(I2C1_EV_IRQHandler, 31)
DEFINE_INTERRUPT_HANDLER(I2C1_ER_IRQHandler, 32)
DEFINE_INTERRUPT_HANDLER(I2C2_EV_IRQHandler, 33)
DEFINE_INTERRUPT_HANDLER(I2C2_ER_IRQHandler, 34)
DEFINE_INTERRUPT_HANDLER(SPI1_IRQHandler, 35)
DEFINE_INTERRUPT_HANDLER(SPI2_IRQHandler, 36)
DEFINE_INTERRUPT_HANDLER(USART1_IRQHandler, 37)
DEFINE_INTERRUPT_HANDLER(USART2_IRQHandler, 38)
DEFINE_INTERRUPT_HANDLER(USART3_IRQHandler, 39)
DEFINE_INTERRUPT_HANDLER(EXTI15_10_IRQHandler, 40)
DEFINE_INTERRUPT_HANDLER(RTC_Alarm_IRQHandler, 41)
DEFINE_INTERRUPT_HANDLER(OTG_FS_WKUP_IRQHandler, 42)
DEFINE_INTERRUPT_HANDLER(TIM8_BRK_TIM12_IRQHandler, 43)
DEFINE_INTERRUPT_HANDLER(TIM8_UP_TIM13_IRQHandler, 44)
DEFINE_INTERRUPT_HANDLER(TIM8_TRG_COM_TIM14_IRQHandler, 45)
DEFINE_INTERRUPT_HANDLER(TIM8_CC_IRQHandler, 46)
DEFINE_INTERRUPT_HANDLER(DMA1_Stream7_IRQHandler, 47)
DEFINE_INTERRUPT_HANDLER(FMC_IRQHandler, 48)
DEFINE_INTERRUPT_HANDLER(SDIO_IRQHandler, 49)
DEFINE_INTERRUPT_HANDLER(TIM5_IRQHandler, 50)
DEFINE_INTERRUPT_HANDLER(SPI3_IRQHandler, 51)
DEFINE_INTERRUPT_HANDLER(UART4_IRQHandler, 52)
DEFINE_INTERRUPT_HANDLER(UART5_IRQHandler, 53)
DEFINE_INTERRUPT_HANDLER(TIM6_DAC_IRQHandler, 54)
DEFINE_INTERRUPT_HANDLER(TIM7_IRQHandler, 55)
DEFINE_INTERRUPT_HANDLER(DMA2_Stream0_IRQHandler, 56)
DEFINE_INTERRUPT_HANDLER(DMA2_Stream1_IRQHandler, 57)
DEFINE_INTERRUPT_HANDLER(DMA2_Stream2_IRQHandler, 58)
DEFINE_INTERRUPT_HANDLER(DMA2_Stream3_IRQHandler, 59)
DEFINE_INTERRUPT_HANDLER(DMA2_Stream4_IRQHandler, 60)
DEFINE_INTERRUPT_HANDLER(ETH_IRQHandler, 61)
DEFINE_INTERRUPT_HANDLER(ETH_WKUP_IRQHandler, 62)
DEFINE_INTERRUPT_HANDLER(CAN2_TX_IRQHandler, 63)
DEFINE_INTERRUPT_HANDLER(CAN2_RX0_IRQHandler, 64)
DEFINE_INTERRUPT_HANDLER(CAN2_RX1_IRQHandler, 65)
DEFINE_INTERRUPT_HANDLER(CAN2_SCE_IRQHandler, 66)
DEFINE_INTERRUPT_HANDLER(OTG_FS_IRQHandler, 67)
DEFINE_INTERRUPT_HANDLER(DMA2_Stream5_IRQHandler, 68)
DEFINE_INTERRUPT_HANDLER(DMA2_Stream6_IRQHandler, 69)
DEFINE_INTERRUPT_HANDLER(DMA2_Stream7_IRQHandler, 70)
DEFINE_INTERRUPT_HANDLER(USART6_IRQHandler, 71)
DEFINE_INTERRUPT_HANDLER(I2C3_EV_IRQHandler, 72)
DEFINE_INTERRUPT_HANDLER(I2C3_ER_IRQHandler, 73)
DEFINE_INTERRUPT_HANDLER(OTG_HS_EP1_OUT_IRQHandler, 74)
DEFINE_INTERRUPT_HANDLER(OTG_HS_EP1_IN_IRQHandler, 75)
DEFINE_INTERRUPT_HANDLER(OTG_HS_WKUP_IRQHandler, 76)
DEFINE_INTERRUPT_HANDLER(OTG_HS_IRQHandler, 77)
DEFINE_INTERRUPT_HANDLER(DCMI_IRQHandler, 78)
DEFINE_INTERRUPT_HANDLER(HASH_RNG_IRQHandler, 80)
DEFINE_INTERRUPT_HANDLER(FPU_IRQHandler, 81)
DEFINE_INTERRUPT_HANDLER(UART7_IRQHandler, 82)
DEFINE_INTERRUPT_HANDLER(UART8_IRQHandler, 83)
DEFINE_INTERRUPT_HANDLER(SPI4_IRQHandler, 84)
DEFINE_INTERRUPT_HANDLER(SPI5_IRQHandler, 85)
DEFINE_INTERRUPT_HANDLER(SPI6_IRQHandler, 86)
DEFINE_INTERRUPT_HANDLER(SAI1_IRQHandler, 87)
DEFINE_INTERRUPT_HANDLER(LTDC_IRQHandler, 88)
DEFINE_INTERRUPT_HANDLER(LTDC_ER_IRQHandler, 89)
DEFINE_INTERRUPT_HANDLER(DMA2D_IRQHandler, 90)
