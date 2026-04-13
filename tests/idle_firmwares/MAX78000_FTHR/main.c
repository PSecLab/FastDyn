/**
 * @file    main.c
 * @brief   Monitor firmware for MAX78000 passthrough interrupt forwarding.
 *
 * This firmware idles on the real MCU while FastDyn's passthrough backend
 * forwards MMIO accesses to it. When a hardware interrupt fires, the ISR
 * (defined in interrupt_server.c) stores the exception number in R0 and
 * halts via BKPT. The passthrough polling thread detects the halt, reads
 * R0, and injects the interrupt into QEMU's emulated NVIC.
 *
 * SystemInit and Board_Init are overridden with empty stubs to prevent the
 * Maxim SDK from configuring clocks, pins, UART, or enabling interrupts
 * during startup. The passthrough will configure all hardware as needed.
 */

int main(void)
{
    /* Idle forever — all real work happens via passthrough MMIO forwarding
       and interrupt handlers that halt on BKPT. */
    while (1);

    return 0;
}
