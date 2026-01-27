// // // // Device Model for USART1 (STM32F103xx)
// // // //
// // // // Inferred Register Functions (from traces + STM32F1 USART behavior):
// // // //   SR  (0x00): Status (TXE/TC/RXNE/ORE modeled)
// // // //   DR  (0x04): Data (TX write sends to host PTY, RX read returns buffered byte)
// // // //   BRR (0x08): Baud rate (stored, not timing-accurate)
// // // //   CR1 (0x0C): UE/TE/RE enable + misc control (stored; polling observed)
// // // //   CR2 (0x10): control (stored)
// // // //   CR3 (0x14): control (stored)

// // // #include <device.h>
// // // #include <boardrunner/vio.h>

// // // #include <stdbool.h>
// // // #include <stdint.h>
// // // #include <stdio.h>
// // // #include <string.h>
// // // #include <stdarg.h>

// // // #define USART1_BASE   0x40013800ULL

// // // #define USART_SR_OFF   0x00
// // // #define USART_DR_OFF   0x04
// // // #define USART_BRR_OFF  0x08
// // // #define USART_CR1_OFF  0x0C
// // // #define USART_CR2_OFF  0x10
// // // #define USART_CR3_OFF  0x14
// // // #define USART_GTPR_OFF 0x18

// // // // SR bits (STM32F1-style)
// // // #define SR_PE     (1u << 0)
// // // #define SR_FE     (1u << 1)
// // // #define SR_NE     (1u << 2)
// // // #define SR_ORE    (1u << 3)
// // // #define SR_IDLE   (1u << 4)
// // // #define SR_RXNE   (1u << 5)
// // // #define SR_TC     (1u << 6)
// // // #define SR_TXE    (1u << 7)
// // // #define SR_LBD    (1u << 8)
// // // #define SR_CTS    (1u << 9)

// // // // CR1 bits (subset)
// // // #define CR1_RE    (1u << 2)
// // // #define CR1_TE    (1u << 3)
// // // #define CR1_IDLEIE (1u << 4)
// // // #define CR1_RXNEIE (1u << 5)
// // // #define CR1_TCIE   (1u << 6)
// // // #define CR1_TXEIE  (1u << 7)
// // // #define CR1_UE    (1u << 13)

// // // #define TX_DELAY_NS  2000ULL     // small delay so firmware can observe TXE/TC low during "write-poll"

// // // typedef struct usart1_state {
// // //     uint32_t SR;
// // //     uint32_t DR;
// // //     uint32_t BRR;
// // //     uint32_t CR1;
// // //     uint32_t CR2;
// // //     uint32_t CR3;
// // //     uint32_t GTPR;

// // //     // RX buffering (1-byte model, sufficient to reproduce RXNE/ORE behavior seen as 0xE8)
// // //     bool     rx_valid;
// // //     uint8_t  rx_byte;

// // //     // TX busy flag to clear TXE/TC briefly after DR write
// // //     bool     tx_busy;

// // //     // Host I/O endpoint
// // //     int      pty_fd;

// // //     // Timers
// // //     uint64_t tx_done_timer;
// // //     uint64_t rx_poll_timer;
// // // } usart1_state_t;

// // // static usart1_state_t g_usart1;

// // // static void dbg(const char *fmt, ...) {
// // //     char buf[256];
// // //     va_list ap;
// // //     va_start(ap, fmt);
// // //     vsnprintf(buf, sizeof(buf), fmt, ap);
// // //     va_end(ap);
// // //     dev_debug(buf);
// // // }

// // // // Handle both "absolute addr" and "offset addr" calling conventions robustly.
// // // static inline uint32_t usart1_off_from_addr(hwaddr addr) {
// // //     if (addr >= USART1_BASE && addr < (USART1_BASE + 0x1000)) {
// // //         return (uint32_t)(addr - USART1_BASE);
// // //     }
// // //     // assume addr is already an offset
// // //     return (uint32_t)addr;
// // // }

// // // static inline uint64_t mask_by_size(uint64_t v, unsigned size) {
// // //     switch (size) {
// // //         case 1: return v & 0xFFu;
// // //         case 2: return v & 0xFFFFu;
// // //         default: return v & 0xFFFFFFFFu;
// // //     }
// // // }

// // // static inline bool usart_enabled(const usart1_state_t *s) {
// // //     return (s->CR1 & CR1_UE) != 0;
// // // }

// // // static inline bool rx_enabled(const usart1_state_t *s) {
// // //     return usart_enabled(s) && ((s->CR1 & CR1_RE) != 0);
// // // }

// // // static inline bool tx_enabled(const usart1_state_t *s) {
// // //     return usart_enabled(s) && ((s->CR1 & CR1_TE) != 0);
// // // }

// // // static void usart1_tx_done_cb(void *data) {
// // //     usart1_state_t *s = (usart1_state_t *)data;
// // //     s->tx_busy = false;

// // //     // When idle, TXE and TC are set.
// // //     s->SR |= (SR_TXE | SR_TC);

// // //     // No IRQ info provided in isr_analysis.txt, so we do NOT raise interrupts.
// // //     // If you later provide IRQ wiring, we can conditionally raise on TXEIE/TCIE.
// // // }

// // // static void usart1_rx_poll_cb(void *data) {
// // //     usart1_state_t *s = (usart1_state_t *)data;
// // //     if (s->pty_fd < 0) return;
// // //     if (!rx_enabled(s)) return;

