/**
 * @file interrupt_handlers.c
 * @brief This file provides the definitions for all interrupt handlers.
 * Each handler loads its processor exception number into R0 for easy debugging,
 * calls a breakpoint instruction, and then waits on a dedicated
 * global variable, which can be changed by a debugger to resume execution.
 * This approach correctly handles nested interrupts.
 */

// An enumeration to provide a unique index for each interrupt handler's flag.
// The order must match the vector table in startup.s
typedef enum {
    NMI_IRQn = -14,
    HardFault_IRQn = -13,
    MemManage_IRQn = -12,
    BusFault_IRQn = -11,
    UsageFault_IRQn = -10,
    SVC_IRQn = -5,
    DebugMon_IRQn = -4,
    PendSV_IRQn = -2,
    SysTick_IRQn = -1,
    WWDG_IRQn = 0,
    PVD_IRQn = 1,
    TAMP_STAMP_IRQn = 2,
    RTC_WKUP_IRQn = 3,
    FLASH_IRQn = 4,
    RCC_IRQn = 5,
    EXTI0_IRQn = 6,
    EXTI1_IRQn = 7,
    EXTI2_IRQn = 8,
    EXTI3_IRQn = 9,
    EXTI4_IRQn = 10,
    DMA1_Stream0_IRQn = 11,
    DMA1_Stream1_IRQn = 12,
    DMA1_Stream2_IRQn = 13,
    DMA1_Stream3_IRQn = 14,
    DMA1_Stream4_IRQn = 15,
    DMA1_Stream5_IRQn = 16,
    DMA1_Stream6_IRQn = 17,
    ADC_IRQn = 18,
    CAN1_TX_IRQn = 19,
    CAN1_RX0_IRQn = 20,
    CAN1_RX1_IRQn = 21,
    CAN1_SCE_IRQn = 22,
    EXTI9_5_IRQn = 23,
    TIM1_BRK_TIM9_IRQn = 24,
    TIM1_UP_TIM10_IRQn = 25,
    TIM1_TRG_COM_TIM11_IRQn = 26,
    TIM1_CC_IRQn = 27,
    TIM2_IRQn = 28,
    TIM3_IRQn = 29,
    TIM4_IRQn = 30,
    I2C1_EV_IRQn = 31,
    I2C1_ER_IRQn = 32,
    I2C2_EV_IRQn = 33,
    I2C2_ER_IRQn = 34,
    SPI1_IRQn = 35,
    SPI2_IRQn = 36,
    USART1_IRQn = 37,
    USART2_IRQn = 38,
    USART3_IRQn = 39,
    EXTI15_10_IRQn = 40,
    RTC_Alarm_IRQn = 41,
    OTG_FS_WKUP_IRQn = 42,
    TIM8_BRK_TIM12_IRQn = 43,
    TIM8_UP_TIM13_IRQn = 44,
    TIM8_TRG_COM_TIM14_IRQn = 45,
    TIM8_CC_IRQn = 46,
    DMA1_Stream7_IRQn = 47,
    FMC_IRQn = 48,
    SDIO_IRQn = 49,
    TIM5_IRQn = 50,
    SPI3_IRQn = 51,
    UART4_IRQn = 52,
    UART5_IRQn = 53,
    TIM6_DAC_IRQn = 54,
    TIM7_IRQn = 55,
    DMA2_Stream0_IRQn = 56,
    DMA2_Stream1_IRQn = 57,
    DMA2_Stream2_IRQn = 58,
    DMA2_Stream3_IRQn = 59,
    DMA2_Stream4_IRQn = 60,
    ETH_IRQn = 61,
    ETH_WKUP_IRQn = 62,
    CAN2_TX_IRQn = 63,
    CAN2_RX0_IRQn = 64,
    CAN2_RX1_IRQn = 65,
    CAN2_SCE_IRQn = 66,
    OTG_FS_IRQn = 67,
    DMA2_Stream5_IRQn = 68,
    DMA2_Stream6_IRQn = 69,
    DMA2_Stream7_IRQn = 70,
    USART6_IRQn = 71,
    I2C3_EV_IRQn = 72,
    I2C3_ER_IRQn = 73,
    OTG_HS_EP1_OUT_IRQn = 74,
    OTG_HS_EP1_IN_IRQn = 75,
    OTG_HS_WKUP_IRQn = 76,
    OTG_HS_IRQn = 77,
    DCMI_IRQn = 78,
    HASH_RNG_IRQn = 80,
    FPU_IRQn = 81,
    UART7_IRQn = 82,
    UART8_IRQn = 83,
    SPI4_IRQn = 84,
    SPI5_IRQn = 85,
    SPI6_IRQn = 86,
    SAI1_IRQn = 87,
    LTDC_IRQn = 88,
    LTDC_ER_IRQn = 89,
    DMA2D_IRQn = 90,
    IRQ_COUNT // Total number of interrupts
} IRQn_Type;


