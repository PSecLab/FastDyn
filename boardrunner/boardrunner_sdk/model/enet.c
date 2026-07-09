#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <device.h>
#include <boardrunner/vio.h>

#define ENET0_BASE       0x402D8000ULL
#define ENET1_BASE       0x402D4000ULL
#define ENET_WINDOW_SIZE 0x628U
#define ENET_REG_WORDS   (ENET_WINDOW_SIZE / 4U)
#define ENET_INSTANCES   2U

#define ENET_EIR   0x004U
#define ENET_EIMR  0x008U
#define ENET_RDAR  0x010U
#define ENET_TDAR  0x014U
#define ENET_ECR   0x024U
#define ENET_MMFR  0x040U
#define ENET_MSCR  0x044U
#define ENET_RDSR  0x180U
#define ENET_TDSR  0x184U
#define ENET_MRBR  0x188U

#define ENET_ECR_RESET_MASK 0x00000001U
#define ENET_EIR_TXF_MASK   0x08000000U
#define ENET_EIR_TXB_MASK   0x04000000U
#define ENET_EIR_RXF_MASK   0x02000000U
#define ENET_EIR_RXB_MASK   0x01000000U
#define ENET_EIR_MII_MASK   0x00800000U

#define ENET_MMFR_ST_SHIFT   30U
#define ENET_MMFR_OP_SHIFT   28U
#define ENET_MMFR_PA_SHIFT   23U
#define ENET_MMFR_RA_SHIFT   18U
#define ENET_MMFR_DATA_MASK  0x0000FFFFU

#define ENET_TX_BD_SIZE             8U
#define ENET_TX_BD_READY_MASK       0x8000U
#define ENET_TX_BD_WRAP_MASK        0x2000U
#define ENET_TX_BD_LAST_MASK        0x0800U
#define ENET_TX_SCAN_LIMIT          128U
#define ENET_TX_FRAME_MAX_COPY      2048U
#define ENET_RX_BD_SIZE             8U
#define ENET_RX_BD_EMPTY_MASK       0x8000U
#define ENET_RX_BD_WRAP_MASK        0x2000U
#define ENET_RX_BD_LAST_MASK        0x0800U
#define ENET_RX_SCAN_LIMIT          128U
#define ENET_RX_FRAME_MAX_COPY      2048U
#define ENET_RX_QUEUE_DEPTH         16U
#define ENET_RX_TIMER_PERIOD_NS     1000000ULL
#define ENET0_COMMON_IRQ            114
#define ENET1_COMMON_IRQ            116
#define ENET_ETH_HDR_LEN            14U
#define ENET_ETHERTYPE_IPV4         0x0800U
#define ENET_IPV4_MIN_HDR_LEN       20U
#define ENET_IPV4_FRAG_MASK         0x3FFFU
#define ENET_ICMP_MIN_LEN           4U
#define ENET_UDP_HDR_LEN            8U
#define ENET_TCP_MIN_HDR_LEN        20U

#define TJA1103_PHYID1                          0x001BU
#define TJA1103_PHYID2                          0xB013U
#define TJA1103_MMD_PMAPMD                      0x01U
#define TJA1103_MMD_VENDOR_SPECIFIC1            0x1EU
#define TJA1103_DEVICE_CONTROL                  0x0040U
#define TJA1103_DEVICE_CONTROL_GLOBAL_CFG_EN    0x4000U
#define TJA1103_DEVICE_CONTROL_SUPER_CFG_EN     0x2000U
#define TJA1103_PMA_PMD_BT1_CTRL                0x0834U
#define TJA1103_PMA_PMD_BT1_CTRL_CFG_MST        0x4000U
#define TJA1103_ALWAYS_ACCESSIBLE               0x801FU
#define TJA1103_PHY_FUNC_IRQ_ACK                0x80A0U
#define TJA1103_PHY_FUNC_IRQ_EN                 0x80A1U
#define TJA1103_PHY_FUNC_IRQ_LINK_EVENT_EN      0x0002U
#define TJA1103_PHY_FUNC_IRQ_LINK_AVAIL_EN      0x0004U
#define TJA1103_PHY_FUNC_IRQ_MSTATUS            0x80A2U
#define TJA1103_PHY_FUNC_IRQ_LINK_EVENT         0x0002U
#define TJA1103_PHY_FUNC_IRQ_LINK_AVAIL         0x0004U
#define TJA1103_PHY_CONTROL                     0x8100U
#define TJA1103_PHY_CONTROL_CFG_EN              0x4000U
#define TJA1103_PHY_STATUS                      0x8102U
#define TJA1103_PHY_STATUS_LINK_STAT            0x0004U

typedef struct {
    uint16_t length;
    uint16_t control;
    uint32_t buffer;
} EnetTxBd;

