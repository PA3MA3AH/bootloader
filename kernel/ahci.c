#include "ahci.h"
#include "pci.h"
#include "vmm.h"
#include "pmm.h"
#include "console.h"
#include "panic.h"
#include "block.h"

#include <stdint.h>
#include <stddef.h>

#define AHCI_CLASS_MASS_STORAGE 0x01
#define AHCI_SUBCLASS_SATA      0x06
#define AHCI_PROGIF_AHCI        0x01

#define PCI_COMMAND_IO          (1U << 0)
#define PCI_COMMAND_MEMORY      (1U << 1)
#define PCI_COMMAND_BUSMASTER   (1U << 2)

#define AHCI_PORT_SIG_ATA       0x00000101U
#define AHCI_PORT_SIG_ATAPI     0xEB140101U

#define AHCI_HBA_GHC_AE         (1U << 31)
#define AHCI_HBA_GHC_HR         (1U << 0)
#define AHCI_HBA_BOHC_BOS       (1U << 0)
#define AHCI_HBA_BOHC_OOS       (1U << 1)
#define AHCI_HBA_BOHC_BB        (1U << 4)

#define AHCI_PORT_CMD_ST        (1U << 0)
#define AHCI_PORT_CMD_SUD       (1U << 1)
#define AHCI_PORT_CMD_POD       (1U << 2)
#define AHCI_PORT_CMD_FRE       (1U << 4)
#define AHCI_PORT_CMD_FR        (1U << 14)
#define AHCI_PORT_CMD_CR        (1U << 15)

#define AHCI_PORT_TFD_ERR       (1U << 0)
#define AHCI_PORT_TFD_DRQ       (1U << 3)
#define AHCI_PORT_TFD_BSY       (1U << 7)

#define AHCI_PORT_DET_PRESENT   0x3
#define AHCI_PORT_IPM_ACTIVE    0x1

#define ATA_CMD_IDENTIFY        0xEC
#define ATA_CMD_READ_DMA_EXT    0x25
#define ATA_CMD_WRITE_DMA_EXT   0x35

#define ATA_DEV_BUSY            0x80
#define ATA_DEV_DRQ             0x08

#define FIS_TYPE_REG_H2D        0x27

#define ATA_IDENT_SERIAL_WORDS   10
#define ATA_IDENT_FW_WORDS       23
#define ATA_IDENT_MODEL_WORDS    27
#define ATA_IDENT_MAXLBA28_WORDS 60
#define ATA_IDENT_MAXLBA48_WORDS 100

#define AHCI_MMIO_MAP_SIZE      0x2000

typedef volatile struct {
    uint32_t clb;
    uint32_t clbu;
    uint32_t fb;
    uint32_t fbu;
    uint32_t is;
    uint32_t ie;
    uint32_t cmd;
    uint32_t reserved0;
    uint32_t tfd;
    uint32_t sig;
    uint32_t ssts;
    uint32_t sctl;
    uint32_t serr;
    uint32_t sact;
    uint32_t ci;
    uint32_t sntf;
    uint32_t fbs;
    uint32_t reserved1[11];
    uint32_t vendor[4];
} HBA_PORT;

typedef volatile struct {
    uint32_t cap;
    uint32_t ghc;
    uint32_t is;
    uint32_t pi;
    uint32_t vs;
    uint32_t ccc_ctl;
    uint32_t ccc_pts;
    uint32_t em_loc;
    uint32_t em_ctl;
    uint32_t cap2;
    uint32_t bohc;
    uint8_t  reserved[0xA0 - 0x2C];
    uint8_t  vendor[0x100 - 0xA0];
    HBA_PORT ports[32];
} HBA_MEM;

typedef struct __attribute__((packed)) {
    uint8_t  cfl:5;
    uint8_t  a:1;
    uint8_t  w:1;
    uint8_t  p:1;

    uint8_t  r:1;
    uint8_t  b:1;
    uint8_t  c:1;
    uint8_t  reserved0:1;
    uint8_t  pmp:4;

    uint16_t prdtl;
    volatile uint32_t prdbc;
    uint32_t ctba;
    uint32_t ctbau;
    uint32_t reserved1[4];
} HBA_CMD_HEADER;