// This global volatile array holds the release flags for each interrupt.
// A debugger can change the value at a specific index to 1 to release that ISR.
volatile int g_release_from_isr[IRQ_COUNT] = {0};

// This is a common way to insert assembly instructions in C code.
// 'bkpt #0' is a breakpoint instruction for ARM Cortex-M processors.
// When the processor executes this, it will halt if a debugger is connected.
#define BKPT_INSTRUCTION __asm("bkpt #0")

/******************************************************************************/
/* Cortex-M4 Processor Exceptions Handlers               */
/******************************************************************************/

// To make the array access work for negative IRQ numbers, we add an offset.
#define IRQ_OFFSET 14

void NMI_Handler(void) {
    __asm("mov r0, #2"); // Exception #2 (NMI_IRQn = -14)
    BKPT_INSTRUCTION;
    while (g_release_from_isr[NMI_IRQn + IRQ_OFFSET] == 0) {}
    g_release_from_isr[NMI_IRQn + IRQ_OFFSET] = 0;
}

void HardFault_Handler(void) {
    __asm("mov r0, #3"); // Exception #3 (HardFault_IRQn = -13)
    BKPT_INSTRUCTION;
    while (g_release_from_isr[HardFault_IRQn + IRQ_OFFSET] == 0) {}
    g_release_from_isr[HardFault_IRQn + IRQ_OFFSET] = 0;
}

void MemManage_Handler(void) {
    __asm("mov r0, #4"); // Exception #4 (MemManage_IRQn = -12)
    BKPT_INSTRUCTION;
    while (g_release_from_isr[MemManage_IRQn + IRQ_OFFSET] == 0) {}
    g_release_from_isr[MemManage_IRQn + IRQ_OFFSET] = 0;
}

void BusFault_Handler(void) {
    __asm("mov r0, #5"); // Exception #5 (BusFault_IRQn = -11)
    BKPT_INSTRUCTION;
    while (g_release_from_isr[BusFault_IRQn + IRQ_OFFSET] == 0) {}
    g_release_from_isr[BusFault_IRQn + IRQ_OFFSET] = 0;
}

void UsageFault_Handler(void) {
    __asm("mov r0, #6"); // Exception #6 (UsageFault_IRQn = -10)
    BKPT_INSTRUCTION;
    while (g_release_from_isr[UsageFault_IRQn + IRQ_OFFSET] == 0) {}
    g_release_from_isr[UsageFault_IRQn + IRQ_OFFSET] = 0;
}

void SVC_Handler(void) {
    __asm("mov r0, #11"); // Exception #11 (SVC_IRQn = -5)
    BKPT_INSTRUCTION;
    while (g_release_from_isr[SVC_IRQn + IRQ_OFFSET] == 0) {}
    g_release_from_isr[SVC_IRQn + IRQ_OFFSET] = 0;
}

void DebugMon_Handler(void) {
    __asm("mov r0, #12"); // Exception #12 (DebugMon_IRQn = -4)
    BKPT_INSTRUCTION;
    while (g_release_from_isr[DebugMon_IRQn + IRQ_OFFSET] == 0) {}
    g_release_from_isr[DebugMon_IRQn + IRQ_OFFSET] = 0;
}

void PendSV_Handler(void) {
    __asm("mov r0, #14"); // Exception #14 (PendSV_IRQn = -2)
    BKPT_INSTRUCTION;
    while (g_release_from_isr[PendSV_IRQn + IRQ_OFFSET] == 0) {}
    g_release_from_isr[PendSV_IRQn + IRQ_OFFSET] = 0;
}

void SysTick_Handler(void) {
    __asm("mov r0, #15"); // Exception #15 (SysTick_IRQn = -1)
    BKPT_INSTRUCTION;
    while (g_release_from_isr[SysTick_IRQn + IRQ_OFFSET] == 0) {}
    g_release_from_isr[SysTick_IRQn + IRQ_OFFSET] = 0;
}

/******************************************************************************/
/* Peripheral Interrupt Handlers                         */
/******************************************************************************/

void WWDG_IRQHandler(void) {
    __asm("mov r0, #16"); // Exception #16 (WWDG_IRQn = 0)
    BKPT_INSTRUCTION;
    while (g_release_from_isr[WWDG_IRQn] == 0) {}
    g_release_from_isr[WWDG_IRQn] = 0;
}

void PVD_IRQHandler(void) {
    __asm("mov r0, #17"); // Exception #17 (PVD_IRQn = 1)
    BKPT_INSTRUCTION;
    while (g_release_from_isr[PVD_IRQn] == 0) {}
    g_release_from_isr[PVD_IRQn] = 0;
}

void TAMP_STAMP_IRQHandler(void) {
    __asm("mov r0, #18"); // Exception #18 (TAMP_STAMP_IRQn = 2)
    BKPT_INSTRUCTION;
    while (g_release_from_isr[TAMP_STAMP_IRQn] == 0) {}
    g_release_from_isr[TAMP_STAMP_IRQn] = 0;
}

