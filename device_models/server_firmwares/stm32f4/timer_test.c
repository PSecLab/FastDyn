#ifdef TRAIN
/**
 * @file main.c
 * @brief Bare-metal example for TIM1 on STM32F4-Discovery board.
 *
 * This code configures TIM1 to generate an update interrupt every 1 second.
 * The interrupt service routine for TIM1 toggles the green LED (PD12) on the
 * STM32F4-Discovery board.
 *
 * The system clock is assumed to be the default 16MHz HSI.
 * TIM1 is on the APB2 bus, which is also running at 16MHz.
 *
 * Calculation:
 * Timer Clock = 16,000,000 Hz
 * Prescaler (PSC) = 15999
 * Counter Clock = 16,000,000 / (15999 + 1) = 1000 Hz
 * Auto-Reload Register (ARR) = 999
 * Interrupt Frequency = 1000 Hz / (999 + 1) = 1 Hz (1 second)
 */

#include <stdint.h>

// Memory-mapped register addresses for STM32F407VG peripherals

// RCC (Reset and Clock Control)
#define RCC_BASE            0x40023800
#define RCC_AHB1ENR         (*(volatile uint32_t*)(RCC_BASE + 0x30))
#define RCC_APB2ENR         (*(volatile uint32_t*)(RCC_BASE + 0x44))

// GPIOD
#define GPIOD_BASE          0x40020C00
#define GPIOD_MODER         (*(volatile uint32_t*)(GPIOD_BASE + 0x00))
#define GPIOD_ODR           (*(volatile uint32_t*)(GPIOD_BASE + 0x14))

// TIM1 (Advanced-control timer)
#define TIM1_BASE           0x40010000
#define TIM1_CR1            (*(volatile uint32_t*)(TIM1_BASE + 0x00))
#define TIM1_DIER           (*(volatile uint32_t*)(TIM1_BASE + 0x0C))
#define TIM1_SR             (*(volatile uint32_t*)(TIM1_BASE + 0x10))
#define TIM1_PSC            (*(volatile uint32_t*)(TIM1_BASE + 0x28))
#define TIM1_ARR            (*(volatile uint32_t*)(TIM1_BASE + 0x2C))

// NVIC (Nested Vectored Interrupt Controller)
#define NVIC_BASE           0xE000E100
#define NVIC_ISER0          (*(volatile uint32_t*)(NVIC_BASE + 0x00))
#define NVIC_ISER1          (*(volatile uint32_t*)(NVIC_BASE + 0x04))

// Define IRQ numbers for TIM1
#define TIM1_UP_TIM10_IRQn  25

/**
 * @brief  This is the interrupt service routine for TIM1 Update and TIM10 global interrupts.
 * It gets called when the TIM1 counter overflows.
 */
void TIM1_UP_TIM10_IRQHandler(void) {
	  __asm__ volatile ("bkpt #0");
    // Check if the update interrupt flag is set
    if (TIM1_SR & (1 << 0)) {
        // Toggle the green LED (PD12)
        GPIOD_ODR ^= (1 << 12);

        // CRITICAL: Clear the update interrupt flag.
        // If you don't do this, the ISR will be called again immediately after exiting.
        TIM1_SR &= ~(1 << 0);
    }
}

/**
 * @brief  Main program.
 */
int timer_test(void) {
	#if 01
    // 1. Enable the clock for GPIOD
    RCC_AHB1ENR |= (1 << 3); // Set bit 3 to enable GPIOD clock

    // 2. Configure PD12 (Green LED) as a general-purpose output
    GPIOD_MODER &= ~(0b11 << 24); // Clear bits 24 and 25
    GPIOD_MODER |= (0b01 << 24);  // Set bits to 01 for General purpose output mode

    // 3. Enable the clock for TIM1
    RCC_APB2ENR |= (1 << 0); // Set bit 0 to enable TIM1 clock

    // 4. Configure TIM1
    // Set the prescaler to 15999. The counter clock will be 16MHz / 16000 = 1kHz
    TIM1_PSC = 15999;
    // Set the auto-reload register to 999. The counter will overflow every 1000 counts.
    TIM1_ARR = 999;

    // 5. Enable the TIM1 update interrupt
    TIM1_DIER |= (1 << 0); // Set UIE bit to enable update interrupt

#endif 


    // 6. Enable the TIM1 update interrupt in the NVIC
    // The IRQ number for TIM1_UP_TIM10 is 25.
    // This corresponds to bit 25 in the ISER0 register.
    NVIC_ISER0 |= (1 << TIM1_UP_TIM10_IRQn);



    // 7. Enable the timer
    TIM1_CR1 |= (1 << 0); // Set CEN bit to enable the counter

    // Infinite loop. The main work is done in the interrupt handler.
    while (1) {
        // The processor can be put into a low-power mode here if desired.
    }
}
#endif