typedef struct __attribute__((packed)) {
    uint32_t dba;
    uint32_t dbau;
    uint32_t reserved0;
    uint32_t dbc:22;
    uint32_t reserved1:9;
    uint32_t i:1;
} HBA_PRDT_ENTRY;

typedef struct __attribute__((packed)) {
    uint8_t  cfis[64];
    uint8_t  acmd[16];
    uint8_t  reserved[48];
    HBA_PRDT_ENTRY prdt_entry[1];
} HBA_CMD_TBL;

typedef struct __attribute__((packed)) {
    uint8_t fis_type;
    uint8_t pmport:4;
    uint8_t reserved0:3;
    uint8_t c:1;

    uint8_t command;
    uint8_t featurel;

    uint8_t lba0;
    uint8_t lba1;
    uint8_t lba2;
    uint8_t device;

    uint8_t lba3;
    uint8_t lba4;
    uint8_t lba5;
    uint8_t featureh;

    uint8_t countl;
    uint8_t counth;
    uint8_t icc;
    uint8_t control;

    uint8_t reserved1[4];
} FIS_REG_H2D;

typedef struct {
    int allocated;
    uint8_t port_no;

    void *clb_virt;
    uint64_t clb_phys;

    void *fb_virt;
    uint64_t fb_phys;

    void *ctba_virt;
    uint64_t ctba_phys;

    void *dma_virt;
    uint64_t dma_phys;
    uint32_t dma_bytes;
} AHCI_PORT_MEM;

static AHCI_INFO g_ahci;
static AHCI_PORT_MEM g_port_mem[AHCI_MAX_PORTS];
static uint8_t g_ahci_block_registered[AHCI_MAX_PORTS];

static void mem_zero(void *dst, uint64_t size) {
    uint8_t *p = (uint8_t*)dst;
    while (size--) {
        *p++ = 0;
    }
}

static void mem_copy(void *dst, const void *src, uint64_t size) {
    uint8_t *d = (uint8_t*)dst;
    const uint8_t *s = (const uint8_t*)src;
    while (size--) {
        *d++ = *s++;
    }
}

static void ata_string_from_words(char *out, uint32_t out_size, const uint16_t *words, uint32_t word_count) {
    uint32_t i;
    uint32_t pos = 0;

    if (!out || out_size == 0) {
        return;
    }

    for (i = 0; i < word_count && pos + 1 < out_size; i++) {
        uint16_t w = words[i];
        char hi = (char)(w >> 8);
        char lo = (char)(w & 0xFF);

        if (pos + 1 < out_size) out[pos++] = hi;
        if (pos + 1 < out_size) out[pos++] = lo;
    }

    out[pos] = '\0';

    while (pos > 0 && (out[pos - 1] == ' ' || out[pos - 1] == '\0')) {
        out[pos - 1] = '\0';
        pos--;
    }
}

static int ahci_find_controller(PCI_DEVICE_INFO *out_pci) {
    uint16_t bus;
    uint8_t device;
    uint8_t function;

    if (!out_pci) {
        return 0;
    }

    for (bus = 0; bus < 256; bus++) {
        for (device = 0; device < 32; device++) {
            uint8_t max_functions = 1;

            if (!pci_probe_device((uint8_t)bus, device, 0, 0)) {
                continue;
            }

            {
                uint8_t header = pci_config_read8((uint8_t)bus, device, 0, 0x0E);
                if (header & 0x80) {
                    max_functions = 8;
                }
            }

            for (function = 0; function < max_functions; function++) {
                PCI_DEVICE_INFO info;
                if (!pci_probe_device((uint8_t)bus, device, function, &info)) {
                    continue;
                }

                if (info.class_code == AHCI_CLASS_MASS_STORAGE &&
                    info.subclass   == AHCI_SUBCLASS_SATA &&
                    info.prog_if    == AHCI_PROGIF_AHCI) {
                    *out_pci = info;
                    return 1;
                }
            }
        }
    }

    return 0;
}