void RTC_WKUP_IRQHandler(void) {
    __asm("mov r0, #19"); // Exception #19 (RTC_WKUP_IRQn = 3)
    BKPT_INSTRUCTION;
    while (g_release_from_isr[RTC_WKUP_IRQn] == 0) {}
    g_release_from_isr[RTC_WKUP_IRQn] = 0;
}

void FLASH_IRQHandler(void) {
    __asm("mov r0, #20"); // Exception #20 (FLASH_IRQn = 4)
    BKPT_INSTRUCTION;
    while (g_release_from_isr[FLASH_IRQn] == 0) {}
    g_release_from_isr[FLASH_IRQn] = 0;
}

void RCC_IRQHandler(void) {
    __asm("mov r0, #21"); // Exception #21 (RCC_IRQn = 5)
    BKPT_INSTRUCTION;
    while (g_release_from_isr[RCC_IRQn] == 0) {}
    g_release_from_isr[RCC_IRQn] = 0;
}

void EXTI0_IRQHandler(void) {
    __asm("mov r0, #22"); // Exception #22 (EXTI0_IRQn = 6)
    BKPT_INSTRUCTION;
    while (g_release_from_isr[EXTI0_IRQn] == 0) {}
    g_release_from_isr[EXTI0_IRQn] = 0;
}

void EXTI1_IRQHandler(void) {
    __asm("mov r0, #23"); // Exception #23 (EXTI1_IRQn = 7)
    BKPT_INSTRUCTION;
    while (g_release_from_isr[EXTI1_IRQn] == 0) {}
    g_release_from_isr[EXTI1_IRQn] = 0;
}

void EXTI2_IRQHandler(void) {
    __asm("mov r0, #24"); // Exception #24 (EXTI2_IRQn = 8)
    BKPT_INSTRUCTION;
    while (g_release_from_isr[EXTI2_IRQn] == 0) {}
    g_release_from_isr[EXTI2_IRQn] = 0;
}

void EXTI3_IRQHandler(void) {
    __asm("mov r0, #25"); // Exception #25 (EXTI3_IRQn = 9)
    BKPT_INSTRUCTION;
    while (g_release_from_isr[EXTI3_IRQn] == 0) {}
    g_release_from_isr[EXTI3_IRQn] = 0;
}

void EXTI4_IRQHandler(void) {
    __asm("mov r0, #26"); // Exception #26 (EXTI4_IRQn = 10)
    BKPT_INSTRUCTION;
    while (g_release_from_isr[EXTI4_IRQn] == 0) {}
    g_release_from_isr[EXTI4_IRQn] = 0;
}

void DMA1_Stream0_IRQHandler(void) {
    __asm("mov r0, #27"); // Exception #27 (DMA1_Stream0_IRQn = 11)
    BKPT_INSTRUCTION;
    while (g_release_from_isr[DMA1_Stream0_IRQn] == 0) {}
    g_release_from_isr[DMA1_Stream0_IRQn] = 0;
}

void DMA1_Stream1_IRQHandler(void) {
    __asm("mov r0, #28"); // Exception #28 (DMA1_Stream1_IRQn = 12)
    BKPT_INSTRUCTION;
    while (g_release_from_isr[DMA1_Stream1_IRQn] == 0) {}
    g_release_from_isr[DMA1_Stream1_IRQn] = 0;
}

void DMA1_Stream2_IRQHandler(void) {
    __asm("mov r0, #29"); // Exception #29 (DMA1_Stream2_IRQn = 13)
    BKPT_INSTRUCTION;
    while (g_release_from_isr[DMA1_Stream2_IRQn] == 0) {}
    g_release_from_isr[DMA1_Stream2_IRQn] = 0;
}

void DMA1_Stream3_IRQHandler(void) {
    __asm("mov r0, #30"); // Exception #30 (DMA1_Stream3_IRQn = 14)
    BKPT_INSTRUCTION;
    while (g_release_from_isr[DMA1_Stream3_IRQn] == 0) {}
    g_release_from_isr[DMA1_Stream3_IRQn] = 0;
}

void DMA1_Stream4_IRQHandler(void) {
    __asm("mov r0, #31"); // Exception #31 (DMA1_Stream4_IRQn = 15)
    BKPT_INSTRUCTION;
    while (g_release_from_isr[DMA1_Stream4_IRQn] == 0) {}
    g_release_from_isr[DMA1_Stream4_IRQn] = 0;
}

void DMA1_Stream5_IRQHandler(void) {
    __asm("mov r0, #32"); // Exception #32 (DMA1_Stream5_IRQn = 16)
    BKPT_INSTRUCTION;
    while (g_release_from_isr[DMA1_Stream5_IRQn] == 0) {}
    g_release_from_isr[DMA1_Stream5_IRQn] = 0;
}

void DMA1_Stream6_IRQHandler(void) {
    __asm("mov r0, #33"); // Exception #33 (DMA1_Stream6_IRQn = 17)
    BKPT_INSTRUCTION;
    while (g_release_from_isr[DMA1_Stream6_IRQn] == 0) {}
    g_release_from_isr[DMA1_Stream6_IRQn] = 0;
}

