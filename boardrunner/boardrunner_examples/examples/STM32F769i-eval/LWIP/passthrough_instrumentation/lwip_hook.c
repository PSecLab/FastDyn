#include <device.h>
#include <hw.h>
#include <utils.h>
#include <pthread.h>
#include <unistd.h>
#include <time.h>
#include <stdlib.h>

static hw_t *hw = NULL;

#define QEMU_DOORBELL_ADDR 0x40000000u

/* ---------- Tunables / config ---------- */
static uint32_t ETH_TX_DESC_CNT = 4;
static uint32_t ETH_RX_DESC_CNT = 4;
static uint32_t ETH_RX_BUF_SIZE = 1528;

static uint32_t sizeof_ETH_DMADescTypeDef = 40; // can also use sizeof(ETH_DMADescTypeDef)

/* ---------- Descriptor helpers ---------- */
#define DESC_STRIDE_BYTES   (0x28u)          // confirmed by your trace; equals 40 bytes
#define OWN_BIT             (1u << 31)       // OWN bit in DESC0 for both Tx/Rx

#define TX_OWN_BIT   (1u << 31)
#define TX_LS_BIT    (1u << 29)
#define TX_FS_BIT    (1u << 28)
#define TX_TCH_BIT   (1u << 20)   // chained mode => DESC3 is next desc pointer
#define TX_TER_BIT   (1u << 21)   // end of ring (may be used if not chained)

/* TX sizes in DESC1 for Synopsys-style descriptors (works for STM32F7 ETH DMA) */
static inline uint16_t tx_buf1_len_from_desc1(uint32_t desc1) { return (uint16_t)(desc1 & 0x1FFFu); }
static inline uint16_t tx_buf2_len_from_desc1(uint32_t desc1) { return (uint16_t)((desc1 >> 16) & 0x1FFFu); }

/* RX frame length is typically in DESC0[29:16] for Synopsys GMAC DMA descriptors */
static inline uint16_t rx_frame_len_from_desc0(uint32_t desc0) { return (uint16_t)((desc0 >> 16) & 0x3FFFu); }

/* ---------- DMA register addresses (from your trace) ---------- */
enum {
  REG_DMATPDR = 0x40029004,
  REG_DMARPDR = 0x40029008,
  REG_DMARDLAR = 0x4002900C,
  REG_DMATDLAR = 0x40029010,
  REG_DMASR   = 0x40029014,
  REG_DMAOMR  = 0x40029018,
};

/* ---------- Your ETH descriptor ---------- */
typedef struct
{
  volatile uint32_t DESC0;
  volatile uint32_t DESC1;
  volatile uint32_t DESC2;
  volatile uint32_t DESC3;
  volatile uint32_t DESC4;
  volatile uint32_t DESC5;
  volatile uint32_t DESC6;
  volatile uint32_t DESC7;
  uint32_t BackupAddr0;
  uint32_t BackupAddr1;
} ETH_DMADescTypeDef;

/* ---------- Sync context ---------- */
typedef struct {
  uint32_t tx_base;     // from DMATDLAR
  uint32_t rx_base;     // from DMARDLAR
  bool have_tx_base;
  bool have_rx_base;

  uint32_t last_dmatpdr;
  uint32_t last_dmarpdr;
} EthSyncCtx;

static EthSyncCtx g_eth;

/* ---------- QEMU guest memory helpers ---------- */

static int qemu_read_mem(uint32_t addr, uint8_t *out, size_t len)
{
  return (qemu_plugin_read_memory((unsigned long long)addr, out, (int)len) == 0) ? 0 : -1;
}

static int qemu_write_mem(uint32_t addr, const uint8_t *in, size_t len)
{
  // API takes non-const
  return (qemu_plugin_write_memory((unsigned long long)addr, (uint8_t *)in, (int)len) == 0) ? 0 : -1;
}

static int qemu_read_desc_words(uint32_t desc_addr, uint32_t words[10])
{
  uint8_t buf[DESC_STRIDE_BYTES];
  if (qemu_read_mem(desc_addr, buf, sizeof(buf)) != 0) return -1;
  for (int i = 0; i < 10; i++) {
    words[i] = (uint32_t)buf[i*4 + 0]
             | ((uint32_t)buf[i*4 + 1] << 8)
             | ((uint32_t)buf[i*4 + 2] << 16)
             | ((uint32_t)buf[i*4 + 3] << 24);
  }
  return 0;
}