typedef struct {
    uint16_t len;
    uint8_t data[ENET_RX_FRAME_MAX_COPY];
} EnetPendingFrame;

typedef struct {
    uint16_t device_control;
    uint16_t phy_control;
    uint16_t bt1_ctrl;
    uint16_t always_accessible;
    uint16_t irq_en;
    uint16_t irq_mstatus;
} EnetTja1103State;

typedef struct {
    uint64_t base;
    int tap_fd;
    int common_irq;
    bool phy_initialized;
    bool rx_active;
    uint8_t rx_q_head;
    uint8_t rx_q_tail;
    uint8_t rx_q_count;
    uint32_t tx_next_bd;
    uint32_t rx_next_bd;
    uint64_t rx_timer;
    uint32_t regs[ENET_REG_WORDS];
    uint16_t phy_regs[32][32];
    uint16_t c45_addr[32][32];
    uint16_t c45_data[32][32];
    EnetPendingFrame rx_queue[ENET_RX_QUEUE_DEPTH];
    EnetTja1103State tja1103[32];
} EnetState;

static EnetState g_enet[ENET_INSTANCES];
static unsigned g_enet_count;
static const char *const g_enet_tap_names[ENET_INSTANCES] = { "enet", "enet2" };
static const int g_enet_common_irqs[ENET_INSTANCES] = { ENET0_COMMON_IRQ, ENET1_COMMON_IRQ };

static uint32_t enet_size_mask(unsigned size, unsigned shift)
{
    if (size >= 4U) {
        return 0xFFFFFFFFU;
    }

    return ((1U << (size * 8U)) - 1U) << shift;
}

static bool enet_decode_offset(EnetState *s, uint64_t addr, uint32_t *offset)
{
    if (addr >= ENET0_BASE && addr < (ENET0_BASE + ENET_WINDOW_SIZE)) {
        s->base = ENET0_BASE;
        *offset = (uint32_t)(addr - ENET0_BASE);
        return true;
    }

    if (addr >= ENET1_BASE && addr < (ENET1_BASE + ENET_WINDOW_SIZE)) {
        s->base = ENET1_BASE;
        *offset = (uint32_t)(addr - ENET1_BASE);
        return true;
    }

    if (s->base != 0U && addr >= s->base && addr < (s->base + ENET_WINDOW_SIZE)) {
        *offset = (uint32_t)(addr - s->base);
        return true;
    }

    return false;
}

static void enet_reset_tja1103_port(EnetState *s, uint32_t phy)
{
    EnetTja1103State *phy_state = &s->tja1103[phy & 0x1FU];

    memset(phy_state, 0, sizeof(*phy_state));
    phy_state->bt1_ctrl = 0x0000U;
    phy_state->always_accessible = 0x0000U;
}

static void enet_reset_phy_port(EnetState *s, uint32_t phy)
{
    memset(s->phy_regs[phy], 0, sizeof(s->phy_regs[phy]));

    /* Clause 22 ID/status values compatible with Zephyr's TJA1103 driver. */
    s->phy_regs[phy][0] = 0x1140U;
    s->phy_regs[phy][1] = 0x796DU;
    s->phy_regs[phy][2] = TJA1103_PHYID1;
    s->phy_regs[phy][3] = TJA1103_PHYID2;
    s->phy_regs[phy][4] = 0x01E1U;
    s->phy_regs[phy][5] = 0xC1E1U;
    s->phy_regs[phy][31] = 0x0007U;

    enet_reset_tja1103_port(s, phy);
}

static void enet_init_phy(EnetState *s)
{
    uint32_t phy;
    uint32_t devad;

    if (s->phy_initialized) {
        return;
    }

    memset(s->c45_addr, 0, sizeof(s->c45_addr));
    memset(s->c45_data, 0, sizeof(s->c45_data));
    memset(s->tja1103, 0, sizeof(s->tja1103));

    for (phy = 0; phy < 32U; phy++) {
        enet_reset_phy_port(s, phy);
        for (devad = 0; devad < 32U; devad++) {
            s->c45_addr[phy][devad] = 0U;
            s->c45_data[phy][devad] = 0U;
        }
    }

    s->phy_initialized = true;
}

static void enet_reset_regs(EnetState *s)
{
    memset(s->regs, 0, sizeof(s->regs));
    s->rx_active = false;
    s->rx_q_head = 0U;
    s->rx_q_tail = 0U;
    s->rx_q_count = 0U;
    s->tx_next_bd = 0U;
    s->rx_next_bd = 0U;
}

static uint16_t enet_phy_read_c22(EnetState *s, uint8_t phy, uint8_t reg)
{
    enet_init_phy(s);
    return s->phy_regs[phy & 0x1FU][reg & 0x1FU];
}

