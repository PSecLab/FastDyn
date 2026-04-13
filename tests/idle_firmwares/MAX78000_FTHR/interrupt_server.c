/**
 * @file interrupt_server.c
 * @brief Monitor interrupt handlers for MAX78000 passthrough interrupt forwarding.
 *
 * Each handler enters a loop that loads its exception number into R0 and halts
 * via BKPT. The passthrough polling thread reads R0 and calls
 * qemu_plugin_raise_irq(R0 + 16) to inject into QEMU's NVIC.
 *
 * R0 = IRQ number. The passthrough adds +16 to convert to exception number
 * for QEMU's armv7m_nvic_set_pending().
 *
 * @warning This register-only method is not safe for nested interrupts.
 */

#define DEFINE_INTERRUPT_HANDLER(handler_name, irq_num) \
void handler_name(void) { \
    __asm volatile ( \
        "1:\n\t"                         \
        "mov r0, %[id]\n\t"              \
        "bkpt #0\n\t"                    \
        "cmp r1, %[id]\n\t"              \
        "bne 1b\n\t"                     \
        : /* No output operands */       \
        : [id] "I" (irq_num)             \
        : "r0", "r1", "memory", "cc"    \
    ); \
}

/******************************************************************************/
/* Cortex-M4 Processor Exception Handlers                                     */
/* NMI_Handler and SysTick_Handler excluded — defined as strong symbols       */
/* by the Maxim SDK (board.c / system code).                                  */
/******************************************************************************/