/* Write descriptor to board with DESC0 (OWN) last */
static int board_write_desc_words_desc0_last(hw_t *hw, uint32_t desc_addr, const uint32_t words[10])
{
  for (int i = 1; i < 10; i++) {
    if (hw_write32(hw, desc_addr + (uint32_t)(i*4), words[i]) != 0) return -1;
  }
  // DESC0 last (makes OWN visible last)
  if (hw_write32(hw, desc_addr + 0, words[0]) != 0) return -1;
  return 0;
}

/* ---------- Low-level board memory access (byte copies using read32/write32/write8) ---------- */

static int board_read_mem(hw_t *hw, uint32_t addr, uint8_t *out, size_t len)
{
  // Uses 32-bit reads; handles unaligned addr/len.
  size_t i = 0;

  // Handle leading unaligned bytes
  while ((addr & 3u) && i < len) {
    uint32_t w;
    uint32_t a_al = addr & ~3u;
    if (hw_read32(hw, a_al, &w) != 0) return -1;
    uint32_t shift = (addr & 3u) * 8u;
    out[i++] = (uint8_t)((w >> shift) & 0xFFu);
    addr += 1;
  }

  // Aligned bulk
  while (i + 4 <= len) {
    uint32_t w;
    if (hw_read32(hw, addr, &w) != 0) return -1;
    out[i + 0] = (uint8_t)(w & 0xFFu);
    out[i + 1] = (uint8_t)((w >> 8) & 0xFFu);
    out[i + 2] = (uint8_t)((w >> 16) & 0xFFu);
    out[i + 3] = (uint8_t)((w >> 24) & 0xFFu);
    i += 4;
    addr += 4;
  }

  // Trailing bytes
  while (i < len) {
    uint32_t w;
    uint32_t a_al = addr & ~3u;
    if (hw_read32(hw, a_al, &w) != 0) return -1;
    uint32_t shift = (addr & 3u) * 8u;
    out[i++] = (uint8_t)((w >> shift) & 0xFFu);
    addr += 1;
  }

  return 0;
}

static int board_write_mem(hw_t *hw, uint32_t addr, const uint8_t *in, size_t len)
{
  // Writes aligned 32-bit chunks where possible; falls back to write8.
  size_t i = 0;

  // Leading unaligned
  while ((addr & 3u) && i < len) {
    if (hw_write8(hw, addr, in[i]) != 0) return -1;
    addr += 1;
    i += 1;
  }

  // Bulk aligned
  while (i + 4 <= len) {
    uint32_t w = (uint32_t)in[i]
               | ((uint32_t)in[i + 1] << 8)
               | ((uint32_t)in[i + 2] << 16)
               | ((uint32_t)in[i + 3] << 24);
    if (hw_write32(hw, addr, w) != 0) return -1;
    addr += 4;
    i += 4;
  }

  // Trailing
  while (i < len) {
    if (hw_write8(hw, addr, in[i]) != 0) return -1;
    addr += 1;
    i += 1;
  }

  return 0;
}


/* ---------- Ring indexing ---------- */

static inline uint32_t ring_idx_from_ptr(uint32_t base, uint32_t ptr)
{
  if (ptr < base) return 0;
  uint32_t off = ptr - base;
  return (off / DESC_STRIDE_BYTES);
}

static inline uint32_t ring_desc_addr(uint32_t base, uint32_t idx)
{
  return base + (idx * DESC_STRIDE_BYTES);
}

//test

static int copy_qemu_to_board(hw_t *hw, uint32_t addr, uint32_t len)
{
  // Avoid malloc in hot path: copy in chunks
  uint8_t tmp[256];
  uint32_t off = 0;

  while (off < len) {
    uint32_t chunk = (len - off > sizeof(tmp)) ? sizeof(tmp) : (len - off);
    if (qemu_read_mem(addr + off, tmp, chunk) != 0) return -1;
    if (board_write_mem(hw, addr + off, tmp, chunk) != 0) return -1;
    off += chunk;
  }
  return 0;
}

static uint32_t ring_idx_from_tpdr(uint32_t base, uint32_t tpdr)
{
  if (tpdr < base) return 0xFFFFFFFFu;
  uint32_t off = tpdr - base;
  if ((off % DESC_STRIDE_BYTES) != 0) return 0xFFFFFFFFu;
  return off / DESC_STRIDE_BYTES;
}