void ADC_IRQHandler(void) {
    __asm("mov r0, #34"); // Exception #34 (ADC_IRQn = 18)
    BKPT_INSTRUCTION;
    while (g_release_from_isr[ADC_IRQn] == 0) {}
    g_release_from_isr[ADC_IRQn] = 0;
}

void CAN1_TX_IRQHandler(void) {
    __asm("mov r0, #35"); // Exception #35 (CAN1_TX_IRQn = 19)
    BKPT_INSTRUCTION;
    while (g_release_from_isr[CAN1_TX_IRQn] == 0) {}
    g_release_from_isr[CAN1_TX_IRQn] = 0;
}

void CAN1_RX0_IRQHandler(void) {
    __asm("mov r0, #36"); // Exception #36 (CAN1_RX0_IRQn = 20)
    BKPT_INSTRUCTION;
    while (g_release_from_isr[CAN1_RX0_IRQn] == 0) {}
    g_release_from_isr[CAN1_RX0_IRQn] = 0;
}

void CAN1_RX1_IRQHandler(void) {
    __asm("mov r0, #37"); // Exception #37 (CAN1_RX1_IRQn = 21)
    BKPT_INSTRUCTION;
    while (g_release_from_isr[CAN1_RX1_IRQn] == 0) {}
    g_release_from_isr[CAN1_RX1_IRQn] = 0;
}

void CAN1_SCE_IRQHandler(void) {
    __asm("mov r0, #38"); // Exception #38 (CAN1_SCE_IRQn = 22)
    BKPT_INSTRUCTION;
    while (g_release_from_isr[CAN1_SCE_IRQn] == 0) {}
    g_release_from_isr[CAN1_SCE_IRQn] = 0;
}

void EXTI9_5_IRQHandler(void) {
    __asm("mov r0, #39"); // Exception #39 (EXTI9_5_IRQn = 23)
    BKPT_INSTRUCTION;
    while (g_release_from_isr[EXTI9_5_IRQn] == 0) {}
    g_release_from_isr[EXTI9_5_IRQn] = 0;
}

void TIM1_BRK_TIM9_IRQHandler(void) {
    __asm("mov r0, #40"); // Exception #40 (TIM1_BRK_TIM9_IRQn = 24)
    BKPT_INSTRUCTION;
    while (g_release_from_isr[TIM1_BRK_TIM9_IRQn] == 0) {}
    g_release_from_isr[TIM1_BRK_TIM9_IRQn] = 0;
}

#ifndef TRAIN
void TIM1_UP_TIM10_IRQHandler(void) {
    __asm("mov r0, #41"); // Exception #41 (TIM1_UP_TIM10_IRQn = 25)
    BKPT_INSTRUCTION;
    while (g_release_from_isr[TIM1_UP_TIM10_IRQn] == 0) {}
    g_release_from_isr[TIM1_UP_TIM10_IRQn] = 0;
}
#endif

void TIM1_TRG_COM_TIM11_IRQHandler(void) {
    __asm("mov r0, #42"); // Exception #42 (TIM1_TRG_COM_TIM11_IRQn = 26)
    BKPT_INSTRUCTION;
    while (g_release_from_isr[TIM1_TRG_COM_TIM11_IRQn] == 0) {}
    g_release_from_isr[TIM1_TRG_COM_TIM11_IRQn] = 0;
}

void TIM1_CC_IRQHandler(void) {
    __asm("mov r0, #43"); // Exception #43 (TIM1_CC_IRQn = 27)
    BKPT_INSTRUCTION;
    while (g_release_from_isr[TIM1_CC_IRQn] == 0) {}
    g_release_from_isr[TIM1_CC_IRQn] = 0;
}

void TIM2_IRQHandler(void) {
    __asm("mov r0, #44"); // Exception #44 (TIM2_IRQn = 28)
    BKPT_INSTRUCTION;
    while (g_release_from_isr[TIM2_IRQn] == 0) {}
    g_release_from_isr[TIM2_IRQn] = 0;
}

void TIM3_IRQHandler(void) {
    __asm("mov r0, #45"); // Exception #45 (TIM3_IRQn = 29)
    BKPT_INSTRUCTION;
    while (g_release_from_isr[TIM3_IRQn] == 0) {}
    g_release_from_isr[TIM3_IRQn] = 0;
}

void TIM4_IRQHandler(void) {
    __asm("mov r0, #46"); // Exception #46 (TIM4_IRQn = 30)
    BKPT_INSTRUCTION;
    while (g_release_from_isr[TIM4_IRQn] == 0) {}
    g_release_from_isr[TIM4_IRQn] = 0;
}

void I2C1_EV_IRQHandler(void) {
    __asm("mov r0, #47"); // Exception #47 (I2C1_EV_IRQn = 31)
    BKPT_INSTRUCTION;
    while (g_release_from_isr[I2C1_EV_IRQn] == 0) {}
    g_release_from_isr[I2C1_EV_IRQn] = 0;
}