static void ahci_enable_pci_device(const PCI_DEVICE_INFO *pci) {
    uint16_t cmd;

    if (!pci) {
        return;
    }

    cmd = pci_config_read16(pci->bus, pci->device, pci->function, 0x04);
    cmd |= PCI_COMMAND_MEMORY | PCI_COMMAND_BUSMASTER;
    cmd &= (uint16_t)~PCI_COMMAND_IO;
    pci_config_write16(pci->bus, pci->device, pci->function, 0x04, cmd);
}

static int ahci_wait_clear(volatile uint32_t *reg, uint32_t mask, uint32_t spins) {
    while (spins--) {
        if (((*reg) & mask) == 0) {
            return 1;
        }
    }
    return 0;
}

static int ahci_wait_set(volatile uint32_t *reg, uint32_t mask, uint32_t spins) {
    while (spins--) {
        if (((*reg) & mask) == mask) {
            return 1;
        }
    }
    return 0;
}

static int ahci_port_is_device_present(HBA_PORT *port) {
    uint32_t ssts = port->ssts;
    uint8_t det = (uint8_t)(ssts & 0x0F);
    uint8_t ipm = (uint8_t)((ssts >> 8) & 0x0F);

    return (det == AHCI_PORT_DET_PRESENT) && (ipm == AHCI_PORT_IPM_ACTIVE);
}

static int ahci_port_signature_is_sata(HBA_PORT *port) {
    return port->sig == AHCI_PORT_SIG_ATA;
}

static int ahci_port_signature_is_atapi(HBA_PORT *port) {
    return port->sig == AHCI_PORT_SIG_ATAPI;
}

static void ahci_port_stop(HBA_PORT *port) {
    uint32_t cmd = port->cmd;
    cmd &= ~AHCI_PORT_CMD_ST;
    cmd &= ~AHCI_PORT_CMD_FRE;
    port->cmd = cmd;

    ahci_wait_clear(&port->cmd, AHCI_PORT_CMD_FR, 1000000);
    ahci_wait_clear(&port->cmd, AHCI_PORT_CMD_CR, 1000000);
}

static void ahci_port_start(HBA_PORT *port) {
    while (port->cmd & AHCI_PORT_CMD_CR) {
    }

    port->cmd |= AHCI_PORT_CMD_FRE;
    port->cmd |= AHCI_PORT_CMD_ST;
}

static int ahci_alloc_port_mem(uint32_t port_no) {
    AHCI_PORT_MEM *pm;
    uint64_t pages;

    if (port_no >= AHCI_MAX_PORTS) {
        return 0;
    }

    pm = &g_port_mem[port_no];
    if (pm->allocated) {
        return 1;
    }

    mem_zero(pm, sizeof(*pm));
    pm->port_no = (uint8_t)port_no;

    pm->clb_virt = pmm_alloc_page();
    pm->fb_virt  = pmm_alloc_page();
    pm->ctba_virt = pmm_alloc_page();

    pages = 1; /* 4 KiB DMA buffer, enough for 8 sectors */
    pm->dma_virt = pmm_alloc_pages(pages);

    if (!pm->clb_virt || !pm->fb_virt || !pm->ctba_virt || !pm->dma_virt) {
        return 0;
    }

    pm->clb_phys  = (uint64_t)(uintptr_t)pm->clb_virt;
    pm->fb_phys   = (uint64_t)(uintptr_t)pm->fb_virt;
    pm->ctba_phys = (uint64_t)(uintptr_t)pm->ctba_virt;
    pm->dma_phys  = (uint64_t)(uintptr_t)pm->dma_virt;
    pm->dma_bytes = 4096;

    mem_zero(pm->clb_virt, 4096);
    mem_zero(pm->fb_virt, 4096);
    mem_zero(pm->ctba_virt, 4096);
    mem_zero(pm->dma_virt, 4096);

    pm->allocated = 1;
    return 1;
}