static void enet_phy_write_c22(EnetState *s, uint8_t phy, uint8_t reg, uint16_t value)
{
    uint8_t pa = phy & 0x1FU;
    uint8_t ra = reg & 0x1FU;

    enet_init_phy(s);

    switch (ra) {
    case 0:
        if ((value & 0x8000U) != 0U) {
            enet_reset_phy_port(s, pa);
        } else {
            s->phy_regs[pa][ra] = value & 0x7FFFU;
        }
        break;
    case 1:
    case 2:
    case 3:
        /* Keep status/ID registers stable. */
        break;
    default:
        s->phy_regs[pa][ra] = value;
        break;
    }
}

static bool enet_tja1103_link_up(EnetState *s, uint8_t phy)
{
    EnetTja1103State *phy_state = &s->tja1103[phy & 0x1FU];

    return (phy_state->device_control &
            (TJA1103_DEVICE_CONTROL_GLOBAL_CFG_EN |
             TJA1103_DEVICE_CONTROL_SUPER_CFG_EN)) ==
               (TJA1103_DEVICE_CONTROL_GLOBAL_CFG_EN |
                TJA1103_DEVICE_CONTROL_SUPER_CFG_EN) &&
           (phy_state->phy_control & TJA1103_PHY_CONTROL_CFG_EN) != 0U;
}

static void enet_tja1103_latch_link_irq(EnetState *s, uint8_t phy)
{
    EnetTja1103State *phy_state = &s->tja1103[phy & 0x1FU];

    if (!enet_tja1103_link_up(s, phy)) {
        return;
    }

    if ((phy_state->irq_en & TJA1103_PHY_FUNC_IRQ_LINK_EVENT_EN) != 0U) {
        phy_state->irq_mstatus |= TJA1103_PHY_FUNC_IRQ_LINK_EVENT;
    }

    if ((phy_state->irq_en & TJA1103_PHY_FUNC_IRQ_LINK_AVAIL_EN) != 0U) {
        phy_state->irq_mstatus |= TJA1103_PHY_FUNC_IRQ_LINK_AVAIL;
    }
}

static uint16_t enet_phy_read_c45(EnetState *s, uint8_t phy, uint8_t devad)
{
    uint8_t pa = phy & 0x1FU;
    uint8_t da = devad & 0x1FU;
    uint16_t reg;
    EnetTja1103State *phy_state;

    enet_init_phy(s);

    reg = s->c45_addr[pa][da];
    phy_state = &s->tja1103[pa];

    if (da == TJA1103_MMD_PMAPMD) {
        switch (reg) {
        case 0x0002U:
            return TJA1103_PHYID1;
        case 0x0003U:
            return TJA1103_PHYID2;
        case TJA1103_PMA_PMD_BT1_CTRL:
            return phy_state->bt1_ctrl;
        default:
            break;
        }
    }

    if (da == TJA1103_MMD_VENDOR_SPECIFIC1) {
        switch (reg) {
        case TJA1103_DEVICE_CONTROL:
            return phy_state->device_control;
        case TJA1103_ALWAYS_ACCESSIBLE:
            return phy_state->always_accessible;
        case TJA1103_PHY_FUNC_IRQ_ACK:
            return 0U;
        case TJA1103_PHY_FUNC_IRQ_EN:
            return phy_state->irq_en;
        case TJA1103_PHY_FUNC_IRQ_MSTATUS:
            return phy_state->irq_mstatus;
        case TJA1103_PHY_CONTROL:
            return phy_state->phy_control;
        case TJA1103_PHY_STATUS:
            return enet_tja1103_link_up(s, pa) ? TJA1103_PHY_STATUS_LINK_STAT : 0U;
        default:
            break;
        }
    }

    return s->c45_data[pa][da];
}

static void enet_phy_write_c45(EnetState *s, uint8_t phy, uint8_t devad, uint16_t value)
{
    uint8_t pa = phy & 0x1FU;
    uint8_t da = devad & 0x1FU;
    uint16_t reg;
    EnetTja1103State *phy_state;

    enet_init_phy(s);

    reg = s->c45_addr[pa][da];
    phy_state = &s->tja1103[pa];

    if (da == TJA1103_MMD_PMAPMD) {
        switch (reg) {
        case TJA1103_PMA_PMD_BT1_CTRL:
            phy_state->bt1_ctrl = value;
            return;
        default:
            break;
        }
    }

    if (da == TJA1103_MMD_VENDOR_SPECIFIC1) {
        switch (reg) {
        case TJA1103_DEVICE_CONTROL:
            phy_state->device_control = value;
            enet_tja1103_latch_link_irq(s, pa);
            return;
        case TJA1103_ALWAYS_ACCESSIBLE:
            phy_state->always_accessible &= (uint16_t)~value;
            return;
        case TJA1103_PHY_FUNC_IRQ_ACK:
            phy_state->irq_mstatus &= (uint16_t)~value;
            return;
        case TJA1103_PHY_FUNC_IRQ_EN:
            phy_state->irq_en = value;
            enet_tja1103_latch_link_irq(s, pa);
            return;
        case TJA1103_PHY_CONTROL:
            phy_state->phy_control = value;
            enet_tja1103_latch_link_irq(s, pa);
            return;
        case TJA1103_PHY_STATUS:
            return;
        default:
            break;
        }
    }

    s->c45_data[pa][da] = value;
}