void I2C1_ER_IRQHandler(void) {
    __asm("mov r0, #48"); // Exception #48 (I2C1_ER_IRQn = 32)
    BKPT_INSTRUCTION;
    while (g_release_from_isr[I2C1_ER_IRQn] == 0) {}
    g_release_from_isr[I2C1_ER_IRQn] = 0;
}

void I2C2_EV_IRQHandler(void) {
    __asm("mov r0, #49"); // Exception #49 (I2C2_EV_IRQn = 33)
    BKPT_INSTRUCTION;
    while (g_release_from_isr[I2C2_EV_IRQn] == 0) {}
    g_release_from_isr[I2C2_EV_IRQn] = 0;
}

void I2C2_ER_IRQHandler(void) {
    __asm("mov r0, #50"); // Exception #50 (I2C2_ER_IRQn = 34)
    BKPT_INSTRUCTION;
    while (g_release_from_isr[I2C2_ER_IRQn] == 0) {}
    g_release_from_isr[I2C2_ER_IRQn] = 0;
}

void SPI1_IRQHandler(void) {
    __asm("mov r0, #51"); // Exception #51 (SPI1_IRQn = 35)
    BKPT_INSTRUCTION;
    while (g_release_from_isr[SPI1_IRQn] == 0) {}
    g_release_from_isr[SPI1_IRQn] = 0;
}

void SPI2_IRQHandler(void) {
    __asm("mov r0, #52"); // Exception #52 (SPI2_IRQn = 36)
    BKPT_INSTRUCTION;
    while (g_release_from_isr[SPI2_IRQn] == 0) {}
    g_release_from_isr[SPI2_IRQn] = 0;
}

void USART1_IRQHandler(void) {
    __asm("mov r0, #53"); // Exception #53 (USART1_IRQn = 37)
    BKPT_INSTRUCTION;
    while (g_release_from_isr[USART1_IRQn] == 0) {}
    g_release_from_isr[USART1_IRQn] = 0;
}

void USART2_IRQHandler(void) {
    __asm("mov r0, #54"); // Exception #54 (USART2_IRQn = 38)
    BKPT_INSTRUCTION;
    while (g_release_from_isr[USART2_IRQn] == 0) {}
    g_release_from_isr[USART2_IRQn] = 0;
}

void USART3_IRQHandler(void) {
    __asm("mov r0, #55"); // Exception #55 (USART3_IRQn = 39)
    BKPT_INSTRUCTION;
    while (g_release_from_isr[USART3_IRQn] == 0) {}
    g_release_from_isr[USART3_IRQn] = 0;
}

void EXTI15_10_IRQHandler(void) {
    __asm("mov r0, #56"); // Exception #56 (EXTI15_10_IRQn = 40)
    BKPT_INSTRUCTION;
    while (g_release_from_isr[EXTI15_10_IRQn] == 0) {}
    g_release_from_isr[EXTI15_10_IRQn] = 0;
}

void RTC_Alarm_IRQHandler(void) {
    __asm("mov r0, #57"); // Exception #57 (RTC_Alarm_IRQn = 41)
    BKPT_INSTRUCTION;
    while (g_release_from_isr[RTC_Alarm_IRQn] == 0) {}
    g_release_from_isr[RTC_Alarm_IRQn] = 0;
}

void OTG_FS_WKUP_IRQHandler(void) {
    __asm("mov r0, #58"); // Exception #58 (OTG_FS_WKUP_IRQn = 42)
    BKPT_INSTRUCTION;
    while (g_release_from_isr[OTG_FS_WKUP_IRQn] == 0) {}
    g_release_from_isr[OTG_FS_WKUP_IRQn] = 0;
}

void TIM8_BRK_TIM12_IRQHandler(void) {
    __asm("mov r0, #59"); // Exception #59 (TIM8_BRK_TIM12_IRQn = 43)
    BKPT_INSTRUCTION;
    while (g_release_from_isr[TIM8_BRK_TIM12_IRQn] == 0) {}
    g_release_from_isr[TIM8_BRK_TIM12_IRQn] = 0;
}

void TIM8_UP_TIM13_IRQHandler(void) {
    __asm("mov r0, #60"); // Exception #60 (TIM8_UP_TIM13_IRQn = 44)
    BKPT_INSTRUCTION;
    while (g_release_from_isr[TIM8_UP_TIM13_IRQn] == 0) {}
    g_release_from_isr[TIM8_UP_TIM13_IRQn] = 0;
}

void TIM8_TRG_COM_TIM14_IRQHandler(void) {
    __asm("mov r0, #61"); // Exception #61 (TIM8_TRG_COM_TIM14_IRQn = 45)
    BKPT_INSTRUCTION;
    while (g_release_from_isr[TIM8_TRG_COM_TIM14_IRQn] == 0) {}
    g_release_from_isr[TIM8_TRG_COM_TIM14_IRQn] = 0;
}

