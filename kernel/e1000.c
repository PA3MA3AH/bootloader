#include "e1000.h"
#include "pci.h"
#include "kheap.h"
#include <stdint.h>

#define E1000_VENDOR_INTEL 0x8086

#define E1000_DEV_82540EM  0x100E
#define E1000_DEV_82545EM  0x100F
#define E1000_DEV_82543GC  0x1004
#define E1000_DEV_82574L   0x10D3

#define E1000_REG_CTRL     0x0000
#define E1000_REG_STATUS   0x0008
#define E1000_REG_EECD     0x0010
#define E1000_REG_EERD     0x0014
#define E1000_REG_ICR      0x00C0
#define E1000_REG_IMS      0x00D0
#define E1000_REG_IMC      0x00D8
#define E1000_REG_RCTL     0x0100
#define E1000_REG_TCTL     0x0400
#define E1000_REG_TIPG     0x0410

#define E1000_REG_RDBAL    0x2800
#define E1000_REG_RDBAH    0x2804
#define E1000_REG_RDLEN    0x2808
#define E1000_REG_RDH      0x2810
#define E1000_REG_RDT      0x2818

#define E1000_REG_TDBAL    0x3800
#define E1000_REG_TDBAH    0x3804
#define E1000_REG_TDLEN    0x3808
#define E1000_REG_TDH      0x3810
#define E1000_REG_TDT      0x3818

#define E1000_REG_RAL      0x5400
#define E1000_REG_RAH      0x5404

#define E1000_CTRL_RST     (1U << 26)

#define E1000_STATUS_LU    (1U << 1)

#define E1000_EERD_START   (1U << 0)
#define E1000_EERD_DONE    (1U << 4)

/* RCTL bits */
#define E1000_RCTL_EN         (1U << 1)
#define E1000_RCTL_BAM        (1U << 15)
#define E1000_RCTL_SECRC      (1U << 26)
#define E1000_RCTL_LBM_NONE   (0U << 6)
#define E1000_RCTL_RDMTS_HALF (0U << 8)
#define E1000_RCTL_BSIZE_2048 (0U << 16)

/* TCTL bits */
#define E1000_TCTL_EN         (1U << 1)
#define E1000_TCTL_PSP        (1U << 3)

/* TX descriptor bits */
#define E1000_TX_CMD_EOP      0x01
#define E1000_TX_CMD_IFCS     0x02
#define E1000_TX_CMD_RS       0x08
#define E1000_TX_STATUS_DD    0x01

#define E1000_RX_DESC_COUNT   32U
#define E1000_TX_DESC_COUNT   32U
#define E1000_RX_BUF_SIZE     2048U
#define E1000_TX_BUF_SIZE     2048U

typedef struct {
    uint64_t addr;
    uint16_t length;
    uint16_t checksum;
    uint8_t status;
    uint8_t errors;
    uint16_t special;
} __attribute__((packed)) E1000_RX_DESC;

typedef struct {
    uint64_t addr;
    uint16_t length;
    uint8_t cso;
    uint8_t cmd;
    uint8_t status;
    uint8_t css;
    uint16_t special;
} __attribute__((packed)) E1000_TX_DESC;

static E1000_RX_DESC *g_rx_desc = 0;
static E1000_TX_DESC *g_tx_desc = 0;
static uint8_t *g_rx_buffers = 0;
static uint8_t *g_tx_buffers = 0;

static int g_rings_initialized = 0;
static int g_device_initialized = 0;
static int g_device_present = 0;
static uint32_t g_tx_tail_index = 0;

static E1000_INFO g_e1000_state;

static void mmio_write32(uint32_t mmio_base, uint32_t reg, uint32_t value) {
    volatile uint32_t *addr = (volatile uint32_t*)(uintptr_t)(mmio_base + reg);
    *addr = value;
}

static uint32_t mmio_read32(uint32_t mmio_base, uint32_t reg) {
    volatile uint32_t *addr = (volatile uint32_t*)(uintptr_t)(mmio_base + reg);
    return *addr;
}

static void io_wait_cycles(void) {
    for (volatile uint32_t i = 0; i < 100000; i++) {
        __asm__ __volatile__("pause");
    }
}

static void memory_zero(void *ptr, uint32_t size) {
    uint8_t *p = (uint8_t*)ptr;
    for (uint32_t i = 0; i < size; i++) {
        p[i] = 0;
    }
}