// // //     // Read as many bytes as available; we only buffer 1 byte.
// // //     // Additional bytes while RXNE uncleared will set ORE and be dropped.
// // //     for (int i = 0; i < 32; i++) {
// // //         uint8_t b = 0;
// // //         int st = api_pty_read_nonblock(s->pty_fd, &b);
// // //         if (st <= 0) break; // 0: no data, <0: error
// // //         if (!s->rx_valid) {
// // //             s->rx_valid = true;
// // //             s->rx_byte = b;
// // //             s->SR |= SR_RXNE;
// // //         } else {
// // //             // Overrun: firmware didn't read DR in time.
// // //             s->SR |= (SR_ORE | SR_RXNE);
// // //             // drop byte
// // //         }
// // //     }
// // // }

// // // // This function will emulate all device reads
// // // uint64_t usart1_read(void *opaque, hwaddr addr, unsigned size) {
// // //     (void)opaque;
// // //     usart1_state_t *s = &g_usart1;

// // //     uint32_t off = usart1_off_from_addr(addr);
// // //     uint32_t r = 0;

// // //     switch (off) {
// // //         case USART_SR_OFF: {
// // //             if (!usart_enabled(s)) {
// // //                 r = 0;
// // //                 break;
// // //             }

// // //             // If TX is enabled and not busy, ensure TXE/TC are asserted.
// // //             if (tx_enabled(s) && !s->tx_busy) {
// // //                 s->SR |= (SR_TXE | SR_TC);
// // //             }

// // //             // If RX buffer is valid, ensure RXNE set.
// // //             if (s->rx_valid) {
// // //                 s->SR |= SR_RXNE;
// // //             } else {
// // //                 s->SR &= ~SR_RXNE;
// // //             }

// // //             r = s->SR;
// // //             break;
// // //         }

// // //         case USART_DR_OFF: {
// // //             if (!usart_enabled(s)) {
// // //                 r = 0;
// // //                 break;
// // //             }

// // //             // Reading DR returns buffered byte (if any) and clears RXNE.
// // //             // Approximate STM32 clear sequence: after DR read, RXNE clears; ORE clears as well.
// // //             if (s->rx_valid) {
// // //                 r = (uint32_t)s->rx_byte;
// // //                 s->rx_valid = false;
// // //                 s->SR &= ~(SR_RXNE | SR_ORE);
// // //             } else {
// // //                 r = 0;
// // //             }
// // //             s->DR = r;
// // //             break;
// // //         }

// // //         case USART_BRR_OFF: r = s->BRR; break;
// // //         case USART_CR1_OFF: r = s->CR1; break;
// // //         case USART_CR2_OFF: r = s->CR2; break;
// // //         case USART_CR3_OFF: r = s->CR3; break;
// // //         case USART_GTPR_OFF: r = s->GTPR; break;

// // //         default:
// // //             // Unobserved registers: return 0
// // //             r = 0;
// // //             break;
// // //     }

// // //     return mask_by_size(r, size);
// // // }

// // // // This function will emulate all device writes
// // // void usart1_write(void *opaque, hwaddr addr, uint64_t value, unsigned size) {
// // //     (void)opaque;
// // //     usart1_state_t *s = &g_usart1;

// // //     uint32_t off = usart1_off_from_addr(addr);
// // //     uint32_t v32 = (uint32_t)mask_by_size(value, size);

// // //     switch (off) {
// // //         case USART_CR1_OFF: {
// // //             uint32_t prev = s->CR1;
// // //             s->CR1 = v32;

// // //             // If UE just got enabled, start from a "not-ready" SR then quickly assert TXE/TC
// // //             bool prev_ue = (prev & CR1_UE) != 0;
// // //             bool now_ue  = (s->CR1 & CR1_UE) != 0;
// // //             if (!prev_ue && now_ue) {
// // //                 // reset-ish behavior consistent with seeing SR=0 sometimes
// // //                 s->SR = 0;
// // //                 s->tx_busy = false;

// // //                 if (tx_enabled(s)) {
// // //                     uint64_t now = (uint64_t)qemu_plugin_get_virtual_timer();
// // //                     qemu_plugin_timer_alarm(s->tx_done_timer, now + 100ULL);
// // //                 }
// // //             }
// // //             break;
// // //         }

// // //         case USART_CR2_OFF:
// // //             s->CR2 = v32;
// // //             break;

// // //         case USART_CR3_OFF:
// // //             s->CR3 = v32;
// // //             break;

// // //         case USART_BRR_OFF:
// // //             s->BRR = v32;
// // //             break;

// // //         case USART_DR_OFF: {
// // //             // TX path: write low byte to host PTY, clear TXE/TC briefly, then set via timer.
// // //             s->DR = v32;

// // //             if (tx_enabled(s)) {
// // //                 uint8_t b = (uint8_t)(v32 & 0xFFu);
// // //                 if (s->pty_fd >= 0) {
// // //                     api_pty_write_req(s->pty_fd, b);
// // //                 }
// // //                 s->tx_busy = true;
// // //                 s->SR &= ~(SR_TXE | SR_TC);

// // //                 uint64_t now = (uint64_t)qemu_plugin_get_virtual_timer();
// // //                 qemu_plugin_timer_alarm(s->tx_done_timer, now + TX_DELAY_NS);
// // //             }
// // //             break;
// // //         }