static int ahci_port_rebase(HBA_PORT *port, uint32_t port_no) {
    HBA_CMD_HEADER *cmdhdr;
    AHCI_PORT_MEM *pm;

    if (!port || port_no >= AHCI_MAX_PORTS) {
        return 0;
    }

    if (!ahci_alloc_port_mem(port_no)) {
        return 0;
    }

    pm = &g_port_mem[port_no];

    ahci_port_stop(port);

    port->clb  = (uint32_t)(pm->clb_phys & 0xFFFFFFFFU);
    port->clbu = (uint32_t)(pm->clb_phys >> 32);
    port->fb   = (uint32_t)(pm->fb_phys & 0xFFFFFFFFU);
    port->fbu  = (uint32_t)(pm->fb_phys >> 32);

    mem_zero(pm->clb_virt, 1024);
    mem_zero(pm->fb_virt, 256);
    mem_zero(pm->ctba_virt, 4096);

    cmdhdr = (HBA_CMD_HEADER*)pm->clb_virt;
    cmdhdr[0].cfl = sizeof(FIS_REG_H2D) / sizeof(uint32_t);
    cmdhdr[0].w = 0;
    cmdhdr[0].prdtl = 1;
    cmdhdr[0].ctba = (uint32_t)(pm->ctba_phys & 0xFFFFFFFFU);
    cmdhdr[0].ctbau = (uint32_t)(pm->ctba_phys >> 32);

    port->serr = 0xFFFFFFFFU;
    port->is   = 0xFFFFFFFFU;

    port->cmd |= AHCI_PORT_CMD_SUD | AHCI_PORT_CMD_POD;

    ahci_port_start(port);
    return 1;
}

static int ahci_find_free_slot(HBA_PORT *port) {
    uint32_t slots;
    uint32_t i;

    if (!port) {
        return -1;
    }

    slots = port->sact | port->ci;
    for (i = 0; i < 32; i++) {
        if ((slots & (1U << i)) == 0) {
            return (int)i;
        }
    }

    return -1;
}

static int ahci_wait_not_busy(HBA_PORT *port, uint32_t spins) {
    while (spins--) {
        uint32_t tfd = port->tfd;
        if ((tfd & (ATA_DEV_BUSY | ATA_DEV_DRQ)) == 0) {
            return 1;
        }
    }
    return 0;
}

static int ahci_issue_identify(uint32_t port_no, uint16_t *identify_words) {
    HBA_MEM *abar;
    HBA_PORT *port;
    AHCI_PORT_MEM *pm;
    HBA_CMD_HEADER *cmdhdr;
    HBA_CMD_TBL *cmdtbl;
    FIS_REG_H2D *fis;
    int slot;

    if (!g_ahci.initialized || port_no >= AHCI_MAX_PORTS || !identify_words) {
        return 0;
    }

    abar = (HBA_MEM*)g_ahci.abar_virt;
    port = &abar->ports[port_no];
    pm = &g_port_mem[port_no];

    if (!pm->allocated) {
        return 0;
    }

    if (!ahci_wait_not_busy(port, 1000000)) {
        return 0;
    }

    slot = ahci_find_free_slot(port);
    if (slot < 0) {
        return 0;
    }

    mem_zero(pm->dma_virt, 512);
    cmdhdr = (HBA_CMD_HEADER*)pm->clb_virt;
    cmdtbl = (HBA_CMD_TBL*)pm->ctba_virt;

    mem_zero(cmdtbl, sizeof(HBA_CMD_TBL));

    cmdhdr[slot].cfl = sizeof(FIS_REG_H2D) / sizeof(uint32_t);
    cmdhdr[slot].w = 0;
    cmdhdr[slot].prdtl = 1;
    cmdhdr[slot].prdbc = 0;
    cmdhdr[slot].ctba = (uint32_t)(pm->ctba_phys & 0xFFFFFFFFU);
    cmdhdr[slot].ctbau = (uint32_t)(pm->ctba_phys >> 32);

    cmdtbl->prdt_entry[0].dba  = (uint32_t)(pm->dma_phys & 0xFFFFFFFFU);
    cmdtbl->prdt_entry[0].dbau = (uint32_t)(pm->dma_phys >> 32);
    cmdtbl->prdt_entry[0].dbc  = 512 - 1;
    cmdtbl->prdt_entry[0].i    = 1;

    fis = (FIS_REG_H2D*)(&cmdtbl->cfis[0]);
    mem_zero(fis, sizeof(FIS_REG_H2D));
    fis->fis_type = FIS_TYPE_REG_H2D;
    fis->c = 1;
    fis->command = ATA_CMD_IDENTIFY;
    fis->device = 0;

    port->is = 0xFFFFFFFFU;
    port->ci = (1U << slot);

    while (1) {
        if ((port->ci & (1U << slot)) == 0) {
            break;
        }

        if (port->is & (1U << 30)) {
            return 0;
        }
    }

    if (port->tfd & (AHCI_PORT_TFD_ERR | AHCI_PORT_TFD_BSY | AHCI_PORT_TFD_DRQ)) {
        return 0;
    }

    mem_copy(identify_words, pm->dma_virt, 512);
    return 1;
}

