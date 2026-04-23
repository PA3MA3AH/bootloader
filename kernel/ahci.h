#ifndef AHCI_H
#define AHCI_H

#include <stdint.h>
#include "console.h"

#define AHCI_MAX_PORTS 32
#define AHCI_MAX_READ_SECTORS 8
#define AHCI_MAX_WRITE_SECTORS 8
#define AHCI_SECTOR_SIZE      512U

typedef struct {
    int present;
    int initialized;

    uint8_t bus;
    uint8_t device;
    uint8_t function;

    uint16_t vendor_id;
    uint16_t device_id;

    uint8_t irq_line;
    uint8_t irq_pin;

    uint32_t abar_phys;
    volatile void *abar_virt;

    uint32_t ports_implemented;
    uint32_t port_count;
} AHCI_INFO;

typedef struct {
    int implemented;
    int sata;
    int atapi;
    int active;
    int device_present;
    uint8_t port_no;

    uint32_t sig;
    uint32_t ssts;
    uint32_t tfd;
    uint64_t sectors_28;
    uint64_t sectors_48;

    char model[41];
    char serial[21];
    char firmware[9];
} AHCI_PORT_INFO;

int ahci_probe(AHCI_INFO *info);
int ahci_init(AHCI_INFO *info);
AHCI_INFO *ahci_get_state(void);

int ahci_port_info(uint32_t port_no, AHCI_PORT_INFO *out_info);
void ahci_print_info(CONSOLE *con, const AHCI_INFO *info);
void ahci_print_ports(CONSOLE *con);

int ahci_identify(uint32_t port_no, AHCI_PORT_INFO *out_info);

/* count: 1..8 sectors currently */
int ahci_read(uint32_t port_no, uint64_t lba, uint32_t count, void *out_buf);
int ahci_write(uint32_t port_no, uint64_t lba, uint32_t count, const void *in_buf);

int ahci_register_block_devices(void);

#endif