static void memory_copy(void *dst, const void *src, uint32_t size) {
    uint8_t *d = (uint8_t*)dst;
    const uint8_t *s = (const uint8_t*)src;

    for (uint32_t i = 0; i < size; i++) {
        d[i] = s[i];
    }
}

static void store_u16_be(uint8_t *dst, uint16_t value) {
    dst[0] = (uint8_t)((value >> 8) & 0xFF);
    dst[1] = (uint8_t)(value & 0xFF);
}

static int e1000_supported_device(uint16_t vendor_id, uint16_t device_id) {
    if (vendor_id != E1000_VENDOR_INTEL) {
        return 0;
    }

    switch (device_id) {
        case E1000_DEV_82540EM:
        case E1000_DEV_82545EM:
        case E1000_DEV_82543GC:
        case E1000_DEV_82574L:
            return 1;
        default:
            return 0;
    }
}

static void e1000_zero_info(E1000_INFO *info) {
    if (!info) {
        return;
    }

    info->present = 0;
    info->bus = 0;
    info->device = 0;
    info->function = 0;
    info->vendor_id = 0;
    info->device_id = 0;
    info->pci_command = 0;
    info->pci_status = 0;
    info->bar0 = 0;
    info->mmio_base = 0;
    info->ctrl = 0;
    info->status = 0;
    info->eecd = 0;
    info->eerd = 0;
    info->icr = 0;
    info->ims = 0;
    info->imc = 0;
    info->rctl = 0;
    info->tctl = 0;
    info->tipg = 0;
    info->rdbal = 0;
    info->rdbah = 0;
    info->rdlen = 0;
    info->rdh = 0;
    info->rdt = 0;
    info->tdbal = 0;
    info->tdbah = 0;
    info->tdlen = 0;
    info->tdh = 0;
    info->tdt = 0;
    info->irq_line = 0;
    info->irq_pin = 0;
    info->mac[0] = 0;
    info->mac[1] = 0;
    info->mac[2] = 0;
    info->mac[3] = 0;
    info->mac[4] = 0;
    info->mac[5] = 0;
    info->link_up = 0;
    info->eeprom_ok = 0;
    info->rings_ready = 0;
    info->rx_desc_count = 0;
    info->tx_desc_count = 0;
}

static void e1000_copy_info(E1000_INFO *dst, const E1000_INFO *src) {
    if (!dst || !src) {
        return;
    }

    dst->present = src->present;
    dst->bus = src->bus;
    dst->device = src->device;
    dst->function = src->function;

    dst->vendor_id = src->vendor_id;
    dst->device_id = src->device_id;

    dst->pci_command = src->pci_command;
    dst->pci_status = src->pci_status;

    dst->bar0 = src->bar0;
    dst->mmio_base = src->mmio_base;

    dst->ctrl = src->ctrl;
    dst->status = src->status;

    dst->eecd = src->eecd;
    dst->eerd = src->eerd;
    dst->icr = src->icr;
    dst->ims = src->ims;
    dst->imc = src->imc;
    dst->rctl = src->rctl;
    dst->tctl = src->tctl;
    dst->tipg = src->tipg;

    dst->rdbal = src->rdbal;
    dst->rdbah = src->rdbah;
    dst->rdlen = src->rdlen;
    dst->rdh = src->rdh;
    dst->rdt = src->rdt;

    dst->tdbal = src->tdbal;
    dst->tdbah = src->tdbah;
    dst->tdlen = src->tdlen;
    dst->tdh = src->tdh;
    dst->tdt = src->tdt;

    dst->irq_line = src->irq_line;
    dst->irq_pin = src->irq_pin;

    dst->mac[0] = src->mac[0];
    dst->mac[1] = src->mac[1];
    dst->mac[2] = src->mac[2];
    dst->mac[3] = src->mac[3];
    dst->mac[4] = src->mac[4];
    dst->mac[5] = src->mac[5];

    dst->link_up = src->link_up;
    dst->eeprom_ok = src->eeprom_ok;

    dst->rings_ready = src->rings_ready;
    dst->rx_desc_count = src->rx_desc_count;
    dst->tx_desc_count = src->tx_desc_count;
}