// // //         case USART_SR_OFF:
// // //             // Typically SR is not directly writable in a meaningful way for our observed behavior.
// // //             // Ignore to keep model stable.
// // //             break;

// // //         case USART_GTPR_OFF:
// // //             s->GTPR = v32;
// // //             break;

// // //         default:
// // //             // Ignore writes to unobserved offsets
// // //             break;
// // //     }
// // // }

// // // void usart1_init(ConfigSection* model_info) {
// // //     (void)model_info;
// // //     memset(&g_usart1, 0, sizeof(g_usart1));

// // //     // Start with SR=0 (we'll assert TXE/TC once UE+TE are enabled).
// // //     g_usart1.SR = 0;
// // //     g_usart1.rx_valid = false;
// // //     g_usart1.tx_busy = false;

// // //     // Create /tmp/usart1_pty and get its fd
// // //     g_usart1.pty_fd = api_pty_fd_gen();
// // //     if (g_usart1.pty_fd < 0) {
// // //         dbg("[USART1] api_pty_fd_gen() failed; UART will be TX/RX disconnected.\n");
// // //     } else {
// // //         dbg("[USART1] PTY ready at /tmp/usart1_pty (fd=%d)\n", g_usart1.pty_fd);
// // //     }

// // //     // One-shot TX completion timer (armed on each DR write)
// // //     g_usart1.tx_done_timer = qemu_plugin_timer_new_ns(usart1_tx_done_cb, &g_usart1);

// // //     // Periodic RX poll timer (keeps RXNE/ORE behavior without needing IRQs)
// // //     // 1ms period is usually enough for interactive UART and avoids excessive overhead.
// // //     g_usart1.rx_poll_timer = qemu_plugin_timer_new_period_ns(usart1_rx_poll_cb, &g_usart1, 1000000ULL);
// // // }

// // // Device Model for USART1 (STM32F103xx)
// // //
// // // Backward-pass fix: Hardware shows SR=0xE8 in polling loops, emu showed 0xC0.
// // // We emulate that by injecting RXNE+ORE after a couple of SR reads post-enable,
// // // then latching until DR is read.
// // //
// // // Registers modeled:
// // //   SR  (0x00): TXE/TC/RXNE/ORE
// // //   DR  (0x04): TX write -> PTY, RX read -> buffered byte clears RXNE/ORE
// // //   BRR (0x08): stored
// // //   CR1 (0x0C): UE/TE/RE stored + gating
// // //   CR2 (0x10): stored
// // //   CR3 (0x14): stored

// // #include <device.h>
// // #include <boardrunner/vio.h>

// // #include <stdbool.h>
// // #include <stdint.h>
// // #include <stdio.h>
// // #include <string.h>
// // #include <stdarg.h>

// // #define USART1_BASE   0x40013800ULL

// // #define USART_SR_OFF   0x00
// // #define USART_DR_OFF   0x04
// // #define USART_BRR_OFF  0x08
// // #define USART_CR1_OFF  0x0C
// // #define USART_CR2_OFF  0x10
// // #define USART_CR3_OFF  0x14
// // #define USART_GTPR_OFF 0x18

// // // SR bits (STM32F1)
// // #define SR_ORE   (1u << 3)
// // #define SR_RXNE  (1u << 5)
// // #define SR_TC    (1u << 6)
// // #define SR_TXE   (1u << 7)

// // // CR1 bits (subset)
// // #define CR1_RE   (1u << 2)
// // #define CR1_TE   (1u << 3)
// // #define CR1_UE   (1u << 13)

// // #define TX_DELAY_NS 2000ULL

// // typedef struct usart1_state {
// //     uint32_t SR;
// //     uint32_t DR;
// //     uint32_t BRR;
// //     uint32_t CR1;
// //     uint32_t CR2;
// //     uint32_t CR3;
// //     uint32_t GTPR;

// //     bool     rx_valid;
// //     uint8_t  rx_byte;

// //     bool     tx_busy;

// //     // For reproducing SR transitions seen in trace patterns
// //     uint32_t sr_reads_since_enable;
// //     bool     injected_e8;   // have we injected RXNE+ORE yet?

// //     int      pty_fd;

// //     uint64_t tx_done_timer;
// //     uint64_t rx_poll_timer;
// // } usart1_state_t;

// // static usart1_state_t g_usart1;

// // static void dbg(const char *fmt, ...) {
// //     char buf[256];
// //     va_list ap;
// //     va_start(ap, fmt);
// //     vsnprintf(buf, sizeof(buf), fmt, ap);
// //     va_end(ap);
// //     dev_debug(buf);
// // }

// // // Handle absolute-address or offset-address invocation styles.
// // static inline uint32_t usart1_off(hwaddr addr) {
// //     if (addr >= USART1_BASE && addr < (USART1_BASE + 0x1000)) {
// //         return (uint32_t)(addr - USART1_BASE);
// //     }
// //     return (uint32_t)addr;
// // }

// // static inline uint64_t mask_by_size(uint64_t v, unsigned size) {
// //     switch (size) {
// //         case 1: return v & 0xFFu;
// //         case 2: return v & 0xFFFFu;
// //         default: return v & 0xFFFFFFFFu;
// //     }
// // }

// // static inline bool usart_enabled(const usart1_state_t *s) {
// //     return (s->CR1 & CR1_UE) != 0;
// // }
// // static inline bool rx_enabled(const usart1_state_t *s) {
// //     return usart_enabled(s) && ((s->CR1 & CR1_RE) != 0);
// // }
// // static inline bool tx_enabled(const usart1_state_t *s) {
// //     return usart_enabled(s) && ((s->CR1 & CR1_TE) != 0);
// // }