static void push_tx_from_tpdr(uint32_t tpdr_value)
{
  if (!g_eth.have_tx_base) return;

  uint32_t idx_free = ring_idx_from_tpdr(g_eth.tx_base, tpdr_value);
  if (idx_free == 0xFFFFFFFFu || idx_free >= ETH_TX_DESC_CNT) {
    // If TPDR is weird, fallback: scan whole ring and copy OWN=1 descriptors.
    // But don't break on the first OWN=0.
    for (uint32_t i = 0; i < ETH_TX_DESC_CNT; i++) {
      uint32_t q_desc_addr = ring_desc_addr(g_eth.tx_base, i);
      uint32_t d[10];
      if (qemu_read_desc_words(q_desc_addr, d) != 0) return;

      uint32_t desc0 = d[0];
      if ((desc0 & TX_OWN_BIT) == 0) continue;

      uint32_t desc1 = d[1];
      uint32_t buf1  = d[2];
      uint32_t desc3 = d[3];

      uint16_t len1 = tx_buf1_len_from_desc1(desc1);

      if (len1 && buf1) {
        if (copy_qemu_to_board(hw, buf1, len1) != 0) return;
      }

      // if not chained, DESC3 could be buf2
      if ((desc0 & TX_TCH_BIT) == 0) {
        uint16_t len2 = tx_buf2_len_from_desc1(desc1);
        if (len2 && desc3) {
          if (copy_qemu_to_board(hw, desc3, len2) != 0) return;
        }
      }

      (void)board_write_desc_words_desc0_last(hw, q_desc_addr, d);
    }
    return;
  }

  // Descriptor likely queued is the one BEFORE the "next free" pointer
  uint32_t idx_last = (idx_free + ETH_TX_DESC_CNT - 1) % ETH_TX_DESC_CNT;

  // Find FS by walking backward (handles multi-descriptor frames)
  uint32_t idx_first = idx_last;
  for (uint32_t step = 0; step < ETH_TX_DESC_CNT; step++) {
    uint32_t addr = ring_desc_addr(g_eth.tx_base, idx_first);
    uint32_t d[10];
    if (qemu_read_desc_words(addr, d) != 0) return;

    if (d[0] & TX_FS_BIT) break; // found frame start
    idx_first = (idx_first + ETH_TX_DESC_CNT - 1) % ETH_TX_DESC_CNT;
  }

  // Walk forward, copy until LS
  uint32_t idx = idx_first;
  for (uint32_t step = 0; step < ETH_TX_DESC_CNT; step++) {
    uint32_t q_desc_addr = ring_desc_addr(g_eth.tx_base, idx);

    uint32_t d[10];
    if (qemu_read_desc_words(q_desc_addr, d) != 0) return;

    uint32_t desc0 = d[0];
    uint32_t desc1 = d[1];
    uint32_t buf1  = d[2];
    uint32_t desc3 = d[3];

    // printf("The current desc0 in passthrough is %x\n", desc0);
    // utils_die("die");
    // Only push descriptors that DMA is supposed to own
    if ((desc0 & TX_OWN_BIT) == 0) {
      // If OWN isn't set, something is inconsistent; stop rather than pushing garbage.
      return;
    }

    uint16_t len1 = tx_buf1_len_from_desc1(desc1);
    if (len1 && buf1) {
      if (copy_qemu_to_board(hw, buf1, len1) != 0) return;
    }

    // IMPORTANT: if chained, DESC3 is NEXT DESC POINTER, not buf2
    if ((desc0 & TX_TCH_BIT) == 0) {
      uint16_t len2 = tx_buf2_len_from_desc1(desc1);
      if (len2 && desc3) {
        if (copy_qemu_to_board(hw, desc3, len2) != 0) return;
      }
    }

    // Write descriptor last with DESC0 last (OWN visible last)
    if (board_write_desc_words_desc0_last(hw, q_desc_addr, d) != 0) return;

    if (desc0 & TX_LS_BIT) break; // end of frame

    idx = (idx + 1) % ETH_TX_DESC_CNT;
    }
}

#ifndef TX_PULL_TIMEOUT_US
#define TX_PULL_TIMEOUT_US     50000u   // 50 ms (>= HAL timeout like 20ms)
#endif

#ifndef TX_PULL_DELAY_US
#define TX_PULL_DELAY_US       50u      // 50 us backoff to avoid busy-spin
#endif

static inline uint64_t now_us_monotonic(void)
{
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (uint64_t)ts.tv_sec * 1000000ull + (uint64_t)ts.tv_nsec / 1000ull;
}

