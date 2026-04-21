#ifndef E1000_H
#define E1000_H

#include <stdint.h>
#include <stddef.h>
#include "console.h"

typedef struct {
    int present;
    uint8_t bus;
    uint8_t device;
    uint8_t function;

    uint16_t vendor_id;
    uint16_t device_id;

    uint16_t pci_command;
    uint16_t pci_status;

    uint32_t bar0;
    uint32_t mmio_base;

    uint32_t ctrl;
    uint32_t status;

    uint32_t eecd;
    uint32_t eerd;
    uint32_t icr;
    uint32_t ims;
    uint32_t imc;
    uint32_t rctl;
    uint32_t tctl;
    uint32_t tipg;

    uint32_t rdbal;
    uint32_t rdbah;
    uint32_t rdlen;
    uint32_t rdh;
    uint32_t rdt;

    uint32_t tdbal;
    uint32_t tdbah;
    uint32_t tdlen;
    uint32_t tdh;
    uint32_t tdt;

    uint8_t irq_line;
    uint8_t irq_pin;

    uint8_t mac[6];

    int link_up;
    int eeprom_ok;

    int rings_ready;
    uint32_t rx_desc_count;
    uint32_t tx_desc_count;
} E1000_INFO;

int e1000_probe(E1000_INFO *info);
int e1000_init(E1000_INFO *info);
int e1000_init_rings(E1000_INFO *info);

int e1000_send_packet(E1000_INFO *info, const void *data, uint16_t length);
int e1000_send_test_packet(E1000_INFO *info);
int e1000_send_arp_request(E1000_INFO *info, const uint8_t target_ip[4]);

void e1000_refresh(E1000_INFO *info);
void e1000_print_info(CONSOLE *con, const E1000_INFO *info);
void e1000_dump_registers(CONSOLE *con, const E1000_INFO *info);

#endif