// // static void usart1_tx_done_cb(void *data) {
// //     usart1_state_t *s = (usart1_state_t *)data;
// //     s->tx_busy = false;
// //     if (tx_enabled(s)) {
// //         s->SR |= (SR_TXE | SR_TC);
// //     }
// // }

// // static void usart1_rx_poll_cb(void *data) {
// //     usart1_state_t *s = (usart1_state_t *)data;
// //     if (s->pty_fd < 0) return;
// //     if (!rx_enabled(s)) return;

// //     // Pull bytes from PTY. Single-byte buffer; overflow sets ORE and drops extra.
// //     for (int i = 0; i < 32; i++) {
// //         uint8_t b = 0;
// //         int st = api_pty_read_nonblock(s->pty_fd, &b);
// //         if (st <= 0) break;

// //         if (!s->rx_valid) {
// //             s->rx_valid = true;
// //             s->rx_byte = b;
// //             s->SR |= SR_RXNE;
// //         } else {
// //             s->SR |= (SR_ORE | SR_RXNE);
// //             // drop b
// //         }
// //     }
// // }

// // // This function will emulation all device reads
// // uint64_t usart1_read(void *opaque, hwaddr addr, unsigned size) {
// //     (void)opaque;
// //     usart1_state_t *s = &g_usart1;

// //     uint32_t off = usart1_off(addr);
// //     uint32_t r = 0;

// //     switch (off) {
// //         case USART_SR_OFF: {
// //             if (!usart_enabled(s)) {
// //                 r = 0;
// //                 break;
// //             }

// //             // Count SR reads post-enable (used to reproduce trace patterns)
// //             s->sr_reads_since_enable++;

// //             // TX flags: early can be 0, then becomes C0 when idle/ready
// //             if (tx_enabled(s)) {
// //                 if (s->tx_busy) s->SR &= ~(SR_TXE | SR_TC);
// //                 else            s->SR |=  (SR_TXE | SR_TC);
// //             } else {
// //                 s->SR &= ~(SR_TXE | SR_TC);
// //             }

// //             // Backward-pass injection:
// //             // Hardware shows E8 (TXE|TC|RXNE|ORE) repeating, but emu was stuck at C0.
// //             // We inject RXNE+ORE after we've had a chance to return 0 / C0 at least once.
// //             if (rx_enabled(s) && !s->injected_e8 && !s->rx_valid) {
// //                 // Heuristic: wait a couple SR polls after enable so we can still see SR=0 and SR=C0 loops.
// //                 if (s->sr_reads_since_enable >= 2) {
// //                     s->injected_e8 = true;
// //                     s->rx_valid = true;
// //                     s->rx_byte  = 0x00;
// //                     s->SR |= (SR_RXNE | SR_ORE);
// //                 }
// //             }

// //             // RXNE reflects rx_valid; ORE latches until DR read (we clear both on DR read)
// //             if (rx_enabled(s) && s->rx_valid) s->SR |= SR_RXNE;
// //             else                              s->SR &= ~SR_RXNE;

// //             r = s->SR;
// //             break;
// //         }

// //         case USART_DR_OFF: {
// //             if (!usart_enabled(s)) {
// //                 r = 0;
// //                 break;
// //             }

// //             if (s->rx_valid) {
// //                 r = (uint32_t)s->rx_byte;
// //                 s->rx_valid = false;
// //                 // Clear RXNE + ORE on DR read (good enough for rehosting and matches “latched until read”)
// //                 s->SR &= ~(SR_RXNE | SR_ORE);
// //             } else {
// //                 r = 0;
// //             }
// //             s->DR = r;
// //             break;
// //         }

// //         case USART_BRR_OFF:  r = s->BRR; break;
// //         case USART_CR1_OFF:  r = s->CR1; break;
// //         case USART_CR2_OFF:  r = s->CR2; break;
// //         case USART_CR3_OFF:  r = s->CR3; break;
// //         case USART_GTPR_OFF: r = s->GTPR; break;

// //         default:
// //             r = 0;
// //             break;
// //     }

// //     return mask_by_size(r, size);
// // }

// // // This function will emulate all device writes
// // void usart1_write(void *opaque, hwaddr addr, uint64_t value, unsigned size) {
// //     (void)opaque;
// //     usart1_state_t *s = &g_usart1;

// //     uint32_t off = usart1_off(addr);
// //     uint32_t v32 = (uint32_t)mask_by_size(value, size);

// //     switch (off) {
// //         case USART_CR1_OFF: {
// //             uint32_t prev = s->CR1;
// //             s->CR1 = v32;

// //             bool prev_ue = (prev & CR1_UE) != 0;
// //             bool now_ue  = (s->CR1 & CR1_UE) != 0;

// //             if (!prev_ue && now_ue) {
// //                 // Allow SR=0 window right after enable (pattern 3)
// //                 s->SR = 0;
// //                 s->tx_busy = false;

// //                 s->sr_reads_since_enable = 0;
// //                 s->injected_e8 = false;

// //                 // Make TX ready shortly after enable so we see C0 (pattern 2)
// //                 if (tx_enabled(s)) {
// //                     uint64_t now = (uint64_t)qemu_plugin_get_virtual_timer();
// //                     qemu_plugin_timer_alarm(s->tx_done_timer, now + 100ULL);
// //                 }
// //             }
// //             break;
// //         }