static void e1000_store_state(const E1000_INFO *info) {
    if (!info) {
        return;
    }
    e1000_copy_info(&g_e1000_state, info);
}

static void e1000_load_state(E1000_INFO *info) {
    if (!info) {
        return;
    }
    e1000_copy_info(info, &g_e1000_state);
}

static int e1000_has_valid_state(void) {
    return g_device_present && g_e1000_state.present && g_e1000_state.mmio_base != 0;
}

int e1000_probe(E1000_INFO *info) {
    if (!info) {
        return 0;
    }

    if (e1000_has_valid_state()) {
        e1000_load_state(info);
        return 1;
    }

    e1000_zero_info(info);

    for (uint16_t bus = 0; bus < 256; bus++) {
        for (uint8_t device = 0; device < 32; device++) {
            uint8_t max_functions = 1;

            if (pci_config_read16((uint8_t)bus, device, 0, 0x00) == 0xFFFF) {
                continue;
            }

            {
                uint8_t header = pci_config_read8((uint8_t)bus, device, 0, 0x0E);
                if (header & 0x80) {
                    max_functions = 8;
                }
            }

            for (uint8_t function = 0; function < max_functions; function++) {
                PCI_DEVICE_INFO pci_info;

                if (!pci_probe_device((uint8_t)bus, device, function, &pci_info)) {
                    continue;
                }

                if (!e1000_supported_device(pci_info.vendor_id, pci_info.device_id)) {
                    continue;
                }

                info->present = 1;
                info->bus = pci_info.bus;
                info->device = pci_info.device;
                info->function = pci_info.function;
                info->vendor_id = pci_info.vendor_id;
                info->device_id = pci_info.device_id;
                info->pci_command = pci_info.command;
                info->pci_status = pci_info.status;
                info->bar0 = pci_info.bar[0];
                info->mmio_base = pci_info.bar[0] & ~0xFULL;
                info->irq_line = pci_info.irq_line;
                info->irq_pin = pci_info.irq_pin;

                g_device_present = 1;
                e1000_store_state(info);
                return 1;
            }
        }
    }

    return 0;
}

static void e1000_read_mac(E1000_INFO *info) {
    uint32_t ral;
    uint32_t rah;

    if (!info || !info->present || info->mmio_base == 0) {
        return;
    }

    ral = mmio_read32(info->mmio_base, E1000_REG_RAL);
    rah = mmio_read32(info->mmio_base, E1000_REG_RAH);

    info->mac[0] = (uint8_t)(ral & 0xFF);
    info->mac[1] = (uint8_t)((ral >> 8) & 0xFF);
    info->mac[2] = (uint8_t)((ral >> 16) & 0xFF);
    info->mac[3] = (uint8_t)((ral >> 24) & 0xFF);
    info->mac[4] = (uint8_t)(rah & 0xFF);
    info->mac[5] = (uint8_t)((rah >> 8) & 0xFF);
}

static int e1000_try_eeprom_read(E1000_INFO *info, uint8_t addr, uint16_t *value_out) {
    uint32_t request;
    uint32_t value;

    if (!info || !value_out || info->mmio_base == 0) {
        return 0;
    }

    request = E1000_EERD_START | ((uint32_t)addr << 8);
    mmio_write32(info->mmio_base, E1000_REG_EERD, request);

    for (uint32_t i = 0; i < 100000; i++) {
        value = mmio_read32(info->mmio_base, E1000_REG_EERD);
        if (value & E1000_EERD_DONE) {
            *value_out = (uint16_t)((value >> 16) & 0xFFFFU);
            return 1;
        }
    }

    return 0;
}

