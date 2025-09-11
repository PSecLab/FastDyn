#ifdef TRAIN_USART

#define RCC_BASE     0x40023800
#define GPIOA_BASE   0x40020000
#define USART1_BASE  0x40011000

// RCC registers
#define RCC_AHB1ENR  (*(volatile unsigned int*)(RCC_BASE + 0x30))
#define RCC_APB2ENR  (*(volatile unsigned int*)(RCC_BASE + 0x44))

// GPIOA registers
#define GPIOA_MODER  (*(volatile unsigned int*)(GPIOA_BASE + 0x00))
#define GPIOA_AFRH   (*(volatile unsigned int*)(GPIOA_BASE + 0x24))

// USART1 registers
#define USART1_SR    (*(volatile unsigned int*)(USART1_BASE + 0x00))
#define USART1_DR    (*(volatile unsigned int*)(USART1_BASE + 0x04))
#define USART1_BRR   (*(volatile unsigned int*)(USART1_BASE + 0x08))
#define USART1_CR1   (*(volatile unsigned int*)(USART1_BASE + 0x0C))

void uart_init(void) {
    // Enable GPIOA clock
    RCC_AHB1ENR |= (1 << 0); // GPIOAEN

    // Set PA9/PA10 to AF7 (USART1)
    GPIOA_MODER &= ~((3 << 18) | (3 << 20)); // clear mode
    GPIOA_MODER |= ((2 << 18) | (2 << 20));  // AF mode
    GPIOA_AFRH &= ~((0xF << 4) | (0xF << 8));
    GPIOA_AFRH |= ((7 << 4) | (7 << 8));     // AF7

    // Enable USART1 clock
    RCC_APB2ENR |= (1 << 4); // USART1EN

    // Set baud rate to 115200 (16 MHz PCLK2)
    USART1_BRR = 0x8B;

    // Enable TX, RX, USART
    USART1_CR1 = (1 << 13) | (1 << 3) | (1 << 2); // UE, TE, RE
}

void uart_send_char(char c) {
    while (!(USART1_SR & (1 << 7))); // Wait TXE
    USART1_DR = c;
}

char uart_receive_char(void) {
    while (!(USART1_SR & (1 << 5))); // Wait RXNE
    return USART1_DR;
}

void uart_send_string(const char *str) {
    while (*str) uart_send_char(*str++);
}

int usart_test(void) {
    uart_init();
    uart_send_string("Hello UART at 115200 baud!\r\n");

    while (1) {
        char c = uart_receive_char();
        uart_send_char(c); // Echo
    }
}

#endif /* TRAIN_USART */