static int ahci_fill_port_info(uint32_t port_no, AHCI_PORT_INFO *out_info) {
    HBA_MEM *abar;
    HBA_PORT *port;
    uint16_t identify[256];

    if (!g_ahci.initialized || !out_info || port_no >= AHCI_MAX_PORTS) {
        return 0;
    }

    abar = (HBA_MEM*)g_ahci.abar_virt;
    port = &abar->ports[port_no];

    mem_zero(out_info, sizeof(*out_info));
    out_info->port_no = (uint8_t)port_no;
    out_info->implemented = ((g_ahci.ports_implemented & (1U << port_no)) != 0);
    out_info->sig = port->sig;
    out_info->ssts = port->ssts;
    out_info->tfd = port->tfd;
    out_info->device_present = ahci_port_is_device_present(port);
    out_info->sata = ahci_port_signature_is_sata(port);
    out_info->atapi = ahci_port_signature_is_atapi(port);
    out_info->active = out_info->implemented && out_info->device_present && out_info->sata;

    if (!out_info->active) {
        return 1;
    }

    if (!ahci_issue_identify(port_no, identify)) {
        return 1;
    }

    ata_string_from_words(out_info->serial, sizeof(out_info->serial),
                          &identify[ATA_IDENT_SERIAL_WORDS], 10);
    ata_string_from_words(out_info->firmware, sizeof(out_info->firmware),
                          &identify[ATA_IDENT_FW_WORDS], 4);
    ata_string_from_words(out_info->model, sizeof(out_info->model),
                          &identify[ATA_IDENT_MODEL_WORDS], 20);

    out_info->sectors_28 =
        ((uint32_t)identify[ATA_IDENT_MAXLBA28_WORDS + 1] << 16) |
        ((uint32_t)identify[ATA_IDENT_MAXLBA28_WORDS + 0]);

    out_info->sectors_48 =
        ((uint64_t)identify[ATA_IDENT_MAXLBA48_WORDS + 3] << 48) |
        ((uint64_t)identify[ATA_IDENT_MAXLBA48_WORDS + 2] << 32) |
        ((uint64_t)identify[ATA_IDENT_MAXLBA48_WORDS + 1] << 16) |
        ((uint64_t)identify[ATA_IDENT_MAXLBA48_WORDS + 0]);

    return 1;
}

int ahci_probe(AHCI_INFO *info) {
    PCI_DEVICE_INFO pci;

    if (!info) {
        return 0;
    }

    mem_zero(info, sizeof(*info));

    if (!ahci_find_controller(&pci)) {
        return 0;
    }

    info->present = 1;
    info->bus = pci.bus;
    info->device = pci.device;
    info->function = pci.function;
    info->vendor_id = pci.vendor_id;
    info->device_id = pci.device_id;
    info->irq_line = pci.irq_line;
    info->irq_pin = pci.irq_pin;
    info->abar_phys = (pci.bar[5] & ~0xFUL);

    return 1;
}