void e1000_refresh(E1000_INFO *info) {
    uint16_t eeprom_word;

    if (!info) {
        return;
    }

    if (!info->present) {
        if (!e1000_probe(info)) {
            return;
        }
    }

    if (info->mmio_base == 0) {
        return;
    }

    info->pci_command = pci_config_read16(info->bus, info->device, info->function, 0x04);
    info->pci_status  = pci_config_read16(info->bus, info->device, info->function, 0x06);

    info->ctrl   = mmio_read32(info->mmio_base, E1000_REG_CTRL);
    info->status = mmio_read32(info->mmio_base, E1000_REG_STATUS);
    info->eecd   = mmio_read32(info->mmio_base, E1000_REG_EECD);
    info->eerd   = mmio_read32(info->mmio_base, E1000_REG_EERD);
    info->icr    = mmio_read32(info->mmio_base, E1000_REG_ICR);
    info->ims    = mmio_read32(info->mmio_base, E1000_REG_IMS);
    info->imc    = mmio_read32(info->mmio_base, E1000_REG_IMC);
    info->rctl   = mmio_read32(info->mmio_base, E1000_REG_RCTL);
    info->tctl   = mmio_read32(info->mmio_base, E1000_REG_TCTL);
    info->tipg   = mmio_read32(info->mmio_base, E1000_REG_TIPG);

    info->rdbal  = mmio_read32(info->mmio_base, E1000_REG_RDBAL);
    info->rdbah  = mmio_read32(info->mmio_base, E1000_REG_RDBAH);
    info->rdlen  = mmio_read32(info->mmio_base, E1000_REG_RDLEN);
    info->rdh    = mmio_read32(info->mmio_base, E1000_REG_RDH);
    info->rdt    = mmio_read32(info->mmio_base, E1000_REG_RDT);

    info->tdbal  = mmio_read32(info->mmio_base, E1000_REG_TDBAL);
    info->tdbah  = mmio_read32(info->mmio_base, E1000_REG_TDBAH);
    info->tdlen  = mmio_read32(info->mmio_base, E1000_REG_TDLEN);
    info->tdh    = mmio_read32(info->mmio_base, E1000_REG_TDH);
    info->tdt    = mmio_read32(info->mmio_base, E1000_REG_TDT);

    info->link_up = (info->status & E1000_STATUS_LU) ? 1 : 0;
    info->eeprom_ok = e1000_try_eeprom_read(info, 0, &eeprom_word);

    info->rings_ready = g_rings_initialized;
    info->rx_desc_count = g_rings_initialized ? E1000_RX_DESC_COUNT : 0;
    info->tx_desc_count = g_rings_initialized ? E1000_TX_DESC_COUNT : 0;

    e1000_read_mac(info);
    e1000_store_state(info);
}

int e1000_init(E1000_INFO *info) {
    uint16_t command;

    if (!info) {
        return 0;
    }

    if (!info->present) {
        if (!e1000_probe(info)) {
            return 0;
        }
    }

    if (info->mmio_base == 0) {
        return 0;
    }

    command = pci_config_read16(info->bus, info->device, info->function, 0x04);
    command |= 0x0006;
    pci_config_write16(info->bus, info->device, info->function, 0x04, command);

    mmio_write32(info->mmio_base, E1000_REG_IMC, 0xFFFFFFFFU);
    (void)mmio_read32(info->mmio_base, E1000_REG_ICR);

    info->ctrl = mmio_read32(info->mmio_base, E1000_REG_CTRL);
    mmio_write32(info->mmio_base, E1000_REG_CTRL, info->ctrl | E1000_CTRL_RST);
    io_wait_cycles();

    g_device_initialized = 1;
    g_rings_initialized = 0;

    e1000_refresh(info);
    return 1;
}

static int e1000_setup_rx(E1000_INFO *info) {
    uint32_t ring_bytes = E1000_RX_DESC_COUNT * (uint32_t)sizeof(E1000_RX_DESC);

    if (!g_rx_desc) {
        g_rx_desc = (E1000_RX_DESC*)kcalloc(E1000_RX_DESC_COUNT, sizeof(E1000_RX_DESC));
    }

    if (!g_rx_buffers) {
        g_rx_buffers = (uint8_t*)kcalloc(E1000_RX_DESC_COUNT, E1000_RX_BUF_SIZE);
    }

    if (!g_rx_desc || !g_rx_buffers) {
        return 0;
    }

    memory_zero(g_rx_desc, ring_bytes);
    memory_zero(g_rx_buffers, E1000_RX_DESC_COUNT * E1000_RX_BUF_SIZE);

    for (uint32_t i = 0; i < E1000_RX_DESC_COUNT; i++) {
        g_rx_desc[i].addr = (uint64_t)(uintptr_t)(g_rx_buffers + (i * E1000_RX_BUF_SIZE));
        g_rx_desc[i].status = 0;
    }

    mmio_write32(info->mmio_base, E1000_REG_RDBAL, (uint32_t)(uintptr_t)g_rx_desc);
    mmio_write32(info->mmio_base, E1000_REG_RDBAH, 0);
    mmio_write32(info->mmio_base, E1000_REG_RDLEN, ring_bytes);
    mmio_write32(info->mmio_base, E1000_REG_RDH, 0);
    mmio_write32(info->mmio_base, E1000_REG_RDT, E1000_RX_DESC_COUNT - 1);

    mmio_write32(info->mmio_base,
                 E1000_REG_RCTL,
                 E1000_RCTL_EN |
                 E1000_RCTL_BAM |
                 E1000_RCTL_SECRC |
                 E1000_RCTL_LBM_NONE |
                 E1000_RCTL_RDMTS_HALF |
                 E1000_RCTL_BSIZE_2048);

    return 1;
}