/******************************************************************************/
/* MAX78000 Peripheral Interrupt Handlers                                     */
/* R0 = IRQ number. Passthrough does: raise_irq(R0 + 16) = exception number  */
/******************************************************************************/
DEFINE_INTERRUPT_HANDLER(PF_IRQHandler, 0)            /* IRQ 0  */
DEFINE_INTERRUPT_HANDLER(WDT0_IRQHandler, 1)          /* IRQ 1  */
DEFINE_INTERRUPT_HANDLER(RSV02_IRQHandler, 2)         /* IRQ 2  */
DEFINE_INTERRUPT_HANDLER(RTC_IRQHandler, 3)           /* IRQ 3  */
DEFINE_INTERRUPT_HANDLER(TRNG_IRQHandler, 4)          /* IRQ 4  */
DEFINE_INTERRUPT_HANDLER(TMR0_IRQHandler, 5)          /* IRQ 5  */
DEFINE_INTERRUPT_HANDLER(TMR1_IRQHandler, 6)          /* IRQ 6  */
DEFINE_INTERRUPT_HANDLER(TMR2_IRQHandler, 7)          /* IRQ 7  */
DEFINE_INTERRUPT_HANDLER(TMR3_IRQHandler, 8)          /* IRQ 8  */
DEFINE_INTERRUPT_HANDLER(TMR4_IRQHandler, 9)          /* IRQ 9  */
DEFINE_INTERRUPT_HANDLER(TMR5_IRQHandler, 10)         /* IRQ 10 */
DEFINE_INTERRUPT_HANDLER(RSV11_IRQHandler, 11)        /* IRQ 11 */
DEFINE_INTERRUPT_HANDLER(RSV12_IRQHandler, 12)        /* IRQ 12 */
DEFINE_INTERRUPT_HANDLER(I2C0_IRQHandler, 13)         /* IRQ 13 */
DEFINE_INTERRUPT_HANDLER(UART0_IRQHandler, 14)        /* IRQ 14 */
DEFINE_INTERRUPT_HANDLER(UART1_IRQHandler, 15)        /* IRQ 15 */
DEFINE_INTERRUPT_HANDLER(SPI1_IRQHandler, 16)         /* IRQ 16 */
DEFINE_INTERRUPT_HANDLER(RSV17_IRQHandler, 17)        /* IRQ 17 */
DEFINE_INTERRUPT_HANDLER(RSV18_IRQHandler, 18)        /* IRQ 18 */
DEFINE_INTERRUPT_HANDLER(RSV19_IRQHandler, 19)        /* IRQ 19 */
DEFINE_INTERRUPT_HANDLER(ADC_IRQHandler, 20)          /* IRQ 20 */
DEFINE_INTERRUPT_HANDLER(RSV21_IRQHandler, 21)        /* IRQ 21 */
DEFINE_INTERRUPT_HANDLER(RSV22_IRQHandler, 22)        /* IRQ 22 */
DEFINE_INTERRUPT_HANDLER(FLC0_IRQHandler, 23)         /* IRQ 23 */
DEFINE_INTERRUPT_HANDLER(GPIO0_IRQHandler, 24)        /* IRQ 24 — button (P0.2) */
DEFINE_INTERRUPT_HANDLER(GPIO1_IRQHandler, 25)        /* IRQ 25 */
DEFINE_INTERRUPT_HANDLER(GPIO2_IRQHandler, 26)        /* IRQ 26 */
DEFINE_INTERRUPT_HANDLER(RSV27_IRQHandler, 27)        /* IRQ 27 */
DEFINE_INTERRUPT_HANDLER(DMA0_IRQHandler, 28)         /* IRQ 28 */
DEFINE_INTERRUPT_HANDLER(DMA1_IRQHandler, 29)         /* IRQ 29 */
DEFINE_INTERRUPT_HANDLER(DMA2_IRQHandler, 30)         /* IRQ 30 */
DEFINE_INTERRUPT_HANDLER(DMA3_IRQHandler, 31)         /* IRQ 31 */
DEFINE_INTERRUPT_HANDLER(RSV32_IRQHandler, 32)        /* IRQ 32 */
DEFINE_INTERRUPT_HANDLER(RSV33_IRQHandler, 33)        /* IRQ 33 */
DEFINE_INTERRUPT_HANDLER(UART2_IRQHandler, 34)        /* IRQ 34 */
DEFINE_INTERRUPT_HANDLER(RSV35_IRQHandler, 35)        /* IRQ 35 */
DEFINE_INTERRUPT_HANDLER(I2C1_IRQHandler, 36)         /* IRQ 36 */
DEFINE_INTERRUPT_HANDLER(RSV37_IRQHandler, 37)        /* IRQ 37 */
DEFINE_INTERRUPT_HANDLER(RSV38_IRQHandler, 38)        /* IRQ 38 */
DEFINE_INTERRUPT_HANDLER(RSV39_IRQHandler, 39)        /* IRQ 39 */
DEFINE_INTERRUPT_HANDLER(RSV40_IRQHandler, 40)        /* IRQ 40 */
DEFINE_INTERRUPT_HANDLER(RSV41_IRQHandler, 41)        /* IRQ 41 */
DEFINE_INTERRUPT_HANDLER(RSV42_IRQHandler, 42)        /* IRQ 42 */
DEFINE_INTERRUPT_HANDLER(RSV43_IRQHandler, 43)        /* IRQ 43 */
DEFINE_INTERRUPT_HANDLER(RSV44_IRQHandler, 44)        /* IRQ 44 */
DEFINE_INTERRUPT_HANDLER(RSV45_IRQHandler, 45)        /* IRQ 45 */
DEFINE_INTERRUPT_HANDLER(RSV46_IRQHandler, 46)        /* IRQ 46 */
DEFINE_INTERRUPT_HANDLER(RSV47_IRQHandler, 47)        /* IRQ 47 */
DEFINE_INTERRUPT_HANDLER(RSV48_IRQHandler, 48)        /* IRQ 48 */
DEFINE_INTERRUPT_HANDLER(RSV49_IRQHandler, 49)        /* IRQ 49 */
DEFINE_INTERRUPT_HANDLER(RSV50_IRQHandler, 50)        /* IRQ 50 */
DEFINE_INTERRUPT_HANDLER(RSV51_IRQHandler, 51)        /* IRQ 51 */
DEFINE_INTERRUPT_HANDLER(RSV52_IRQHandler, 52)        /* IRQ 52 */
DEFINE_INTERRUPT_HANDLER(WUT_IRQHandler, 53)          /* IRQ 53 */
DEFINE_INTERRUPT_HANDLER(GPIOWAKE_IRQHandler, 54)     /* IRQ 54 */
DEFINE_INTERRUPT_HANDLER(RSV55_IRQHandler, 55)        /* IRQ 55 */
DEFINE_INTERRUPT_HANDLER(SPI0_IRQHandler, 56)         /* IRQ 56 */
DEFINE_INTERRUPT_HANDLER(WDT1_IRQHandler, 57)         /* IRQ 57 */
DEFINE_INTERRUPT_HANDLER(RSV58_IRQHandler, 58)        /* IRQ 58 */
DEFINE_INTERRUPT_HANDLER(PT_IRQHandler, 59)           /* IRQ 59 */
DEFINE_INTERRUPT_HANDLER(RSV60_IRQHandler, 60)        /* IRQ 60 */
DEFINE_INTERRUPT_HANDLER(RSV61_IRQHandler, 61)        /* IRQ 61 */
DEFINE_INTERRUPT_HANDLER(I2C2_IRQHandler, 62)         /* IRQ 62 */
DEFINE_INTERRUPT_HANDLER(RISCV_IRQHandler, 63)        /* IRQ 63 */
DEFINE_INTERRUPT_HANDLER(RSV64_IRQHandler, 64)        /* IRQ 64 */
DEFINE_INTERRUPT_HANDLER(RSV65_IRQHandler, 65)        /* IRQ 65 */
DEFINE_INTERRUPT_HANDLER(RSV66_IRQHandler, 66)        /* IRQ 66 */
DEFINE_INTERRUPT_HANDLER(OWM_IRQHandler, 67)          /* IRQ 67 */
DEFINE_INTERRUPT_HANDLER(RSV68_IRQHandler, 68)        /* IRQ 68 */
DEFINE_INTERRUPT_HANDLER(RSV69_IRQHandler, 69)        /* IRQ 69 */
DEFINE_INTERRUPT_HANDLER(RSV70_IRQHandler, 70)        /* IRQ 70 */
DEFINE_INTERRUPT_HANDLER(RSV71_IRQHandler, 71)        /* IRQ 71 */
DEFINE_INTERRUPT_HANDLER(RSV72_IRQHandler, 72)        /* IRQ 72 */
DEFINE_INTERRUPT_HANDLER(RSV73_IRQHandler, 73)        /* IRQ 73 */
DEFINE_INTERRUPT_HANDLER(RSV74_IRQHandler, 74)        /* IRQ 74 */
DEFINE_INTERRUPT_HANDLER(RSV75_IRQHandler, 75)        /* IRQ 75 */
DEFINE_INTERRUPT_HANDLER(RSV76_IRQHandler, 76)        /* IRQ 76 */
DEFINE_INTERRUPT_HANDLER(RSV77_IRQHandler, 77)        /* IRQ 77 */
DEFINE_INTERRUPT_HANDLER(RSV78_IRQHandler, 78)        /* IRQ 78 */
DEFINE_INTERRUPT_HANDLER(RSV79_IRQHandler, 79)        /* IRQ 79 */
DEFINE_INTERRUPT_HANDLER(RSV80_IRQHandler, 80)        /* IRQ 80 */
DEFINE_INTERRUPT_HANDLER(RSV81_IRQHandler, 81)        /* IRQ 81 */
DEFINE_INTERRUPT_HANDLER(ECC_IRQHandler, 82)          /* IRQ 82 */
DEFINE_INTERRUPT_HANDLER(DVS_IRQHandler, 83)          /* IRQ 83 */
DEFINE_INTERRUPT_HANDLER(SIMO_IRQHandler, 84)         /* IRQ 84 */
DEFINE_INTERRUPT_HANDLER(RSV85_IRQHandler, 85)        /* IRQ 85 */
DEFINE_INTERRUPT_HANDLER(RSV86_IRQHandler, 86)        /* IRQ 86 */
DEFINE_INTERRUPT_HANDLER(RSV87_IRQHandler, 87)        /* IRQ 87 */
DEFINE_INTERRUPT_HANDLER(UART3_IRQHandler, 88)        /* IRQ 88 */
DEFINE_INTERRUPT_HANDLER(RSV89_IRQHandler, 89)        /* IRQ 89 */
DEFINE_INTERRUPT_HANDLER(RSV90_IRQHandler, 90)        /* IRQ 90 */
DEFINE_INTERRUPT_HANDLER(PCIF_IRQHandler, 91)         /* IRQ 91 */
DEFINE_INTERRUPT_HANDLER(RSV92_IRQHandler, 92)        /* IRQ 92 */
DEFINE_INTERRUPT_HANDLER(RSV93_IRQHandler, 93)        /* IRQ 93 */
DEFINE_INTERRUPT_HANDLER(RSV94_IRQHandler, 94)        /* IRQ 94 */
DEFINE_INTERRUPT_HANDLER(RSV95_IRQHandler, 95)        /* IRQ 95 */
DEFINE_INTERRUPT_HANDLER(RSV96_IRQHandler, 96)        /* IRQ 96 */
DEFINE_INTERRUPT_HANDLER(AES_IRQHandler, 97)          /* IRQ 97 */
DEFINE_INTERRUPT_HANDLER(RSV98_IRQHandler, 98)        /* IRQ 98 */
DEFINE_INTERRUPT_HANDLER(I2S_IRQHandler, 99)          /* IRQ 99 */
DEFINE_INTERRUPT_HANDLER(CNN_FIFO_IRQHandler, 100)    /* IRQ 100 */
DEFINE_INTERRUPT_HANDLER(CNN_IRQHandler, 101)         /* IRQ 101 */
DEFINE_INTERRUPT_HANDLER(RSV102_IRQHandler, 102)      /* IRQ 102 */
DEFINE_INTERRUPT_HANDLER(LPCMP_IRQHandler, 103)       /* IRQ 103 */