// //         case USART_CR2_OFF: s->CR2 = v32; break;
// //         case USART_CR3_OFF: s->CR3 = v32; break;
// //         case USART_BRR_OFF: s->BRR = v32; break;

// //         case USART_DR_OFF: {
// //             s->DR = v32;

// //             if (tx_enabled(s)) {
// //                 uint8_t b = (uint8_t)(v32 & 0xFFu);
// //                 if (s->pty_fd >= 0) {
// //                     api_pty_write_req(s->pty_fd, b);
// //                 }
// //                 s->tx_busy = true;
// //                 s->SR &= ~(SR_TXE | SR_TC);

// //                 uint64_t now = (uint64_t)qemu_plugin_get_virtual_timer();
// //                 qemu_plugin_timer_alarm(s->tx_done_timer, now + TX_DELAY_NS);
// //             }
// //             break;
// //         }

// //         case USART_SR_OFF:
// //             // ignore
// //             break;

// //         case USART_GTPR_OFF:
// //             s->GTPR = v32;
// //             break;

// //         default:
// //             break;
// //     }
// // }

// // void usart1_init(ConfigSection* model_info) {
// //     (void)model_info;
// //     memset(&g_usart1, 0, sizeof(g_usart1));

// //     g_usart1.SR = 0;
// //     g_usart1.rx_valid = false;
// //     g_usart1.tx_busy = false;

// //     g_usart1.sr_reads_since_enable = 0;
// //     g_usart1.injected_e8 = false;

// //     g_usart1.pty_fd = api_pty_fd_gen();
// //     if (g_usart1.pty_fd < 0) {
// //         dbg("[USART1] api_pty_fd_gen() failed; UART disconnected.\n");
// //     } else {
// //         dbg("[USART1] PTY ready at /tmp/usart1_pty (fd=%d)\n", g_usart1.pty_fd);
// //     }

// //     g_usart1.tx_done_timer = qemu_plugin_timer_new_ns(usart1_tx_done_cb, &g_usart1);
// //     g_usart1.rx_poll_timer = qemu_plugin_timer_new_period_ns(usart1_rx_poll_cb, &g_usart1, 1000000ULL);
// // }

// // Device Model for USART1 (STM32F103xx)
// //
// // Requirements satisfied:
// // - No trace-replay / no enforced loop-order.
// // - RXNE is set only when PTY actually has data.
// // - RXNE is cleared when DR is read.
// // - ORE is set only when additional bytes arrive while RXNE is already set.
// // - TXE/TC behave like an always-ready UART, except briefly after DR write.
// //
// // Notes:
// // - No ISR data provided, so we do not raise IRQs.
// // - We avoid qemu_plugin_read_register usage (signature mismatch in your build logs).

// #include <device.h>
// #include <boardrunner/vio.h>

// #include <stdbool.h>
// #include <stdint.h>
// #include <stdio.h>
// #include <string.h>
// #include <stdarg.h>

// #define USART1_BASE   0x40013800ULL

// #define USART_SR_OFF   0x00
// #define USART_DR_OFF   0x04
// #define USART_BRR_OFF  0x08
// #define USART_CR1_OFF  0x0C
// #define USART_CR2_OFF  0x10
// #define USART_CR3_OFF  0x14
// #define USART_GTPR_OFF 0x18

// // SR bits (STM32F1 subset)
// #define SR_ORE   (1u << 3)
// #define SR_RXNE  (1u << 5)
// #define SR_TC    (1u << 6)
// #define SR_TXE   (1u << 7)

// // CR1 bits (subset)
// #define CR1_RE   (1u << 2)
// #define CR1_TE   (1u << 3)
// #define CR1_UE   (1u << 13)

// #define TX_DELAY_NS 2000ULL          // brief busy time after DR write
// #define RX_POLL_PERIOD_NS 1000000ULL // 1ms poll

// typedef struct usart1_state {
//     uint32_t SR;
//     uint32_t DR;
//     uint32_t BRR;
//     uint32_t CR1;
//     uint32_t CR2;
//     uint32_t CR3;
//     uint32_t GTPR;

//     // RX one-byte holding register (RDR)
//     bool     rx_valid;
//     uint8_t  rx_byte;

//     // TX busy (clears TXE/TC while busy)
//     bool     tx_busy;

//     // Host endpoint
//     int      pty_fd;

//     // Timers
//     uint64_t tx_done_timer;
//     uint64_t rx_poll_timer;
// } usart1_state_t;

// static usart1_state_t g_usart1;

// static void dbg(const char *fmt, ...) {
//     char buf[256];
//     va_list ap;
//     va_start(ap, fmt);
//     vsnprintf(buf, sizeof(buf), fmt, ap);
//     va_end(ap);
//     dev_debug(buf);
// }

// // Handle both absolute-address and offset-address invocation styles.
// static inline uint32_t usart1_off(hwaddr addr) {
//     if (addr >= USART1_BASE && addr < (USART1_BASE + 0x1000)) {
//         return (uint32_t)(addr - USART1_BASE);
//     }
//     return (uint32_t)addr;
// }

// static inline uint64_t mask_by_size(uint64_t v, unsigned size) {
//     switch (size) {
//         case 1: return v & 0xFFu;
//         case 2: return v & 0xFFFFu;
//         default: return v & 0xFFFFFFFFu;
//     }
// }