/* ---- QEMU little-endian 32-bit helpers ---- */

static int qemu_read32_le(uint32_t addr, uint32_t *out)
{
  uint8_t b[4];
  if (qemu_read_mem(addr, b, 4) != 0) return -1;
  *out = (uint32_t)b[0]
       | ((uint32_t)b[1] << 8)
       | ((uint32_t)b[2] << 16)
       | ((uint32_t)b[3] << 24);
  return 0;
}

static int qemu_write32_le(uint32_t addr, uint32_t v)
{
  uint8_t b[4];
  b[0] = (uint8_t)(v & 0xFFu);
  b[1] = (uint8_t)((v >> 8) & 0xFFu);
  b[2] = (uint8_t)((v >> 16) & 0xFFu);
  b[3] = (uint8_t)((v >> 24) & 0xFFu);
  return (qemu_write_mem(addr, b, 4) == 0) ? 0 : -1;
}

/* ---- Compute which QEMU descriptors are currently pending (OWN=1) ---- */

static uint32_t tx_pending_mask_from_qemu(void)
{
  if (!g_eth.have_tx_base) return 0;

  uint32_t pending = 0;
  for (uint32_t i = 0; i < ETH_TX_DESC_CNT; i++) {
    uint32_t desc0 = 0;
    uint32_t addr  = ring_desc_addr(g_eth.tx_base, i);
    if (qemu_read32_le(addr, &desc0) != 0) continue;
    if (desc0 & TX_OWN_BIT) pending |= (1u << i);
  }
  return pending;
}

/* ---- Board descriptor read (10 words = 40 bytes) ---- */

static int board_read_desc_words(hw_t *hw, uint32_t desc_addr, uint32_t words[10])
{
  for (int i = 0; i < 10; i++) {
    if (hw_read32(hw, desc_addr + (uint32_t)(i * 4), &words[i]) != 0) {
      return -1;
    }
  }
  return 0;
}

/* ---- Mirror descriptor board -> QEMU, DESC0 last ---- */

static int qemu_write_desc_words_desc0_last(uint32_t desc_addr, const uint32_t words[10])
{
  /* write DESC1..BackupAddr1 first */
  for (int i = 1; i < 10; i++) {
    if (qemu_write32_le(desc_addr + (uint32_t)(i * 4), words[i]) != 0) return -1;
  }
  /* DESC0 last (OWN clears become visible last) */
  if (qemu_write32_le(desc_addr + 0, words[0]) != 0) return -1;
  return 0;
}

/* ---- One pass: for any pending desc, if board has OWN=0 then mirror full desc back ---- */

static int pull_tx_completion_once(hw_t *hw, uint32_t *pending_mask_io, int *completed_io)
{
  uint32_t pending = *pending_mask_io;
  int completed = 0;

  for (uint32_t i = 0; i < ETH_TX_DESC_CNT; i++) {
    if ((pending & (1u << i)) == 0) continue;

    uint32_t desc_addr = ring_desc_addr(g_eth.tx_base, i);

    /* Quick check: board DESC0 */
    uint32_t b_desc0 = 0;
    if (hw_read32(hw, desc_addr + 0, &b_desc0) != 0) return -1;

    /* If board DMA cleared OWN, mirror the full descriptor */
    if ((b_desc0 & TX_OWN_BIT) == 0) {
      uint32_t w[10];
      if (board_read_desc_words(hw, desc_addr, w) != 0) return -1;

      if (qemu_write_desc_words_desc0_last(desc_addr, w) != 0) return -1;

      pending &= ~(1u << i);
      completed++;
    }
  }

  *pending_mask_io = pending;
  if (completed_io) *completed_io += completed;
  return 0;
}
static int pull_tx_completion_wait(hw_t *hw, uint32_t timeout_us)
{
  if (!g_eth.have_tx_base) return 0;

  uint32_t pending = tx_pending_mask_from_qemu();
  if (pending == 0) return 0;

  const uint64_t t0 = now_us_monotonic();
  int completed_total = 0;

  while (pending != 0) {
    int before = completed_total;

    if (pull_tx_completion_once(hw, &pending, &completed_total) != 0) {
      return -1;
    }

    /* If no progress, backoff a bit */
    if (completed_total == before) {
      if ((now_us_monotonic() - t0) >= (uint64_t)timeout_us) {
        break; /* timeout: leave remaining OWN=1 as-is */
      }
      usleep(TX_PULL_DELAY_US);
    }
  }

  return completed_total;
}