static uint16_t enet_load_le16(const uint8_t *buf)
{
    return (uint16_t)buf[0] | ((uint16_t)buf[1] << 8);
}

static uint32_t enet_load_le32(const uint8_t *buf)
{
    return (uint32_t)buf[0] |
           ((uint32_t)buf[1] << 8) |
           ((uint32_t)buf[2] << 16) |
           ((uint32_t)buf[3] << 24);
}

static void enet_store_le16(uint8_t *buf, uint16_t value)
{
    buf[0] = (uint8_t)(value & 0xFFU);
    buf[1] = (uint8_t)((value >> 8) & 0xFFU);
}

static uint16_t enet_load_be16(const uint8_t *buf)
{
    return ((uint16_t)buf[0] << 8) | (uint16_t)buf[1];
}

static void enet_store_be16(uint8_t *buf, uint16_t value)
{
    buf[0] = (uint8_t)((value >> 8) & 0xFFU);
    buf[1] = (uint8_t)(value & 0xFFU);
}

static uint32_t enet_checksum_accumulate(uint32_t sum, const uint8_t *buf, uint32_t len)
{
    uint32_t i;

    for (i = 0U; (i + 1U) < len; i += 2U) {
        sum += ((uint32_t)buf[i] << 8) | (uint32_t)buf[i + 1U];
    }

    if ((len & 1U) != 0U) {
        sum += (uint32_t)buf[len - 1U] << 8;
    }

    return sum;
}

static uint16_t enet_checksum_finalize(uint32_t sum)
{
    while ((sum >> 16) != 0U) {
        sum = (sum & 0xFFFFU) + (sum >> 16);
    }

    return (uint16_t)(~sum);
}

static uint32_t enet_ipv4_pseudo_sum(const uint8_t *ip, uint8_t protocol, uint16_t l4_len)
{
    uint32_t sum = 0U;

    sum = enet_checksum_accumulate(sum, &ip[12], 8U);
    sum += (uint32_t)protocol;
    sum += (uint32_t)l4_len;

    return sum;
}

static void enet_repair_icmpv4_checksum(uint8_t *l4, uint32_t l4_len)
{
    uint16_t current;
    uint16_t computed;

    if (l4_len < ENET_ICMP_MIN_LEN) {
        return;
    }

    current = enet_load_be16(&l4[2]);
    enet_store_be16(&l4[2], 0U);
    computed = enet_checksum_finalize(enet_checksum_accumulate(0U, l4, l4_len));

    if (current == 0U || current != computed) {
        enet_store_be16(&l4[2], computed);
    } else {
        enet_store_be16(&l4[2], current);
    }
}

static void enet_repair_udpv4_checksum(const uint8_t *ip, uint8_t *l4, uint32_t l4_len)
{
    uint16_t udp_len;
    uint16_t current;
    uint16_t computed;
    uint32_t sum;

    if (l4_len < ENET_UDP_HDR_LEN) {
        return;
    }

    udp_len = enet_load_be16(&l4[4]);
    if (udp_len < ENET_UDP_HDR_LEN || udp_len > l4_len) {
        return;
    }

    current = enet_load_be16(&l4[6]);
    enet_store_be16(&l4[6], 0U);

    sum = enet_ipv4_pseudo_sum(ip, 17U, udp_len);
    sum = enet_checksum_accumulate(sum, l4, udp_len);
    computed = enet_checksum_finalize(sum);
    if (computed == 0U) {
        computed = 0xFFFFU;
    }

    if (current == 0U || current != computed) {
        enet_store_be16(&l4[6], computed);
    } else {
        enet_store_be16(&l4[6], current);
    }
}

static void enet_repair_tcpv4_checksum(const uint8_t *ip, uint8_t *l4, uint32_t l4_len)
{
    uint8_t header_len;
    uint16_t current;
    uint16_t computed;
    uint32_t sum;

    if (l4_len < ENET_TCP_MIN_HDR_LEN) {
        return;
    }

    header_len = (uint8_t)((l4[12] >> 4) * 4U);
    if (header_len < ENET_TCP_MIN_HDR_LEN || header_len > l4_len) {
        return;
    }

    current = enet_load_be16(&l4[16]);
    enet_store_be16(&l4[16], 0U);

    sum = enet_ipv4_pseudo_sum(ip, 6U, (uint16_t)l4_len);
    sum = enet_checksum_accumulate(sum, l4, l4_len);
    computed = enet_checksum_finalize(sum);

    if (current == 0U || current != computed) {
        enet_store_be16(&l4[16], computed);
    } else {
        enet_store_be16(&l4[16], current);
    }
}