// static inline bool usart_enabled(const usart1_state_t *s) {
//     return (s->CR1 & CR1_UE) != 0;
// }
// static inline bool rx_enabled(const usart1_state_t *s) {
//     return usart_enabled(s) && ((s->CR1 & CR1_RE) != 0);
// }
// static inline bool tx_enabled(const usart1_state_t *s) {
//     return usart_enabled(s) && ((s->CR1 & CR1_TE) != 0);
// }

// static void usart1_tx_done_cb(void *data) {
//     usart1_state_t *s = (usart1_state_t *)data;
//     s->tx_busy = false;
//     // TXE/TC will be recomputed on SR reads as well; keep SR consistent here too.
//     if (tx_enabled(s)) {
//         s->SR |= (SR_TXE | SR_TC);
//     }
// }

// static void usart1_drain_or_buffer_rx(usart1_state_t *s) {
//     if (s->pty_fd < 0) return;
//     if (!rx_enabled(s)) return;

//     // CRITICAL FIX:
//     // If we are already holding a byte (rx_valid is true),
//     // DO NOT read from the PTY. Let the OS kernel buffer the incoming data.
//     // Only fetch a new byte when the firmware has read the previous one (clearing rx_valid).
//     if (s->rx_valid) {
//         return;
//     }

//     uint8_t b = 0;
//     int st = api_pty_read_nonblock(s->pty_fd, &b);

//     if (st > 0) {
//         s->rx_valid = true;
//         s->rx_byte  = b;
//         s->SR |= SR_RXNE;

//         // Clear ORE flag if we successfully latched a new byte
//         // (Optional, keeps state clean)
//         s->SR &= ~SR_ORE;
//     }
// }

// static void usart1_rx_poll_cb(void *data) {
//     usart1_state_t *s = (usart1_state_t *)data;
//     usart1_drain_or_buffer_rx(s);
// }

// static inline void usart1_update_sr_dynamic(usart1_state_t *s) {
//     if (!usart_enabled(s)) {
//         s->SR = 0;
//         return;
//     }

//     // TX bits
//     if (tx_enabled(s)) {
//         if (s->tx_busy) {
//             s->SR &= ~(SR_TXE | SR_TC);
//         } else {
//             s->SR |= (SR_TXE | SR_TC);
//         }
//     } else {
//         s->SR &= ~(SR_TXE | SR_TC);
//     }

//     // RXNE reflects rx_valid when receiver enabled
//     if (rx_enabled(s) && s->rx_valid) s->SR |= SR_RXNE;
//     else                              s->SR &= ~SR_RXNE;

//     // ORE is sticky until cleared by DR read (approximation)
// }

// // This function will emulate all device reads
// uint64_t usart1_read(void *opaque, hwaddr addr, unsigned size) {
//     (void)opaque;
//     usart1_state_t *s = &g_usart1;

//     uint32_t off = usart1_off(addr);
//     uint32_t r = 0;

//     switch (off) {
//         case USART_SR_OFF:
//             // Optional: opportunistically pull host bytes to reduce latency without needing interrupts
//             usart1_drain_or_buffer_rx(s);
//             usart1_update_sr_dynamic(s);
//             r = s->SR;
//             break;

//         case USART_DR_OFF:
//             if (!usart_enabled(s)) {
//                 r = 0;
//                 break;
//             }

//             if (rx_enabled(s) && s->rx_valid) {
//                 r = (uint32_t)s->rx_byte;
//                 s->rx_valid = false;

//                 // Clear RXNE and (approx) clear ORE on DR read.
//                 // Real STM32 clears ORE by read SR then read DR; firmware is polling SR anyway.
//                 s->SR &= ~(SR_RXNE | SR_ORE);
//             } else {
//                 r = 0;
//             }
//             s->DR = r;
//             break;

//         case USART_BRR_OFF:  r = s->BRR; break;
//         case USART_CR1_OFF:  r = s->CR1; break;
//         case USART_CR2_OFF:  r = s->CR2; break;
//         case USART_CR3_OFF:  r = s->CR3; break;
//         case USART_GTPR_OFF: r = s->GTPR; break;

//         default:
//             r = 0;
//             break;
//     }

//     return mask_by_size(r, size);
// }

// // This function will emulate all device writes
// void usart1_write(void *opaque, hwaddr addr, uint64_t value, unsigned size) {
//     (void)opaque;
//     usart1_state_t *s = &g_usart1;

//     uint32_t off = usart1_off(addr);
//     uint32_t v32 = (uint32_t)mask_by_size(value, size);

//     switch (off) {
//         case USART_CR1_OFF: {
//             uint32_t prev = s->CR1;
//             s->CR1 = v32;

//             bool prev_ue = (prev & CR1_UE) != 0;
//             bool now_ue  = (s->CR1 & CR1_UE) != 0;

//             if (!now_ue) {
//                 // USART disabled: reset observable state
//                 s->SR = 0;
//                 s->rx_valid = false;
//                 s->tx_busy = false;
//             } else if (!prev_ue && now_ue) {
//                 // On enable: start with SR cleared (some firmwares observe SR=0 early),
//                 // then TXE/TC will become visible when not busy.
//                 s->SR = 0;
//                 s->rx_valid = false;
//                 s->tx_busy = false;