int ahci_init(AHCI_INFO *info) {
    PCI_DEVICE_INFO pci;
    HBA_MEM *abar;
    uint32_t i;
    uint32_t count = 0;

    if (!info) {
        return 0;
    }

    mem_zero(&g_ahci, sizeof(g_ahci));

    if (!ahci_find_controller(&pci)) {
        return 0;
    }

    ahci_enable_pci_device(&pci);

    g_ahci.present = 1;
    g_ahci.bus = pci.bus;
    g_ahci.device = pci.device;
    g_ahci.function = pci.function;
    g_ahci.vendor_id = pci.vendor_id;
    g_ahci.device_id = pci.device_id;
    g_ahci.irq_line = pci.irq_line;
    g_ahci.irq_pin = pci.irq_pin;
    g_ahci.abar_phys = (pci.bar[5] & ~0xFUL);

    vmm_map_range(g_ahci.abar_phys, g_ahci.abar_phys, AHCI_MMIO_MAP_SIZE, VMM_WRITE | VMM_PCD);
    g_ahci.abar_virt = (volatile void*)(uintptr_t)g_ahci.abar_phys;

    abar = (HBA_MEM*)g_ahci.abar_virt;

    abar->ghc |= AHCI_HBA_GHC_AE;

    if (abar->cap2 & 1U) {
        uint32_t bohc = abar->bohc;
        if (bohc & AHCI_HBA_BOHC_BOS) {
            abar->bohc |= AHCI_HBA_BOHC_OOS;
            ahci_wait_clear(&abar->bohc, AHCI_HBA_BOHC_BB, 1000000);
        }
    }

    g_ahci.ports_implemented = abar->pi;

    for (i = 0; i < 32; i++) {
        HBA_PORT *port;

        if ((g_ahci.ports_implemented & (1U << i)) == 0) {
            continue;
        }

        port = &abar->ports[i];

        if (!ahci_port_is_device_present(port)) {
            continue;
        }

        if (!ahci_port_signature_is_sata(port)) {
            continue;
        }

        if (!ahci_port_rebase(port, i)) {
            continue;
        }

        count++;
    }

    g_ahci.port_count = count;
    g_ahci.initialized = 1;
    *info = g_ahci;
    return 1;
}

AHCI_INFO *ahci_get_state(void) {
    return &g_ahci;
}

int ahci_port_info(uint32_t port_no, AHCI_PORT_INFO *out_info) {
    return ahci_fill_port_info(port_no, out_info);
}

void ahci_print_info(CONSOLE *con, const AHCI_INFO *info) {
    if (!con || !info) {
        return;
    }

    if (!info->present) {
        console_printf(con, "ahci: controller not found\n");
        return;
    }

    console_printf(con, "AHCI controller:\n");
    console_printf(con, "  PCI:               %u:%u.%u\n",
                   (unsigned int)info->bus,
                   (unsigned int)info->device,
                   (unsigned int)info->function);
    console_printf(con, "  vendor/device:     %x / %x\n",
                   (unsigned int)info->vendor_id,
                   (unsigned int)info->device_id);
    console_printf(con, "  ABAR phys:         %x\n",
                   (unsigned int)info->abar_phys);
    console_printf(con, "  IRQ line/pin:      %u / %u\n",
                   (unsigned int)info->irq_line,
                   (unsigned int)info->irq_pin);
    console_printf(con, "  PI bitmap:         %x\n",
                   (unsigned int)info->ports_implemented);
    console_printf(con, "  active SATA ports: %u\n",
                   (unsigned int)info->port_count);
}

void ahci_print_ports(CONSOLE *con) {
    uint32_t i;

    if (!con) {
        return;
    }

    if (!g_ahci.initialized) {
        console_printf(con, "ahciports: AHCI is not initialized\n");
        return;
    }

    console_printf(con, "AHCI ports:\n");

    for (i = 0; i < 32; i++) {
        AHCI_PORT_INFO pi;

        if ((g_ahci.ports_implemented & (1U << i)) == 0) {
            continue;
        }

        mem_zero(&pi, sizeof(pi));
        ahci_fill_port_info(i, &pi);

        console_printf(con, "  port %u: sig=%x ssts=%x ",
                       (unsigned int)i,
                       (unsigned int)pi.sig,
                       (unsigned int)pi.ssts);

        if (!pi.device_present) {
            console_printf(con, "[no device]\n");
            continue;
        }

        if (pi.atapi) {
            console_printf(con, "[ATAPI]\n");
            continue;
        }

        if (!pi.sata) {
            console_printf(con, "[non-SATA]\n");
            continue;
        }

        console_printf(con, "[SATA]");
        if (pi.model[0]) {
            console_printf(con, " model=%s", pi.model);
        }
        if (pi.sectors_48) {
            console_printf(con, " sectors48=%u",
                           (unsigned int)(pi.sectors_48 & 0xFFFFFFFFULL));
        }
        console_printf(con, "\n");
    }
}