static void enet_repair_ipv4_tx_checksums(uint8_t *frame, uint32_t frame_len)
{
    uint8_t *ip;
    uint8_t *l4;
    uint16_t ether_type;
    uint16_t total_len;
    uint16_t frag_field;
    uint16_t ip_hdr_checksum;
    uint32_t ip_header_len;
    uint32_t l4_len;

    if (frame == NULL || frame_len < (ENET_ETH_HDR_LEN + ENET_IPV4_MIN_HDR_LEN)) {
        return;
    }

    ether_type = enet_load_be16(&frame[12]);
    if (ether_type != ENET_ETHERTYPE_IPV4) {
        return;
    }

    ip = &frame[ENET_ETH_HDR_LEN];
    if ((ip[0] >> 4) != 4U) {
        return;
    }

    ip_header_len = ((uint32_t)ip[0] & 0x0FU) * 4U;
    if (ip_header_len < ENET_IPV4_MIN_HDR_LEN) {
        return;
    }

    if (frame_len < (ENET_ETH_HDR_LEN + ip_header_len)) {
        return;
    }

    total_len = enet_load_be16(&ip[2]);
    if (total_len < ip_header_len) {
        return;
    }

    if ((uint32_t)total_len > (frame_len - ENET_ETH_HDR_LEN)) {
        return;
    }

    enet_store_be16(&ip[10], 0U);
    ip_hdr_checksum = enet_checksum_finalize(enet_checksum_accumulate(0U, ip, ip_header_len));
    enet_store_be16(&ip[10], ip_hdr_checksum);

    l4_len = (uint32_t)total_len - ip_header_len;
    if (l4_len == 0U) {
        return;
    }

    frag_field = enet_load_be16(&ip[6]);
    if ((frag_field & ENET_IPV4_FRAG_MASK) != 0U) {
        return;
    }

    l4 = &ip[ip_header_len];
    switch (ip[9]) {
    case 1U:
        enet_repair_icmpv4_checksum(l4, l4_len);
        break;
    case 6U:
        enet_repair_tcpv4_checksum(ip, l4, l4_len);
        break;
    case 17U:
        enet_repair_udpv4_checksum(ip, l4, l4_len);
        break;
    default:
        break;
    }
}

static bool enet_write_rx_bd_meta(uint32_t bd_addr, uint16_t length, uint16_t control)
{
    uint8_t raw[4];

    enet_store_le16(&raw[0], length);
    enet_store_le16(&raw[2], control);

    return qemu_plugin_write_memory((unsigned long long)bd_addr, raw, sizeof(raw)) == 0;
}

static bool enet_read_tx_bd(uint32_t bd_addr, EnetTxBd *bd)
{
    uint8_t raw[ENET_TX_BD_SIZE];

    if (qemu_plugin_read_memory((unsigned long long)bd_addr, raw, sizeof(raw)) != 0) {
        return false;
    }

    bd->length = enet_load_le16(&raw[0]);
    bd->control = enet_load_le16(&raw[2]);
    bd->buffer = enet_load_le32(&raw[4]);
    return true;
}

static bool enet_write_tx_bd_control(uint32_t bd_addr, uint16_t control)
{
    uint8_t raw[2];

    enet_store_le16(raw, control);
    return qemu_plugin_write_memory((unsigned long long)(bd_addr + 2U), raw, sizeof(raw)) == 0;
}

static uint32_t enet_advance_tx_bd(uint32_t ring_base, uint32_t bd_addr, uint16_t control)
{
    if ((control & ENET_TX_BD_WRAP_MASK) != 0U) {
        return ring_base;
    }

    return bd_addr + ENET_TX_BD_SIZE;
}

static void enet_raise_common_irq_if_pending(EnetState *s)
{
    uint32_t pending;

    if (s->common_irq < 0) {
        return;
    }

    pending = s->regs[ENET_EIR >> 2] & s->regs[ENET_EIMR >> 2];
    if (pending != 0U) {
        qemu_plugin_raise_irq(s->common_irq + 16, false);
    }
}

static uint32_t enet_advance_rx_bd(uint32_t ring_base, uint32_t bd_addr, uint16_t control)
{
    if ((control & ENET_RX_BD_WRAP_MASK) != 0U) {
        return ring_base;
    }

    return bd_addr + ENET_RX_BD_SIZE;
}