void TIM8_CC_IRQHandler(void) {
    __asm("mov r0, #62"); // Exception #62 (TIM8_CC_IRQn = 46)
    BKPT_INSTRUCTION;
    while (g_release_from_isr[TIM8_CC_IRQn] == 0) {}
    g_release_from_isr[TIM8_CC_IRQn] = 0;
}

void DMA1_Stream7_IRQHandler(void) {
    __asm("mov r0, #63"); // Exception #63 (DMA1_Stream7_IRQn = 47)
    BKPT_INSTRUCTION;
    while (g_release_from_isr[DMA1_Stream7_IRQn] == 0) {}
    g_release_from_isr[DMA1_Stream7_IRQn] = 0;
}

void FMC_IRQHandler(void) {
    __asm("mov r0, #64"); // Exception #64 (FMC_IRQn = 48)
    BKPT_INSTRUCTION;
    while (g_release_from_isr[FMC_IRQn] == 0) {}
    g_release_from_isr[FMC_IRQn] = 0;
}

void SDIO_IRQHandler(void) {
    __asm("mov r0, #65"); // Exception #65 (SDIO_IRQn = 49)
    BKPT_INSTRUCTION;
    while (g_release_from_isr[SDIO_IRQn] == 0) {}
    g_release_from_isr[SDIO_IRQn] = 0;
}

void TIM5_IRQHandler(void) {
    __asm("mov r0, #66"); // Exception #66 (TIM5_IRQn = 50)
    BKPT_INSTRUCTION;
    while (g_release_from_isr[TIM5_IRQn] == 0) {}
    g_release_from_isr[TIM5_IRQn] = 0;
}

void SPI3_IRQHandler(void) {
    __asm("mov r0, #67"); // Exception #67 (SPI3_IRQn = 51)
    BKPT_INSTRUCTION;
    while (g_release_from_isr[SPI3_IRQn] == 0) {}
    g_release_from_isr[SPI3_IRQn] = 0;
}

void UART4_IRQHandler(void) {
    __asm("mov r0, #68"); // Exception #68 (UART4_IRQn = 52)
    BKPT_INSTRUCTION;
    while (g_release_from_isr[UART4_IRQn] == 0) {}
    g_release_from_isr[UART4_IRQn] = 0;
}

void UART5_IRQHandler(void) {
    __asm("mov r0, #69"); // Exception #69 (UART5_IRQn = 53)
    BKPT_INSTRUCTION;
    while (g_release_from_isr[UART5_IRQn] == 0) {}
    g_release_from_isr[UART5_IRQn] = 0;
}

void TIM6_DAC_IRQHandler(void) {
    __asm("mov r0, #70"); // Exception #70 (TIM6_DAC_IRQn = 54)
    BKPT_INSTRUCTION;
    while (g_release_from_isr[TIM6_DAC_IRQn] == 0) {}
    g_release_from_isr[TIM6_DAC_IRQn] = 0;
}

void TIM7_IRQHandler(void) {
    __asm("mov r0, #71"); // Exception #71 (TIM7_IRQn = 55)
    BKPT_INSTRUCTION;
    while (g_release_from_isr[TIM7_IRQn] == 0) {}
    g_release_from_isr[TIM7_IRQn] = 0;
}

void DMA2_Stream0_IRQHandler(void) {
    __asm("mov r0, #72"); // Exception #72 (DMA2_Stream0_IRQn = 56)
    BKPT_INSTRUCTION;
    while (g_release_from_isr[DMA2_Stream0_IRQn] == 0) {}
    g_release_from_isr[DMA2_Stream0_IRQn] = 0;
}

void DMA2_Stream1_IRQHandler(void) {
    __asm("mov r0, #73"); // Exception #73 (DMA2_Stream1_IRQn = 57)
    BKPT_INSTRUCTION;
    while (g_release_from_isr[DMA2_Stream1_IRQn] == 0) {}
    g_release_from_isr[DMA2_Stream1_IRQn] = 0;
}

void DMA2_Stream2_IRQHandler(void) {
    __asm("mov r0, #74"); // Exception #74 (DMA2_Stream2_IRQn = 58)
    BKPT_INSTRUCTION;
    while (g_release_from_isr[DMA2_Stream2_IRQn] == 0) {}
    g_release_from_isr[DMA2_Stream2_IRQn] = 0;
}

void DMA2_Stream3_IRQHandler(void) {
    __asm("mov r0, #75"); // Exception #75 (DMA2_Stream3_IRQn = 59)
    BKPT_INSTRUCTION;
    while (g_release_from_isr[DMA2_Stream3_IRQn] == 0) {}
    g_release_from_isr[DMA2_Stream3_IRQn] = 0;
}

void DMA2_Stream4_IRQHandler(void) {
    __asm("mov r0, #76"); // Exception #76 (DMA2_Stream4_IRQn = 60)
    BKPT_INSTRUCTION;
    while (g_release_from_isr[DMA2_Stream4_IRQn] == 0) {}
    g_release_from_isr[DMA2_Stream4_IRQn] = 0;
}

