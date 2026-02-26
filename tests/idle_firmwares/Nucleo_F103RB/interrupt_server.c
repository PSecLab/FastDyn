/*
 * interrupt_server.c
 * STM32F103 Interrupt Handlers for Passthrough Stub
 */

/* Macro to define the "Hypercall" Stub */
#define DEFINE_INTERRUPT_HANDLER(handler_name, id) \
void handler_name(void) { \
    __asm volatile ( \
        "1:\n\t"                         /* Label for loop start */ \
        "mov r0, %[val]\n\t"             /* Load ID into R0 (Tool reads this) */ \
        "bkpt #0\n\t"                    /* HALT Board. Tool wakes up here. */ \
        "cmp r1, %[val]\n\t"             /* Handshake: Did Tool write ID to R1? */ \
        "bne 1b\n\t"                     /* If not, loop back and wait */ \
        "bx lr\n\t"                      /* Return from ISR */ \
        : /* No output */ \
        : [val] "I" (id)                 /* Input: Immediate ID */ \
        : "r0", "r1", "memory", "cc"     /* Clobbers */ \
    ); \
}

/******************************************************************************/
/* Cortex-M3 Processor Exceptions (IDs 2 - 15)                      */
/******************************************************************************/

/* Note: IDs 2-15 are Exception Numbers.
   QEMU might not support injecting these via simple IRQ lines,
   but they are useful for debugging if the board crashes. */

DEFINE_INTERRUPT_HANDLER(NMI_Handler,           2)
DEFINE_INTERRUPT_HANDLER(HardFault_Handler,     3)
DEFINE_INTERRUPT_HANDLER(MemManage_Handler,     4)
DEFINE_INTERRUPT_HANDLER(BusFault_Handler,      5)
DEFINE_INTERRUPT_HANDLER(UsageFault_Handler,    6)
/* Reserved 7-10 */
DEFINE_INTERRUPT_HANDLER(SVC_Handler,           11)
DEFINE_INTERRUPT_HANDLER(DebugMon_Handler,      12)
/* Reserved 13 */
DEFINE_INTERRUPT_HANDLER(PendSV_Handler,        14)
DEFINE_INTERRUPT_HANDLER(SysTick_Handler,       15)

/******************************************************************************/
/* STM32F103 Peripheral Interrupts (IRQ Numbers 0 - 59)             */
/******************************************************************************/

/* These IDs correspond to the NVIC IRQ numbers expected by QEMU.
   Example: DMA1_Channel1 is IRQ 11. */

DEFINE_INTERRUPT_HANDLER(WWDG_IRQHandler,               0)
DEFINE_INTERRUPT_HANDLER(PVD_IRQHandler,                1)
DEFINE_INTERRUPT_HANDLER(TAMPER_IRQHandler,             2)
DEFINE_INTERRUPT_HANDLER(RTC_IRQHandler,                3)
DEFINE_INTERRUPT_HANDLER(FLASH_IRQHandler,              4)
DEFINE_INTERRUPT_HANDLER(RCC_IRQHandler,                5)
DEFINE_INTERRUPT_HANDLER(EXTI0_IRQHandler,              6)
DEFINE_INTERRUPT_HANDLER(EXTI1_IRQHandler,              7)
DEFINE_INTERRUPT_HANDLER(EXTI2_IRQHandler,              8)
DEFINE_INTERRUPT_HANDLER(EXTI3_IRQHandler,              9)
DEFINE_INTERRUPT_HANDLER(EXTI4_IRQHandler,              10)

/* THE IMPORTANT ONE FOR YOUR DEMO */
DEFINE_INTERRUPT_HANDLER(DMA1_Channel1_IRQHandler,      11)

DEFINE_INTERRUPT_HANDLER(DMA1_Channel2_IRQHandler,      12)
DEFINE_INTERRUPT_HANDLER(DMA1_Channel3_IRQHandler,      13)
DEFINE_INTERRUPT_HANDLER(DMA1_Channel4_IRQHandler,      14)
DEFINE_INTERRUPT_HANDLER(DMA1_Channel5_IRQHandler,      15)
DEFINE_INTERRUPT_HANDLER(DMA1_Channel6_IRQHandler,      16)
DEFINE_INTERRUPT_HANDLER(DMA1_Channel7_IRQHandler,      17)