static bool enet_queue_rx_frame(EnetState *s, const uint8_t *data, uint32_t len)
{
    EnetPendingFrame *slot;

    if (s == NULL || data == NULL || len == 0U || len > ENET_RX_FRAME_MAX_COPY ||
        s->rx_q_count >= ENET_RX_QUEUE_DEPTH) {
        return false;
    }

    slot = &s->rx_queue[s->rx_q_tail];
    slot->len = (uint16_t)len;
    memcpy(slot->data, data, len);

    s->rx_q_tail++;
    if (s->rx_q_tail >= ENET_RX_QUEUE_DEPTH) {
        s->rx_q_tail = 0U;
    }
    s->rx_q_count++;

    return true;
}

static void enet_pop_rx_frame(EnetState *s)
{
    if (s == NULL || s->rx_q_count == 0U) {
        return;
    }

    s->rx_q_head++;
    if (s->rx_q_head >= ENET_RX_QUEUE_DEPTH) {
        s->rx_q_head = 0U;
    }
    s->rx_q_count--;
}

static void enet_poll_tap(EnetState *s)
{
    uint8_t frame[ENET_RX_FRAME_MAX_COPY];
    int len;

    if (s == NULL || s->tap_fd < 0) {
        return;
    }

    do {
        len = api_tap_recv_nonblock(s->tap_fd, frame, sizeof(frame));
        if (len > 0) {
            (void)enet_queue_rx_frame(s, frame, (uint32_t)len);
        }
    } while (len > 0);
}

static bool enet_rx_is_configured(EnetState *s)
{
    return s != NULL &&
           s->rx_active &&
           s->regs[ENET_RDSR >> 2] != 0U &&
           s->regs[ENET_MRBR >> 2] != 0U;
}

static bool enet_try_inject_rx_frame(EnetState *s, const uint8_t *frame, uint16_t frame_len)
{
    struct EnetRxSlot {
        uint32_t bd_addr;
        uint32_t buffer;
        uint16_t control;
        uint16_t chunk;
    } slots[ENET_RX_SCAN_LIMIT];
    uint32_t ring_base;
    uint32_t buf_size;
    uint32_t slot_count;
    uint32_t cur;
    uint32_t remaining;
    uint32_t offset;
    uint32_t i;
    EnetTxBd bd;

    if (s == NULL || frame == NULL || frame_len == 0U) {
        return false;
    }

    ring_base = s->regs[ENET_RDSR >> 2];
    buf_size = s->regs[ENET_MRBR >> 2] & 0xFFFFU;
    if (ring_base == 0U || buf_size == 0U) {
        return false;
    }

    slot_count = ((uint32_t)frame_len + buf_size - 1U) / buf_size;
    if (slot_count == 0U || slot_count > ENET_RX_SCAN_LIMIT) {
        return false;
    }

    cur = (s->rx_next_bd != 0U) ? s->rx_next_bd : ring_base;
    remaining = frame_len;

    for (i = 0; i < slot_count; i++) {
        uint32_t chunk = remaining > buf_size ? buf_size : remaining;

        if (!enet_read_tx_bd(cur, &bd)) {
            return false;
        }

        if ((bd.control & ENET_RX_BD_EMPTY_MASK) == 0U || bd.buffer == 0U) {
            return false;
        }

        slots[i].bd_addr = cur;
        slots[i].buffer = bd.buffer;
        slots[i].control = bd.control;
        slots[i].chunk = (uint16_t)chunk;

        cur = enet_advance_rx_bd(ring_base, cur, bd.control);
        remaining -= chunk;
    }

    offset = 0U;
    for (i = 0; i < slot_count; i++) {
        if (qemu_plugin_write_memory((unsigned long long)slots[i].buffer,
                                     (uint8_t *)&frame[offset],
                                     (int)slots[i].chunk) != 0) {
            return false;
        }
        offset += slots[i].chunk;
    }

    for (i = 0; i < slot_count; i++) {
        uint16_t control = (uint16_t)(slots[i].control & ENET_RX_BD_WRAP_MASK);
        uint16_t length = (i + 1U == slot_count) ? frame_len : slots[i].chunk;

        if (i + 1U == slot_count) {
            control |= ENET_RX_BD_LAST_MASK;
        }

        if (!enet_write_rx_bd_meta(slots[i].bd_addr, length, control)) {
            return false;
        }
    }

    s->rx_next_bd = cur;
    s->regs[ENET_EIR >> 2] |= (ENET_EIR_RXF_MASK | ENET_EIR_RXB_MASK);
    enet_raise_common_irq_if_pending(s);
    return true;
}

static void enet_pump_rx(EnetState *s)
{
    while (enet_rx_is_configured(s) && s->rx_q_count != 0U) {
        EnetPendingFrame *slot = &s->rx_queue[s->rx_q_head];

        if (!enet_try_inject_rx_frame(s, slot->data, slot->len)) {
            return;
        }

        enet_pop_rx_frame(s);
    }
}