void ETH_IRQHandler(void) {
    __asm("mov r0, #77"); // Exception #77 (ETH_IRQn = 61)
    BKPT_INSTRUCTION;
    while (g_release_from_isr[ETH_IRQn] == 0) {}
    g_release_from_isr[ETH_IRQn] = 0;
}

void ETH_WKUP_IRQHandler(void) {
    __asm("mov r0, #78"); // Exception #78 (ETH_WKUP_IRQn = 62)
    BKPT_INSTRUCTION;
    while (g_release_from_isr[ETH_WKUP_IRQn] == 0) {}
    g_release_from_isr[ETH_WKUP_IRQn] = 0;
}

void CAN2_TX_IRQHandler(void) {
    __asm("mov r0, #79"); // Exception #79 (CAN2_TX_IRQn = 63)
    BKPT_INSTRUCTION;
    while (g_release_from_isr[CAN2_TX_IRQn] == 0) {}
    g_release_from_isr[CAN2_TX_IRQn] = 0;
}

void CAN2_RX0_IRQHandler(void) {
    __asm("mov r0, #80"); // Exception #80 (CAN2_RX0_IRQn = 64)
    BKPT_INSTRUCTION;
    while (g_release_from_isr[CAN2_RX0_IRQn] == 0) {}
    g_release_from_isr[CAN2_RX0_IRQn] = 0;
}

void CAN2_RX1_IRQHandler(void) {
    __asm("mov r0, #81"); // Exception #81 (CAN2_RX1_IRQn = 65)
    BKPT_INSTRUCTION;
    while (g_release_from_isr[CAN2_RX1_IRQn] == 0) {}
    g_release_from_isr[CAN2_RX1_IRQn] = 0;
}

void CAN2_SCE_IRQHandler(void) {
    __asm("mov r0, #82"); // Exception #82 (CAN2_SCE_IRQn = 66)
    BKPT_INSTRUCTION;
    while (g_release_from_isr[CAN2_SCE_IRQn] == 0) {}
    g_release_from_isr[CAN2_SCE_IRQn] = 0;
}

void OTG_FS_IRQHandler(void) {
    __asm("mov r0, #83"); // Exception #83 (OTG_FS_IRQn = 67)
    BKPT_INSTRUCTION;
    while (g_release_from_isr[OTG_FS_IRQn] == 0) {}
    g_release_from_isr[OTG_FS_IRQn] = 0;
}

void DMA2_Stream5_IRQHandler(void) {
    __asm("mov r0, #84"); // Exception #84 (DMA2_Stream5_IRQn = 68)
    BKPT_INSTRUCTION;
    while (g_release_from_isr[DMA2_Stream5_IRQn] == 0) {}
    g_release_from_isr[DMA2_Stream5_IRQn] = 0;
}

void DMA2_Stream6_IRQHandler(void) {
    __asm("mov r0, #85"); // Exception #85 (DMA2_Stream6_IRQn = 69)
    BKPT_INSTRUCTION;
    while (g_release_from_isr[DMA2_Stream6_IRQn] == 0) {}
    g_release_from_isr[DMA2_Stream6_IRQn] = 0;
}

void DMA2_Stream7_IRQHandler(void) {
    __asm("mov r0, #86"); // Exception #86 (DMA2_Stream7_IRQn = 70)
    BKPT_INSTRUCTION;
    while (g_release_from_isr[DMA2_Stream7_IRQn] == 0) {}
    g_release_from_isr[DMA2_Stream7_IRQn] = 0;
}

void USART6_IRQHandler(void) {
    __asm("mov r0, #87"); // Exception #87 (USART6_IRQn = 71)
    BKPT_INSTRUCTION;
    while (g_release_from_isr[USART6_IRQn] == 0) {}
    g_release_from_isr[USART6_IRQn] = 0;
}

void I2C3_EV_IRQHandler(void) {
    __asm("mov r0, #88"); // Exception #88 (I2C3_EV_IRQn = 72)
    BKPT_INSTRUCTION;
    while (g_release_from_isr[I2C3_EV_IRQn] == 0) {}
    g_release_from_isr[I2C3_EV_IRQn] = 0;
}

void I2C3_ER_IRQHandler(void) {
    __asm("mov r0, #89"); // Exception #89 (I2C3_ER_IRQn = 73)
    BKPT_INSTRUCTION;
    while (g_release_from_isr[I2C3_ER_IRQn] == 0) {}
    g_release_from_isr[I2C3_ER_IRQn] = 0;
}

void OTG_HS_EP1_OUT_IRQHandler(void) {
    __asm("mov r0, #90"); // Exception #90 (OTG_HS_EP1_OUT_IRQn = 74)
    BKPT_INSTRUCTION;
    while (g_release_from_isr[OTG_HS_EP1_OUT_IRQn] == 0) {}
    g_release_from_isr[OTG_HS_EP1_OUT_IRQn] = 0;
}

