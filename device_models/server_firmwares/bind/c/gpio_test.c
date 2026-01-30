// bare_gpio_blink_all.c
// Blink all LEDs on STM32F429I-DISC1 without CMSIS/HAL.
#ifdef TRAIN_GPIO
#define PERIPH_BASE     0x40000000UL
#define AHB1PERIPH_BASE (PERIPH_BASE + 0x00020000UL)
#define RCC_BASE        (AHB1PERIPH_BASE + 0x3800UL)
#define GPIOG_BASE      (AHB1PERIPH_BASE + 0x1800UL)

// RCC register
#define RCC_AHB1ENR     (*(volatile unsigned int *)(RCC_BASE + 0x30))

// GPIOG registers
#define GPIOG_MODER     (*(volatile unsigned int *)(GPIOG_BASE + 0x00))
#define GPIOG_ODR       (*(volatile unsigned int *)(GPIOG_BASE + 0x14))

// LED pins
#define LED_BLUE   12
#define LED_GREEN  13
#define LED_ORANGE 14
#define LED_RED    15

static void delay(volatile unsigned int count) {
    while (count--);
}

int gpio_test(void) {
    // 1. Enable clock for GPIOG
    RCC_AHB1ENR |= (1 << 6); // GPIOG enable

    // 2. Set PG12, PG13, PG14, PG15 as output
    GPIOG_MODER &= ~((0x3 << (LED_BLUE*2)) |
                     (0x3 << (LED_GREEN*2)) |
                     (0x3 << (LED_ORANGE*2)) |
                     (0x3 << (LED_RED*2)));
    GPIOG_MODER |=  ((0x1 << (LED_BLUE*2)) |
                     (0x1 << (LED_GREEN*2)) |
                     (0x1 << (LED_ORANGE*2)) |
                     (0x1 << (LED_RED*2)));

    // 3. Blink loop
    for (int i =0; i < 1000000; i++) {
        // Toggle all LEDs
        GPIOG_ODR ^= (1 << LED_BLUE) | (1 << LED_GREEN) |
                     (1 << LED_ORANGE) | (1 << LED_RED);
    }

	while(1<2);
}
#endif /* TRAIN_GPIO */