int ahci_identify(uint32_t port_no, AHCI_PORT_INFO *out_info) {
    if (!out_info) {
        return 0;
    }

    return ahci_fill_port_info(port_no, out_info);
}

static int ahci_rw(uint32_t port_no,
                   uint64_t lba,
                   uint32_t count,
                   void *buffer,
                   int is_write) {
    HBA_MEM *abar;
    HBA_PORT *port;
    AHCI_PORT_MEM *pm;
    HBA_CMD_HEADER *cmdhdr;
    HBA_CMD_TBL *cmdtbl;
    FIS_REG_H2D *fis;
    int slot;
    uint32_t bytes;

    if (!g_ahci.initialized || !buffer) {
        return 0;
    }

    if (port_no >= AHCI_MAX_PORTS || count == 0) {
        return 0;
    }

    if (!is_write && count > AHCI_MAX_READ_SECTORS) {
        return 0;
    }

    if (is_write && count > AHCI_MAX_WRITE_SECTORS) {
        return 0;
    }

    abar = (HBA_MEM*)g_ahci.abar_virt;
    port = &abar->ports[port_no];
    pm = &g_port_mem[port_no];

    if (!pm->allocated) {
        return 0;
    }

    if (!ahci_port_is_device_present(port) || !ahci_port_signature_is_sata(port)) {
        return 0;
    }

    if (!ahci_wait_not_busy(port, 1000000)) {
        return 0;
    }

    slot = ahci_find_free_slot(port);
    if (slot < 0) {
        return 0;
    }

    bytes = count * AHCI_SECTOR_SIZE;
    if (bytes > pm->dma_bytes) {
        return 0;
    }

    if (is_write) {
        mem_copy(pm->dma_virt, buffer, bytes);
    } else {
        mem_zero(pm->dma_virt, bytes);
    }

    cmdhdr = (HBA_CMD_HEADER*)pm->clb_virt;
    cmdtbl = (HBA_CMD_TBL*)pm->ctba_virt;

    mem_zero(cmdtbl, sizeof(HBA_CMD_TBL));

    cmdhdr[slot].cfl = sizeof(FIS_REG_H2D) / sizeof(uint32_t);
    cmdhdr[slot].w = is_write ? 1 : 0;
    cmdhdr[slot].prdtl = 1;
    cmdhdr[slot].prdbc = 0;
    cmdhdr[slot].ctba = (uint32_t)(pm->ctba_phys & 0xFFFFFFFFU);
    cmdhdr[slot].ctbau = (uint32_t)(pm->ctba_phys >> 32);

    cmdtbl->prdt_entry[0].dba  = (uint32_t)(pm->dma_phys & 0xFFFFFFFFU);
    cmdtbl->prdt_entry[0].dbau = (uint32_t)(pm->dma_phys >> 32);
    cmdtbl->prdt_entry[0].dbc  = bytes - 1;
    cmdtbl->prdt_entry[0].i    = 1;

    fis = (FIS_REG_H2D*)(&cmdtbl->cfis[0]);
    mem_zero(fis, sizeof(FIS_REG_H2D));
    fis->fis_type = FIS_TYPE_REG_H2D;
    fis->c = 1;
    fis->command = is_write ? ATA_CMD_WRITE_DMA_EXT : ATA_CMD_READ_DMA_EXT;
    fis->device = 1U << 6;

    fis->lba0 = (uint8_t)(lba >> 0);
    fis->lba1 = (uint8_t)(lba >> 8);
    fis->lba2 = (uint8_t)(lba >> 16);
    fis->lba3 = (uint8_t)(lba >> 24);
    fis->lba4 = (uint8_t)(lba >> 32);
    fis->lba5 = (uint8_t)(lba >> 40);

    fis->countl = (uint8_t)(count & 0xFF);
    fis->counth = (uint8_t)((count >> 8) & 0xFF);

    port->is = 0xFFFFFFFFU;
    port->ci = (1U << slot);

    while (1) {
        if ((port->ci & (1U << slot)) == 0) {
            break;
        }

        if (port->is & (1U << 30)) {
            return 0;
        }
    }

    if (port->tfd & (AHCI_PORT_TFD_ERR | AHCI_PORT_TFD_BSY | AHCI_PORT_TFD_DRQ)) {
        return 0;
    }

    if (!is_write) {
        mem_copy(buffer, pm->dma_virt, bytes);
    }

    return 1;
}