void OTG_HS_EP1_IN_IRQHandler(void) {
    __asm("mov r0, #91"); // Exception #91 (OTG_HS_EP1_IN_IRQn = 75)
    BKPT_INSTRUCTION;
    while (g_release_from_isr[OTG_HS_EP1_IN_IRQn] == 0) {}
    g_release_from_isr[OTG_HS_EP1_IN_IRQn] = 0;
}

void OTG_HS_WKUP_IRQHandler(void) {
    __asm("mov r0, #92"); // Exception #92 (OTG_HS_WKUP_IRQn = 76)
    BKPT_INSTRUCTION;
    while (g_release_from_isr[OTG_HS_WKUP_IRQn] == 0) {}
    g_release_from_isr[OTG_HS_WKUP_IRQn] = 0;
}

void OTG_HS_IRQHandler(void) {
    __asm("mov r0, #93"); // Exception #93 (OTG_HS_IRQn = 77)
    BKPT_INSTRUCTION;
    while (g_release_from_isr[OTG_HS_IRQn] == 0) {}
    g_release_from_isr[OTG_HS_IRQn] = 0;
}

void DCMI_IRQHandler(void) {
    __asm("mov r0, #94"); // Exception #94 (DCMI_IRQn = 78)
    BKPT_INSTRUCTION;
    while (g_release_from_isr[DCMI_IRQn] == 0) {}
    g_release_from_isr[DCMI_IRQn] = 0;
}

void HASH_RNG_IRQHandler(void) {
    __asm("mov r0, #96"); // Exception #96 (HASH_RNG_IRQn = 80)
    BKPT_INSTRUCTION;
    while (g_release_from_isr[HASH_RNG_IRQn] == 0) {}
    g_release_from_isr[HASH_RNG_IRQn] = 0;
}

void FPU_IRQHandler(void) {
    __asm("mov r0, #97"); // Exception #97 (FPU_IRQn = 81)
    BKPT_INSTRUCTION;
    while (g_release_from_isr[FPU_IRQn] == 0) {}
    g_release_from_isr[FPU_IRQn] = 0;
}

void UART7_IRQHandler(void) {
    __asm("mov r0, #98"); // Exception #98 (UART7_IRQn = 82)
    BKPT_INSTRUCTION;
    while (g_release_from_isr[UART7_IRQn] == 0) {}
    g_release_from_isr[UART7_IRQn] = 0;
}

void UART8_IRQHandler(void) {
    __asm("mov r0, #99"); // Exception #99 (UART8_IRQn = 83)
    BKPT_INSTRUCTION;
    while (g_release_from_isr[UART8_IRQn] == 0) {}
    g_release_from_isr[UART8_IRQn] = 0;
}

void SPI4_IRQHandler(void) {
    __asm("mov r0, #100"); // Exception #100 (SPI4_IRQn = 84)
    BKPT_INSTRUCTION;
    while (g_release_from_isr[SPI4_IRQn] == 0) {}
    g_release_from_isr[SPI4_IRQn] = 0;
}

void SPI5_IRQHandler(void) {
    __asm("mov r0, #101"); // Exception #101 (SPI5_IRQn = 85)
    BKPT_INSTRUCTION;
    while (g_release_from_isr[SPI5_IRQn] == 0) {}
    g_release_from_isr[SPI5_IRQn] = 0;
}

void SPI6_IRQHandler(void) {
    __asm("mov r0, #102"); // Exception #102 (SPI6_IRQn = 86)
    BKPT_INSTRUCTION;
    while (g_release_from_isr[SPI6_IRQn] == 0) {}
    g_release_from_isr[SPI6_IRQn] = 0;
}

void SAI1_IRQHandler(void) {
    __asm("mov r0, #103"); // Exception #103 (SAI1_IRQn = 87)
    BKPT_INSTRUCTION;
    while (g_release_from_isr[SAI1_IRQn] == 0) {}
    g_release_from_isr[SAI1_IRQn] = 0;
}

void LTDC_IRQHandler(void) {
    __asm("mov r0, #104"); // Exception #104 (LTDC_IRQn = 88)
    BKPT_INSTRUCTION;
    while (g_release_from_isr[LTDC_IRQn] == 0) {}
    g_release_from_isr[LTDC_IRQn] = 0;
}

void LTDC_ER_IRQHandler(void) {
    __asm("mov r0, #105"); // Exception #105 (LTDC_ER_IRQn = 89)
    BKPT_INSTRUCTION;
    while (g_release_from_isr[LTDC_ER_IRQn] == 0) {}
    g_release_from_isr[LTDC_ER_IRQn] = 0;
}

void DMA2D_IRQHandler(void) {
    __asm("mov r0, #106"); // Exception #106 (DMA2D_IRQn = 90)
    BKPT_INSTRUCTION;
    while (g_release_from_isr[DMA2D_IRQn] == 0) {}
    g_release_from_isr[DMA2D_IRQn] = 0;
}