static void enet_service_rx(EnetState *s)
{
    enet_poll_tap(s);
    enet_pump_rx(s);
}

static void enet_rx_timer_cb(void *opaque)
{
    EnetState *s = (EnetState *)opaque;

    enet_service_rx(s);
}

static bool enet_find_ready_tx_bd(EnetState *s, uint32_t ring_base, uint32_t *bd_addr, EnetTxBd *bd)
{
    uint32_t start;
    uint32_t cur;
    unsigned steps;

    start = (s->tx_next_bd != 0U) ? s->tx_next_bd : ring_base;
    cur = start;

    for (steps = 0; steps < ENET_TX_SCAN_LIMIT; steps++) {
        if (!enet_read_tx_bd(cur, bd)) {
            return false;
        }

        if ((bd->control & ENET_TX_BD_READY_MASK) != 0U) {
            *bd_addr = cur;
            return true;
        }

        cur = enet_advance_tx_bd(ring_base, cur, bd->control);
        if (cur == start) {
            break;
        }
    }

    return false;
}

static void enet_complete_tx(EnetState *s)
{
    uint32_t ring_base = s->regs[ENET_TDSR >> 2];
    uint32_t cur;
    EnetTxBd bd;
    uint8_t frame[ENET_TX_FRAME_MAX_COPY];
    uint32_t frame_len = 0U;
    bool frame_ok = true;
    bool completed = false;
    unsigned steps;

    if (ring_base == 0U) {
        return;
    }

    if (!enet_find_ready_tx_bd(s, ring_base, &cur, &bd)) {
        return;
    }

    for (steps = 0; steps < ENET_TX_SCAN_LIMIT; steps++) {
        uint16_t control = bd.control;
        uint32_t next = enet_advance_tx_bd(ring_base, cur, control);

        if ((control & ENET_TX_BD_READY_MASK) == 0U) {
            break;
        }

        if (frame_ok && bd.length != 0U) {
            if (bd.buffer == 0U || frame_len > ENET_TX_FRAME_MAX_COPY ||
                (uint32_t)bd.length > (ENET_TX_FRAME_MAX_COPY - frame_len)) {
                frame_ok = false;
            } else if (qemu_plugin_read_memory((unsigned long long)bd.buffer,
                                               &frame[frame_len], (int)bd.length) != 0) {
                frame_ok = false;
            } else {
                frame_len += (uint32_t)bd.length;
            }
        }

        if (!enet_write_tx_bd_control(cur, (uint16_t)(control & ~ENET_TX_BD_READY_MASK))) {
            return;
        }

        completed = true;
        cur = next;

        if ((control & ENET_TX_BD_LAST_MASK) != 0U) {
            break;
        }

        if (!enet_read_tx_bd(cur, &bd)) {
            frame_ok = false;
            break;
        }
    }

    if (!completed) {
        return;
    }

    s->tx_next_bd = cur;

    if (frame_ok && frame_len != 0U && s->tap_fd >= 0) {
        enet_repair_ipv4_tx_checksums(frame, frame_len);
        (void)api_tap_send(s->tap_fd, frame, (int)frame_len);
    }

    s->regs[ENET_EIR >> 2] |= (ENET_EIR_TXF_MASK | ENET_EIR_TXB_MASK);
    enet_raise_common_irq_if_pending(s);
}

static void enet_complete_mdio(EnetState *s)
{
    s->regs[ENET_EIR >> 2] |= ENET_EIR_MII_MASK;
    enet_raise_common_irq_if_pending(s);
}

static void enet_handle_mmfr(EnetState *s, uint32_t frame)
{
    uint8_t st = (uint8_t)((frame >> ENET_MMFR_ST_SHIFT) & 0x3U);
    uint8_t op = (uint8_t)((frame >> ENET_MMFR_OP_SHIFT) & 0x3U);
    uint8_t pa = (uint8_t)((frame >> ENET_MMFR_PA_SHIFT) & 0x1FU);
    uint8_t ra = (uint8_t)((frame >> ENET_MMFR_RA_SHIFT) & 0x1FU);
    uint16_t data = (uint16_t)(frame & ENET_MMFR_DATA_MASK);
    uint32_t result = frame;

    enet_init_phy(s);

    if (st == 0x1U) {
        /* Clause 22. */
        if (op == 0x1U) {
            enet_phy_write_c22(s, pa, ra, data);
        } else if (op == 0x2U) {
            result = (frame & ~ENET_MMFR_DATA_MASK) | enet_phy_read_c22(s, pa, ra);
        }
    } else {
        /* Clause 45. */
        if (op == 0x0U) {
            s->c45_addr[pa][ra] = data;
        } else if (op == 0x1U) {
            enet_phy_write_c45(s, pa, ra, data);
        } else {
            result = (frame & ~ENET_MMFR_DATA_MASK) | enet_phy_read_c45(s, pa, ra);
            if (op == 0x2U) {
                s->c45_addr[pa][ra]++;
            }
        }
    }

    s->regs[ENET_MMFR >> 2] = result;
    enet_complete_mdio(s);
}