int ahci_read(uint32_t port_no, uint64_t lba, uint32_t count, void *out_buf) {
    return ahci_rw(port_no, lba, count, out_buf, 0);
}

int ahci_write(uint32_t port_no, uint64_t lba, uint32_t count, const void *in_buf) {
    return ahci_rw(port_no, lba, count, (void*)in_buf, 1);
}

static int ahci_block_read(BLOCK_DEVICE *dev, uint64_t lba, uint32_t count, void *out_buf) {
    uint32_t port_no;

    if (!dev) {
        return 0;
    }

    port_no = (uint32_t)(uintptr_t)dev->driver_data;
    return ahci_read(port_no, lba, count, out_buf);
}

static int ahci_block_write(BLOCK_DEVICE *dev, uint64_t lba, uint32_t count, const void *in_buf) {
    uint32_t port_no;

    if (!dev) {
        return 0;
    }

    port_no = (uint32_t)(uintptr_t)dev->driver_data;
    return ahci_write(port_no, lba, count, in_buf);
}

static int ahci_block_flush(BLOCK_DEVICE *dev) {
    (void)dev;
    return 1;
}

int ahci_register_block_devices(void) {
    static const BLOCK_DEVICE_OPS ahci_block_ops = {
        .read = ahci_block_read,
        .write = ahci_block_write,
        .flush = ahci_block_flush
    };

    uint32_t port_no;
    uint32_t disk_index = block_get_count();

    if (!g_ahci.initialized) {
        return 0;
    }

    if (!block_is_initialized()) {
        return 0;
    }

    for (port_no = 0; port_no < AHCI_MAX_PORTS; port_no++) {
        AHCI_PORT_INFO pi;
        BLOCK_DEVICE dev;
        uint64_t total_sectors;

        if ((g_ahci.ports_implemented & (1U << port_no)) == 0) {
            continue;
        }

        if (g_ahci_block_registered[port_no]) {
            continue;
        }

        if (!ahci_identify(port_no, &pi)) {
            continue;
        }

        if (!pi.active) {
            continue;
        }

        total_sectors = pi.sectors_48 ? pi.sectors_48 : pi.sectors_28;
        if (total_sectors == 0) {
            continue;
        }

        mem_zero(&dev, sizeof(dev));
        dev.present = 1;
        dev.type = BLOCK_TYPE_DISK;
        dev.flags = BLOCK_FLAG_PRESENT;
        dev.sector_size = AHCI_SECTOR_SIZE;
        dev.total_sectors = total_sectors;
        dev.total_bytes = total_sectors * (uint64_t)AHCI_SECTOR_SIZE;
        dev.driver_name = "ahci";
        dev.driver_data = (void*)(uintptr_t)port_no;
        dev.ops = &ahci_block_ops;

        dev.name[0] = 's';
        dev.name[1] = 'd';
        dev.name[2] = (char)('0' + (disk_index % 10));
        dev.name[3] = '\0';

        if (pi.model[0]) {
            uint32_t i = 0;
            while (i + 1 < sizeof(dev.model) && pi.model[i]) {
                dev.model[i] = pi.model[i];
                i++;
            }
            dev.model[i] = '\0';
        }

        if (!block_register_device(&dev)) {
            return 0;
        }

        g_ahci_block_registered[port_no] = 1;
        disk_index++;
    }

    return 1;
}