static int e1000_setup_tx(E1000_INFO *info) {
    uint32_t ring_bytes = E1000_TX_DESC_COUNT * (uint32_t)sizeof(E1000_TX_DESC);

    if (!g_tx_desc) {
        g_tx_desc = (E1000_TX_DESC*)kcalloc(E1000_TX_DESC_COUNT, sizeof(E1000_TX_DESC));
    }

    if (!g_tx_buffers) {
        g_tx_buffers = (uint8_t*)kcalloc(E1000_TX_DESC_COUNT, E1000_TX_BUF_SIZE);
    }

    if (!g_tx_desc || !g_tx_buffers) {
        return 0;
    }

    memory_zero(g_tx_desc, ring_bytes);
    memory_zero(g_tx_buffers, E1000_TX_DESC_COUNT * E1000_TX_BUF_SIZE);

    for (uint32_t i = 0; i < E1000_TX_DESC_COUNT; i++) {
        g_tx_desc[i].addr = (uint64_t)(uintptr_t)(g_tx_buffers + (i * E1000_TX_BUF_SIZE));
        g_tx_desc[i].status = E1000_TX_STATUS_DD;
    }

    g_tx_tail_index = 0;

    mmio_write32(info->mmio_base, E1000_REG_TDBAL, (uint32_t)(uintptr_t)g_tx_desc);
    mmio_write32(info->mmio_base, E1000_REG_TDBAH, 0);
    mmio_write32(info->mmio_base, E1000_REG_TDLEN, ring_bytes);
    mmio_write32(info->mmio_base, E1000_REG_TDH, 0);
    mmio_write32(info->mmio_base, E1000_REG_TDT, 0);

    mmio_write32(info->mmio_base, E1000_REG_TIPG, 0x00602008U);
    mmio_write32(info->mmio_base,
                 E1000_REG_TCTL,
                 E1000_TCTL_EN |
                 E1000_TCTL_PSP |
                 (0x10U << 4) |
                 (0x40U << 12));

    return 1;
}

int e1000_init_rings(E1000_INFO *info) {
    if (!info) {
        return 0;
    }

    if (!g_device_initialized) {
        if (!e1000_init(info)) {
            return 0;
        }
    } else {
        if (!info->present && !e1000_probe(info)) {
            return 0;
        }
    }

    if (!g_rings_initialized) {
        if (!e1000_setup_rx(info)) {
            return 0;
        }

        if (!e1000_setup_tx(info)) {
            return 0;
        }

        g_rings_initialized = 1;
    }

    e1000_refresh(info);
    return 1;
}

int e1000_send_packet(E1000_INFO *info, const void *data, uint16_t length) {
    uint32_t index;
    uint32_t next_index;

    if (!info || !data) {
        return 0;
    }

    if (!info->present && !e1000_probe(info)) {
        return 0;
    }

    if (!g_rings_initialized) {
        if (!e1000_init_rings(info)) {
            return 0;
        }
    }

    if (length == 0 || length > E1000_TX_BUF_SIZE) {
        return 0;
    }

    index = g_tx_tail_index;
    next_index = (index + 1U) % E1000_TX_DESC_COUNT;

    if ((g_tx_desc[index].status & E1000_TX_STATUS_DD) == 0) {
        return 0;
    }

    memory_copy(g_tx_buffers + (index * E1000_TX_BUF_SIZE), data, length);

    g_tx_desc[index].length = length;
    g_tx_desc[index].cso = 0;
    g_tx_desc[index].cmd = E1000_TX_CMD_EOP | E1000_TX_CMD_IFCS | E1000_TX_CMD_RS;
    g_tx_desc[index].status = 0;
    g_tx_desc[index].css = 0;
    g_tx_desc[index].special = 0;

    mmio_write32(info->mmio_base, E1000_REG_TDT, next_index);

    for (uint32_t spin = 0; spin < 1000000U; spin++) {
        if (g_tx_desc[index].status & E1000_TX_STATUS_DD) {
            g_tx_tail_index = next_index;
            e1000_refresh(info);
            return 1;
        }
        __asm__ __volatile__("pause");
    }

    e1000_refresh(info);
    return 0;
}