//                 // If TX enabled, make it ready shortly after enable (timing approximation)
//                 if (tx_enabled(s)) {
//                     uint64_t now = (uint64_t)qemu_plugin_get_virtual_timer();
//                     qemu_plugin_timer_alarm(s->tx_done_timer, now + 100ULL);
//                 }
//             }

//             // Update SR bits based on new gating
//             usart1_update_sr_dynamic(s);
//             break;
//         }

//         case USART_CR2_OFF: s->CR2 = v32; break;
//         case USART_CR3_OFF: s->CR3 = v32; break;
//         case USART_BRR_OFF: s->BRR = v32; break;

//         case USART_DR_OFF: {
//             s->DR = v32;

//             if (tx_enabled(s)) {
//                 uint8_t b = (uint8_t)(v32 & 0xFFu);

//                 if (s->pty_fd >= 0) {
//                     api_pty_write_req(s->pty_fd, b);
//                 }

//                 // Busy window: TXE/TC cleared, then restored by timer.
//                 s->tx_busy = true;
//                 s->SR &= ~(SR_TXE | SR_TC);

//                 uint64_t now = (uint64_t)qemu_plugin_get_virtual_timer();
//                 qemu_plugin_timer_alarm(s->tx_done_timer, now + TX_DELAY_NS);
//             }
//             break;
//         }

//         case USART_SR_OFF:
//             // SR writes ignored for this workload
//             break;

//         case USART_GTPR_OFF:
//             s->GTPR = v32;
//             break;

//         default:
//             break;
//     }
// }

// void usart1_init(ConfigSection* model_info) {
//     (void)model_info;
//     memset(&g_usart1, 0, sizeof(g_usart1));

//     g_usart1.SR = 0;
//     g_usart1.rx_valid = false;
//     g_usart1.tx_busy = false;

//     // PTY endpoint at /tmp/usart1_pty
//     g_usart1.pty_fd = api_pty_fd_gen();
//     if (g_usart1.pty_fd < 0) {
//         dbg("[USART1] api_pty_fd_gen() failed; UART disconnected.\n");
//     } else {
//         dbg("[USART1] PTY ready at /tmp/usart1_pty (fd=%d)\n", g_usart1.pty_fd);
//     }

//     g_usart1.tx_done_timer = qemu_plugin_timer_new_ns(usart1_tx_done_cb, &g_usart1);

//     // Periodic RX poll so RXNE/ORE can assert asynchronously (hardware-like)
//     g_usart1.rx_poll_timer = qemu_plugin_timer_new_period_ns(
//         usart1_rx_poll_cb, &g_usart1, RX_POLL_PERIOD_NS
//     );
// }

// Device Model for USART1 (STM32F103xx)
// - No trace replay / no enforced loop ordering.
// - RXNE set only when PTY provides a byte; cleared when DR is read.
// - ORE set only if additional PTY data arrives while RXNE still set; cleared by SR read then DR read.
// - TXE/TC modeled for forward progress when UE+TE enabled.

#include <device.h>
#include <boardrunner/vio.h>

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>

#define USART1_BASE      0x40013800ULL

#define USART_SR_OFF     0x00
#define USART_DR_OFF     0x04
#define USART_BRR_OFF    0x08
#define USART_CR1_OFF    0x0C
#define USART_CR2_OFF    0x10
#define USART_CR3_OFF    0x14
#define USART_GTPR_OFF   0x18

// SR bits (subset)
#define SR_ORE   (1u << 3)
#define SR_RXNE  (1u << 5)
#define SR_TC    (1u << 6)
#define SR_TXE   (1u << 7)

// CR1 bits (subset)
#define CR1_RE   (1u << 2)
#define CR1_TE   (1u << 3)
#define CR1_UE   (1u << 13)

typedef struct usart1_state {
    uint32_t SR;
    uint32_t DR;
    uint32_t BRR;
    uint32_t CR1;
    uint32_t CR2;
    uint32_t CR3;
    uint32_t GTPR;

    // Single-byte RX latch (matches STM32F1 “1-byte deep” behavior)
    bool     rx_valid;
    uint8_t  rx_byte;

    // For ORE clear sequencing: ORE cleared by read SR then read DR.
    bool     sr_read_since_last_dr;

    // Host endpoint
    int      pty_fd;
} usart1_state_t;

static usart1_state_t g_usart1;

static void dbg(const char *fmt, ...) {
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    dev_debug(buf);
}

static inline uint32_t usart1_off(hwaddr addr) {
    if (addr >= USART1_BASE && addr < (USART1_BASE + 0x1000)) {
        return (uint32_t)(addr - USART1_BASE);
    }
    // If boardrunner passes an offset already
    return (uint32_t)addr;
}

static inline uint64_t mask_by_size(uint64_t v, unsigned size) {
    switch (size) {
        case 1: return v & 0xFFu;
        case 2: return v & 0xFFFFu;
        default: return v & 0xFFFFFFFFu;
    }
}

static inline bool usart_enabled(const usart1_state_t *s) {
    return (s->CR1 & CR1_UE) != 0;
}
static inline bool rx_enabled(const usart1_state_t *s) {
    return usart_enabled(s) && ((s->CR1 & CR1_RE) != 0);
}
static inline bool tx_enabled(const usart1_state_t *s) {
    return usart_enabled(s) && ((s->CR1 & CR1_TE) != 0);
}