DEFINE_INTERRUPT_HANDLER(ADC1_2_IRQHandler,             18)
DEFINE_INTERRUPT_HANDLER(USB_HP_CAN1_TX_IRQHandler,     19)
DEFINE_INTERRUPT_HANDLER(USB_LP_CAN1_RX0_IRQHandler,    20)
DEFINE_INTERRUPT_HANDLER(CAN1_RX1_IRQHandler,           21)
DEFINE_INTERRUPT_HANDLER(CAN1_SCE_IRQHandler,           22)
DEFINE_INTERRUPT_HANDLER(EXTI9_5_IRQHandler,            23)
DEFINE_INTERRUPT_HANDLER(TIM1_BRK_TIM9_IRQHandler,      24)
DEFINE_INTERRUPT_HANDLER(TIM1_UP_TIM10_IRQHandler,      25)
DEFINE_INTERRUPT_HANDLER(TIM1_TRG_COM_TIM11_IRQHandler, 26)
DEFINE_INTERRUPT_HANDLER(TIM1_CC_IRQHandler,            27)
DEFINE_INTERRUPT_HANDLER(TIM2_IRQHandler,               28)
DEFINE_INTERRUPT_HANDLER(TIM3_IRQHandler,               29)
DEFINE_INTERRUPT_HANDLER(TIM4_IRQHandler,               30)
DEFINE_INTERRUPT_HANDLER(I2C1_EV_IRQHandler,            31)
DEFINE_INTERRUPT_HANDLER(I2C1_ER_IRQHandler,            32)
DEFINE_INTERRUPT_HANDLER(I2C2_EV_IRQHandler,            33)
DEFINE_INTERRUPT_HANDLER(I2C2_ER_IRQHandler,            34)
DEFINE_INTERRUPT_HANDLER(SPI1_IRQHandler,               35)
DEFINE_INTERRUPT_HANDLER(SPI2_IRQHandler,               36)
DEFINE_INTERRUPT_HANDLER(USART1_IRQHandler,             37)
DEFINE_INTERRUPT_HANDLER(USART2_IRQHandler,             38)
DEFINE_INTERRUPT_HANDLER(USART3_IRQHandler,             39)
DEFINE_INTERRUPT_HANDLER(EXTI15_10_IRQHandler,          40)
DEFINE_INTERRUPT_HANDLER(RTC_Alarm_IRQHandler,          41)
DEFINE_INTERRUPT_HANDLER(USBWakeUp_IRQHandler,          42)
DEFINE_INTERRUPT_HANDLER(TIM8_BRK_TIM12_IRQHandler,     43)
DEFINE_INTERRUPT_HANDLER(TIM8_UP_TIM13_IRQHandler,      44)
DEFINE_INTERRUPT_HANDLER(TIM8_TRG_COM_TIM14_IRQHandler, 45)
DEFINE_INTERRUPT_HANDLER(TIM8_CC_IRQHandler,            46)
DEFINE_INTERRUPT_HANDLER(ADC3_IRQHandler,               47)
DEFINE_INTERRUPT_HANDLER(FSMC_IRQHandler,               48)
DEFINE_INTERRUPT_HANDLER(SDIO_IRQHandler,               49)
DEFINE_INTERRUPT_HANDLER(TIM5_IRQHandler,               50)
DEFINE_INTERRUPT_HANDLER(SPI3_IRQHandler,               51)
DEFINE_INTERRUPT_HANDLER(UART4_IRQHandler,              52)
DEFINE_INTERRUPT_HANDLER(UART5_IRQHandler,              53)
DEFINE_INTERRUPT_HANDLER(TIM6_IRQHandler,               54)
DEFINE_INTERRUPT_HANDLER(TIM7_IRQHandler,               55)
DEFINE_INTERRUPT_HANDLER(DMA2_Channel1_IRQHandler,      56)
DEFINE_INTERRUPT_HANDLER(DMA2_Channel2_IRQHandler,      57)
DEFINE_INTERRUPT_HANDLER(DMA2_Channel3_IRQHandler,      58)
DEFINE_INTERRUPT_HANDLER(DMA2_Channel4_5_IRQHandler,    59)

/******************************************************************************/