int e1000_send_test_packet(E1000_INFO *info) {
    uint8_t frame[60];
    const char payload[] = "MyOS e1000 test packet";
    uint32_t payload_len = (uint32_t)(sizeof(payload) - 1U);
    uint32_t i;
    uint16_t ethertype = 0x88B5;

    if (!info) {
        return 0;
    }

    memory_zero(frame, sizeof(frame));

    for (i = 0; i < 6; i++) {
        frame[i] = 0xFF;
    }

    if (!info->present) {
        if (!e1000_probe(info)) {
            return 0;
        }
    }

    if (!g_rings_initialized) {
        if (!e1000_init_rings(info)) {
            return 0;
        }
    } else {
        e1000_refresh(info);
    }

    for (i = 0; i < 6; i++) {
        frame[6 + i] = info->mac[i];
    }

    frame[12] = (uint8_t)((ethertype >> 8) & 0xFF);
    frame[13] = (uint8_t)(ethertype & 0xFF);

    for (i = 0; i < payload_len && (14U + i) < sizeof(frame); i++) {
        frame[14 + i] = (uint8_t)payload[i];
    }

    return e1000_send_packet(info, frame, (uint16_t)sizeof(frame));
}

int e1000_send_arp_request(E1000_INFO *info, const uint8_t target_ip[4]) {
    uint8_t frame[60];
    const uint8_t sender_ip[4] = { 10, 0, 2, 15 };

    if (!info || !target_ip) {
        return 0;
    }

    if (!info->present) {
        if (!e1000_probe(info)) {
            return 0;
        }
    }

    if (!g_rings_initialized) {
        if (!e1000_init_rings(info)) {
            return 0;
        }
    } else {
        e1000_refresh(info);
    }

    memory_zero(frame, sizeof(frame));

    /* Ethernet header */
    frame[0] = 0xFF;
    frame[1] = 0xFF;
    frame[2] = 0xFF;
    frame[3] = 0xFF;
    frame[4] = 0xFF;
    frame[5] = 0xFF;

    frame[6]  = info->mac[0];
    frame[7]  = info->mac[1];
    frame[8]  = info->mac[2];
    frame[9]  = info->mac[3];
    frame[10] = info->mac[4];
    frame[11] = info->mac[5];

    store_u16_be(&frame[12], 0x0806);

    /* ARP payload */
    store_u16_be(&frame[14], 0x0001); /* Ethernet */
    store_u16_be(&frame[16], 0x0800); /* IPv4 */
    frame[18] = 6;                    /* hardware size */
    frame[19] = 4;                    /* protocol size */
    store_u16_be(&frame[20], 0x0001); /* request */

    frame[22] = info->mac[0];
    frame[23] = info->mac[1];
    frame[24] = info->mac[2];
    frame[25] = info->mac[3];
    frame[26] = info->mac[4];
    frame[27] = info->mac[5];

    frame[28] = sender_ip[0];
    frame[29] = sender_ip[1];
    frame[30] = sender_ip[2];
    frame[31] = sender_ip[3];

    frame[32] = 0x00;
    frame[33] = 0x00;
    frame[34] = 0x00;
    frame[35] = 0x00;
    frame[36] = 0x00;
    frame[37] = 0x00;

    frame[38] = target_ip[0];
    frame[39] = target_ip[1];
    frame[40] = target_ip[2];
    frame[41] = target_ip[3];

    return e1000_send_packet(info, frame, (uint16_t)sizeof(frame));
}