//-------------------------------------------------------RX Path Implementation----------------------------

/* ---------- RX helpers ---------- */
#define RX_OWN_BIT  (1u << 31)

/* For Synopsys-style descriptors, buffer1 size is in DESC1[12:0] */
static inline uint16_t rx_buf1_size_from_desc1(uint32_t desc1)
{
  return (uint16_t)(desc1 & 0x1FFFu);
}

static int copy_board_to_qemu(hw_t *hw, uint32_t addr, uint32_t len)
{
  uint8_t tmp[256];
  uint32_t off = 0;

  while (off < len) {
    uint32_t chunk = (len - off > sizeof(tmp)) ? sizeof(tmp) : (len - off);

    if (board_read_mem(hw, addr + off, tmp, chunk) != 0) return -1;
    if (qemu_write_mem(addr + off, tmp, chunk) != 0) return -1;

    off += chunk;
  }
  return 0;
}

/* ---- Pull RX: if board has OWN=0 but QEMU still has OWN=1, mirror desc + payload to QEMU ---- */
static int pull_rx_ready_once(hw_t *hw)
{
  if (!g_eth.have_rx_base) return 0;

  int pulled = 0;

  for (uint32_t i = 0; i < ETH_RX_DESC_CNT; i++) {
    uint32_t desc_addr = ring_desc_addr(g_eth.rx_base, i);

    /* QEMU-side quick check: if QEMU already sees OWN=0, nothing to do */
    uint32_t q_desc0 = 0;
    if (qemu_read32_le(desc_addr + 0, &q_desc0) != 0) continue;
    if ((q_desc0 & RX_OWN_BIT) == 0) continue;

    /* Board-side quick check */
    uint32_t b_desc0 = 0;
    if (hw_read32(hw, desc_addr + 0, &b_desc0) != 0) return -1;

    /* Not ready yet (DMA still owns it) */
    if (b_desc0 & RX_OWN_BIT) continue;

    /* Read full descriptor from board */
    uint32_t w[10];
    if (board_read_desc_words(hw, desc_addr, w) != 0) return -1;

    uint32_t desc0 = w[0];
    uint32_t desc1 = w[1];
    uint32_t buf1  = w[2];

    /* Copy payload board->QEMU.
       Safe rule: copy buffer1 size (clamped). lwIP uses HAL-provided length anyway. */
    uint32_t to_copy = (uint32_t)rx_buf1_size_from_desc1(desc1);
    if (to_copy == 0) to_copy = ETH_RX_BUF_SIZE;
    if (to_copy > ETH_RX_BUF_SIZE) to_copy = ETH_RX_BUF_SIZE;

    if (buf1 != 0 && to_copy != 0) {
      if (copy_board_to_qemu(hw, buf1, to_copy) != 0) return -1;
    }

    /* Mirror descriptor board->QEMU, DESC0 last (so OWN=0 becomes visible last) */
    if (qemu_write_desc_words_desc0_last(desc_addr, w) != 0) return -1;

    pulled++;
  }

  return pulled;
}

/* ---- Push RX refill: if QEMU has OWN=1 but board still OWN=0, push desc to board (DESC0 last) ---- */
static int push_rx_refill_from_qemu(hw_t *hw)
{
  if (!g_eth.have_rx_base) return 0;

  int pushed = 0;

  for (uint32_t i = 0; i < ETH_RX_DESC_CNT; i++) {
    uint32_t desc_addr = ring_desc_addr(g_eth.rx_base, i);

    /* QEMU owns? We only care about descriptors CPU returned to DMA (OWN=1) */
    uint32_t q_desc0 = 0;
    if (qemu_read32_le(desc_addr + 0, &q_desc0) != 0) continue;
    if ((q_desc0 & RX_OWN_BIT) == 0) continue;

    /* If board already has OWN=1, nothing to do */
    uint32_t b_desc0 = 0;
    if (hw_read32(hw, desc_addr + 0, &b_desc0) != 0) return -1;
    if (b_desc0 & RX_OWN_BIT) continue;

    /* Copy descriptor QEMU->board (DESC0 last so DMA sees OWN last) */
    uint32_t w[10];
    if (qemu_read_desc_words(desc_addr, w) != 0) return -1;

    if (board_write_desc_words_desc0_last(hw, desc_addr, w) != 0) return -1;

    pushed++;
  }

  return pushed;
}