static uint32_t enet_read_word(EnetState *s, uint32_t aligned_offset)
{
    if (aligned_offset >= ENET_WINDOW_SIZE) {
        return 0;
    }

    switch (aligned_offset) {
    case ENET_RDAR:
    case ENET_TDAR:
        return 0U;
    default:
        return s->regs[aligned_offset >> 2];
    }
}

static void enet_write_word(EnetState *s, uint32_t aligned_offset, uint32_t value)
{
    if (aligned_offset >= ENET_WINDOW_SIZE) {
        return;
    }

    switch (aligned_offset) {
    case ENET_EIR:
        s->regs[ENET_EIR >> 2] &= ~value;
        return;
    case ENET_EIMR:
        s->regs[ENET_EIMR >> 2] = value;
        enet_raise_common_irq_if_pending(s);
        return;
    case ENET_ECR:
        if ((value & ENET_ECR_RESET_MASK) != 0U) {
            enet_reset_regs(s);
            s->regs[ENET_ECR >> 2] = value & ~ENET_ECR_RESET_MASK;
        } else {
            s->regs[ENET_ECR >> 2] = value;
        }
        return;
    case ENET_MMFR:
        enet_handle_mmfr(s, value);
        return;
    case ENET_RDSR:
        s->regs[ENET_RDSR >> 2] = value;
        s->rx_next_bd = value;
        return;
    case ENET_TDSR:
        s->regs[ENET_TDSR >> 2] = value;
        s->tx_next_bd = value;
        return;
    case ENET_MRBR:
        s->regs[ENET_MRBR >> 2] = value;
        return;
    case ENET_RDAR:
        s->rx_active = true;
        s->regs[aligned_offset >> 2] = 0U;
        return;
    case ENET_TDAR:
        s->regs[aligned_offset >> 2] = 0U;
        enet_complete_tx(s);
        return;
    default:
        s->regs[aligned_offset >> 2] = value;
        return;
    }
}

void* enet_init(ConfigSection* model_info)
{
    unsigned idx = g_enet_count;
    EnetState *s;

    (void)model_info;

    if (idx >= ENET_INSTANCES) {
        idx = ENET_INSTANCES - 1U;
    } else {
        g_enet_count++;
    }

    s = &g_enet[idx];
    memset(s, 0, sizeof(*s));
    s->common_irq = g_enet_common_irqs[idx];
    s->tap_fd = api_tap_init(g_enet_tap_names[idx]);
    s->rx_timer = qemu_plugin_timer_new_period_ns(enet_rx_timer_cb, s, ENET_RX_TIMER_PERIOD_NS);

    enet_init_phy(s);
    enet_reset_regs(s);

    return s;
}

uint64_t enet_read(void *opaque, uint64_t addr, unsigned size)
{
    EnetState *s = (EnetState *)opaque;
    uint32_t offset;
    uint32_t aligned_offset;
    uint32_t shift;
    uint32_t word;

    if (s == NULL) {
        s = &g_enet[0];
    }

    if (!enet_decode_offset(s, addr, &offset)) {
        return 0;
    }

    enet_service_rx(s);

    aligned_offset = offset & ~0x3U;
    shift = (offset & 0x3U) * 8U;
    word = enet_read_word(s, aligned_offset);

    if (size >= 4U) {
        return word;
    }

    return (word >> shift) & ((1ULL << (size * 8U)) - 1ULL);
}

void enet_write(void *opaque, uint64_t addr, uint64_t value, unsigned size)
{
    EnetState *s = (EnetState *)opaque;
    uint32_t offset;
    uint32_t aligned_offset;
    uint32_t shift;
    uint32_t current;
    uint32_t merged;
    uint32_t mask;

    if (s == NULL) {
        s = &g_enet[0];
    }

    if (!enet_decode_offset(s, addr, &offset)) {
        return;
    }

    aligned_offset = offset & ~0x3U;

    if (size >= 4U) {
        enet_write_word(s, aligned_offset, (uint32_t)value);
        enet_service_rx(s);
        return;
    }

    shift = (offset & 0x3U) * 8U;
    current = enet_read_word(s, aligned_offset);
    mask = enet_size_mask(size, shift);
    merged = (current & ~mask) | ((((uint32_t)value) << shift) & mask);

    enet_write_word(s, aligned_offset, merged);
    enet_service_rx(s);
}