void e1000_print_info(CONSOLE *con, const E1000_INFO *info) {
    if (!con) {
        return;
    }

    if (!info || !info->present) {
        console_printf(con, "e1000: device not found\n");
        return;
    }

    console_printf(con, "e1000 info:\n");
    console_printf(con, "  pci:        %u:%u.%u\n",
                   (unsigned int)info->bus,
                   (unsigned int)info->device,
                   (unsigned int)info->function);
    console_printf(con, "  vendor id:  %x\n", (unsigned int)info->vendor_id);
    console_printf(con, "  device id:  %x\n", (unsigned int)info->device_id);
    console_printf(con, "  bar0:       %x\n", (unsigned int)info->bar0);
    console_printf(con, "  mmio base:  %x\n", (unsigned int)info->mmio_base);
    console_printf(con, "  ctrl:       %x\n", (unsigned int)info->ctrl);
    console_printf(con, "  status:     %x\n", (unsigned int)info->status);
    console_printf(con, "  link:       %s\n", info->link_up ? "up" : "down");
    console_printf(con, "  eeprom:     %s\n", info->eeprom_ok ? "ok" : "not responding");
    console_printf(con, "  rings:      %s\n", info->rings_ready ? "ready" : "not initialized");
    console_printf(con, "  mac:        %x:%x:%x:%x:%x:%x\n",
                   (unsigned int)info->mac[0],
                   (unsigned int)info->mac[1],
                   (unsigned int)info->mac[2],
                   (unsigned int)info->mac[3],
                   (unsigned int)info->mac[4],
                   (unsigned int)info->mac[5]);
}

void e1000_dump_registers(CONSOLE *con, const E1000_INFO *info) {
    if (!con) {
        return;
    }

    if (!info || !info->present) {
        console_printf(con, "e1000dump: device not found\n");
        return;
    }

    console_printf(con, "e1000 register dump:\n");
    console_printf(con, "  pci command: %x\n", (unsigned int)info->pci_command);
    console_printf(con, "  pci status:  %x\n", (unsigned int)info->pci_status);
    console_printf(con, "  irq line:    %x\n", (unsigned int)info->irq_line);
    console_printf(con, "  irq pin:     %x\n", (unsigned int)info->irq_pin);
    console_printf(con, "  CTRL:        %x\n", (unsigned int)info->ctrl);
    console_printf(con, "  STATUS:      %x\n", (unsigned int)info->status);
    console_printf(con, "  EECD:        %x\n", (unsigned int)info->eecd);
    console_printf(con, "  EERD:        %x\n", (unsigned int)info->eerd);
    console_printf(con, "  ICR:         %x\n", (unsigned int)info->icr);
    console_printf(con, "  IMS:         %x\n", (unsigned int)info->ims);
    console_printf(con, "  IMC:         %x\n", (unsigned int)info->imc);
    console_printf(con, "  RCTL:        %x\n", (unsigned int)info->rctl);
    console_printf(con, "  TCTL:        %x\n", (unsigned int)info->tctl);
    console_printf(con, "  TIPG:        %x\n", (unsigned int)info->tipg);
    console_printf(con, "  RDBAL:       %x\n", (unsigned int)info->rdbal);
    console_printf(con, "  RDBAH:       %x\n", (unsigned int)info->rdbah);
    console_printf(con, "  RDLEN:       %x\n", (unsigned int)info->rdlen);
    console_printf(con, "  RDH:         %x\n", (unsigned int)info->rdh);
    console_printf(con, "  RDT:         %x\n", (unsigned int)info->rdt);
    console_printf(con, "  TDBAL:       %x\n", (unsigned int)info->tdbal);
    console_printf(con, "  TDBAH:       %x\n", (unsigned int)info->tdbah);
    console_printf(con, "  TDLEN:       %x\n", (unsigned int)info->tdlen);
    console_printf(con, "  TDH:         %x\n", (unsigned int)info->tdh);
    console_printf(con, "  TDT:         %x\n", (unsigned int)info->tdt);
    console_printf(con, "  rings:       %s\n", info->rings_ready ? "ready" : "not initialized");
    console_printf(con, "  rx desc cnt: %u\n", (unsigned int)info->rx_desc_count);
    console_printf(con, "  tx desc cnt: %u\n", (unsigned int)info->tx_desc_count);
    console_printf(con, "  link:        %s\n", info->link_up ? "up" : "down");
    console_printf(con, "  eeprom:      %s\n", info->eeprom_ok ? "ok" : "not responding");
}
