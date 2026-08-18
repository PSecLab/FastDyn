/* Bare-metal USART1 helper for STM32F429I-DISC1.
 * PA9 = TX, PA10 = RX, 115200 8N1, PCLK2 = 16 MHz (default HSI post-reset).
 * Extracted from FastDyn tests/idle_firmwares/STM32F429i_disc1/simple_tests/usart_test.c
 * with the TRAIN_USART guards removed.
 */

#define RCC_BASE     0x40023800U
#define GPIOA_BASE   0x40020000U
#define USART1_BASE  0x40011000U

#define RCC_AHB1ENR  (*(volatile unsigned int*)(RCC_BASE + 0x30))
#define RCC_APB2ENR  (*(volatile unsigned int*)(RCC_BASE + 0x44))

#define GPIOA_MODER  (*(volatile unsigned int*)(GPIOA_BASE + 0x00))
#define GPIOA_AFRH   (*(volatile unsigned int*)(GPIOA_BASE + 0x24))

#define USART1_SR    (*(volatile unsigned int*)(USART1_BASE + 0x00))
#define USART1_DR    (*(volatile unsigned int*)(USART1_BASE + 0x04))
#define USART1_BRR   (*(volatile unsigned int*)(USART1_BASE + 0x08))
#define USART1_CR1   (*(volatile unsigned int*)(USART1_BASE + 0x0C))

void uart_send_char(char c) {
    while (!(USART1_SR & (1U << 7))); /* wait TXE */
    USART1_DR = (unsigned int)c;
}

void uart_send_string(const char *str) {
    while (*str) uart_send_char(*str++);
}

void uart_init(void) {
    RCC_AHB1ENR |= (1U << 0);                                /* GPIOA clock */
    GPIOA_MODER &= ~((3U << 18) | (3U << 20));
    GPIOA_MODER |=  ((2U << 18) | (2U << 20));               /* PA9/PA10 → AF */
    GPIOA_AFRH  &= ~((0xFU << 4) | (0xFU << 8));
    GPIOA_AFRH  |=  ((7U << 4) | (7U << 8));                 /* AF7 = USART1 */
    RCC_APB2ENR |= (1U << 4);                                /* USART1 clock */
    USART1_BRR   = 0x8B;                                     /* 115200 @ 16 MHz */
    USART1_CR1   = (1U << 13) | (1U << 3) | (1U << 2);       /* UE, TE, RE */
}