// Poll PTY input non-blocking and update RXNE/ORE semantics.
// - If RX latch empty and a byte arrives: latch it, set RXNE.
// - If RX latch already full and more bytes arrive: set ORE, drop extras.
static void usart1_poll_rx(usart1_state_t *s) {
    if (s->pty_fd < 0) return;

    // Drain a bounded amount to avoid spending too long in one MMIO read
    for (int i = 0; i < 64; i++) {
        uint8_t b = 0;
        int st = api_pty_read_nonblock(s->pty_fd, &b);
        if (st <= 0) break;

        if (!rx_enabled(s)) {
            // Receiver disabled: drop incoming data
            continue;
        }

        if (!s->rx_valid) {
            s->rx_valid = true;
            s->rx_byte  = b;
            s->SR |= SR_RXNE;
        } else {
            // Overrun: new byte arrived before software read DR
            s->SR |= SR_ORE;
            // Drop the extra byte (STM32F1 overrun behavior)
        }
    }

    // Keep RXNE consistent
    if (rx_enabled(s) && s->rx_valid) s->SR |= SR_RXNE;
    else                              s->SR &= ~SR_RXNE;
}

static void usart1_update_tx_flags(usart1_state_t *s) {
    if (!tx_enabled(s)) {
        s->SR &= ~(SR_TXE | SR_TC);
        return;
    }
    // For robustness in polling firmware: always ready.
    // (TXE/TC will be observed as 1 in traces like 0xC0.)
    s->SR |= (SR_TXE | SR_TC);
}

// This function will emulation all device reads
uint64_t usart1_read(void *opaque, hwaddr addr, unsigned size) {
    (void)opaque;
    usart1_state_t *s = &g_usart1;

    uint32_t off = usart1_off(addr);
    uint32_t r = 0;

    switch (off) {
        case USART_SR_OFF:
            s->sr_read_since_last_dr = true;

            // Make SR reflect real host input availability
            usart1_poll_rx(s);
            usart1_update_tx_flags(s);

            r = s->SR;
            break;

        case USART_DR_OFF:
            if (!usart_enabled(s)) {
                r = 0;
                break;
            }

            // Reading DR returns the latched RX byte if present
            if (rx_enabled(s) && s->rx_valid) {
                r = (uint32_t)s->rx_byte;
                s->rx_valid = false;
            } else {
                r = 0;
            }

            // DR read clears RXNE
            s->SR &= ~SR_RXNE;

            // ORE cleared only by SR read then DR read
            if (s->sr_read_since_last_dr) {
                s->SR &= ~SR_ORE;
            }
            s->sr_read_since_last_dr = false;

            s->DR = r;
            break;

        case USART_BRR_OFF:  r = s->BRR; break;
        case USART_CR1_OFF:  r = s->CR1; break;
        case USART_CR2_OFF:  r = s->CR2; break;
        case USART_CR3_OFF:  r = s->CR3; break;
        case USART_GTPR_OFF: r = s->GTPR; break;

        default:
            r = 0;
            break;
    }

    return mask_by_size(r, size);
}

// This function will emulate all device writes
void usart1_write(void *opaque, hwaddr addr, uint64_t value, unsigned size) {
    (void)opaque;
    usart1_state_t *s = &g_usart1;

    uint32_t off = usart1_off(addr);
    uint32_t v32 = (uint32_t)mask_by_size(value, size);

    switch (off) {
        case USART_CR1_OFF: {
            uint32_t prev = s->CR1;
            s->CR1 = v32;

            bool prev_ue = (prev & CR1_UE) != 0;
            bool now_ue  = (s->CR1 & CR1_UE) != 0;

            if (!now_ue) {
                // Disable: clear visible state
                s->SR = 0;
                s->rx_valid = false;
                s->sr_read_since_last_dr = false;
            } else if (!prev_ue && now_ue) {
                // Enable: start clean
                s->SR = 0;
                s->rx_valid = false;
                s->sr_read_since_last_dr = false;
            }

            // Recompute flag gating
            usart1_poll_rx(s);
            usart1_update_tx_flags(s);
            break;
        }

        case USART_CR2_OFF: s->CR2 = v32; break;
        case USART_CR3_OFF: s->CR3 = v32; break;
        case USART_BRR_OFF: s->BRR = v32; break;
        case USART_GTPR_OFF: s->GTPR = v32; break;

        case USART_DR_OFF: {
            s->DR = v32;
            s->sr_read_since_last_dr = false;

            if (tx_enabled(s)) {
                uint8_t b = (uint8_t)(v32 & 0xFFu);
                if (s->pty_fd >= 0) {
                    api_pty_write_req(s->pty_fd, b);
                }
                // Keep TXE/TC asserted for polling-style firmware
                usart1_update_tx_flags(s);
            }
            break;
        }

        case USART_SR_OFF:
            // Ignore SR writes in this minimal model
            break;

        default:
            break;
    }
}

void usart1_init(ConfigSection* model_info) {
    (void)model_info;
    memset(&g_usart1, 0, sizeof(g_usart1));

    g_usart1.pty_fd = api_pty_fd_gen();
    if (g_usart1.pty_fd < 0) {
        dbg("[USART1] api_pty_fd_gen() failed; /tmp/usart1_pty unavailable.\n");
    } else {
        dbg("[USART1] PTY ready at /tmp/usart1_pty (fd=%d)\n", g_usart1.pty_fd);
    }

    // Start disabled until firmware sets CR1. Flags will be computed on reads/writes.
    g_usart1.SR = 0;
    g_usart1.rx_valid = false;
    g_usart1.sr_read_since_last_dr = false;
}
