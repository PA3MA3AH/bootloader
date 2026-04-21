#ifndef PCI_H
#define PCI_H

#include <stdint.h>
#include "console.h"

typedef struct {
    uint8_t bus;
    uint8_t device;
    uint8_t function;

    uint16_t vendor_id;
    uint16_t device_id;

    uint8_t class_code;
    uint8_t subclass;
    uint8_t prog_if;
    uint8_t revision_id;

    uint8_t header_type;

    uint16_t command;
    uint16_t status;

    uint16_t subsystem_vendor_id;
    uint16_t subsystem_device_id;

    uint8_t irq_line;
    uint8_t irq_pin;

    uint32_t bar[6];
} PCI_DEVICE_INFO;

uint32_t pci_config_read32(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset);
uint16_t pci_config_read16(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset);
uint8_t pci_config_read8(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset);

void pci_config_write32(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset, uint32_t value);
void pci_config_write16(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset, uint16_t value);
void pci_config_write8(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset, uint8_t value);

int pci_probe_device(uint8_t bus, uint8_t device, uint8_t function, PCI_DEVICE_INFO *info);
void pci_scan_and_print(CONSOLE *con);
void pci_dump_device(CONSOLE *con, uint8_t bus, uint8_t device, uint8_t function);

#endif
