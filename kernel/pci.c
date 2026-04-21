#include "pci.h"
#include <stdint.h>

#define PCI_CONFIG_ADDRESS 0xCF8
#define PCI_CONFIG_DATA    0xCFC

static void pci_outl(uint16_t port, uint32_t value) {
    __asm__ volatile ("outl %0, %1" : : "a"(value), "Nd"(port));
}

static uint32_t pci_inl(uint16_t port) {
    uint32_t value;
    __asm__ volatile ("inl %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static uint32_t pci_make_config_address(uint8_t bus,
                                        uint8_t device,
                                        uint8_t function,
                                        uint8_t offset) {
    return (1U << 31) |
           ((uint32_t)bus << 16) |
           ((uint32_t)device << 11) |
           ((uint32_t)function << 8) |
           ((uint32_t)(offset & 0xFC));
}

uint32_t pci_config_read32(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset) {
    uint32_t address = pci_make_config_address(bus, device, function, offset);
    pci_outl(PCI_CONFIG_ADDRESS, address);
    return pci_inl(PCI_CONFIG_DATA);
}

uint16_t pci_config_read16(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset) {
    uint32_t value = pci_config_read32(bus, device, function, offset);
    uint8_t shift = (uint8_t)((offset & 2U) * 8U);
    return (uint16_t)((value >> shift) & 0xFFFFU);
}

uint8_t pci_config_read8(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset) {
    uint32_t value = pci_config_read32(bus, device, function, offset);
    uint8_t shift = (uint8_t)((offset & 3U) * 8U);
    return (uint8_t)((value >> shift) & 0xFFU);
}

void pci_config_write32(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset, uint32_t value) {
    uint32_t address = pci_make_config_address(bus, device, function, offset);
    pci_outl(PCI_CONFIG_ADDRESS, address);
    pci_outl(PCI_CONFIG_DATA, value);
}

void pci_config_write16(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset, uint16_t value) {
    uint32_t aligned = pci_config_read32(bus, device, function, offset);
    uint8_t shift = (uint8_t)((offset & 2U) * 8U);
    uint32_t mask = 0xFFFFU << shift;
    uint32_t new_value = (aligned & ~mask) | ((uint32_t)value << shift);
    pci_config_write32(bus, device, function, offset, new_value);
}

void pci_config_write8(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset, uint8_t value) {
    uint32_t aligned = pci_config_read32(bus, device, function, offset);
    uint8_t shift = (uint8_t)((offset & 3U) * 8U);
    uint32_t mask = 0xFFU << shift;
    uint32_t new_value = (aligned & ~mask) | ((uint32_t)value << shift);
    pci_config_write32(bus, device, function, offset, new_value);
}

static const char *pci_class_name(uint8_t class_code, uint8_t subclass) {
    switch (class_code) {
        case 0x00:
            switch (subclass) {
                case 0x00: return "Unclassified/VGA-compatible";
                case 0x01: return "Unclassified/Other";
                default:   return "Unclassified";
            }

        case 0x01:
            switch (subclass) {
                case 0x00: return "Mass Storage/SCSI";
                case 0x01: return "Mass Storage/IDE";
                case 0x02: return "Mass Storage/Floppy";
                case 0x03: return "Mass Storage/IPI";
                case 0x04: return "Mass Storage/RAID";
                case 0x05: return "Mass Storage/ATA";
                case 0x06: return "Mass Storage/SATA";
                case 0x07: return "Mass Storage/SAS";
                case 0x08: return "Mass Storage/NVMe";
                default:   return "Mass Storage";
            }

        case 0x02:
            switch (subclass) {
                case 0x00: return "Network/Ethernet";
                case 0x80: return "Network/Other";
                default:   return "Network";
            }

        case 0x03:
            switch (subclass) {
                case 0x00: return "Display/VGA";
                case 0x80: return "Display/Other";
                default:   return "Display";
            }

        case 0x04: return "Multimedia";
        case 0x05: return "Memory";
        case 0x06:
            switch (subclass) {
                case 0x00: return "Bridge/Host";
                case 0x01: return "Bridge/ISA";
                case 0x04: return "Bridge/PCI-to-PCI";
                default:   return "Bridge";
            }

        case 0x07: return "Communication";
        case 0x08: return "System Peripheral";
        case 0x09: return "Input Device";
        case 0x0A: return "Docking Station";
        case 0x0B: return "Processor";
        case 0x0C:
            switch (subclass) {
                case 0x03: return "Serial Bus/USB";
                case 0x05: return "Serial Bus/SMBus";
                default:   return "Serial Bus";
            }

        case 0x0D: return "Wireless";
        case 0x0E: return "Intelligent I/O";
        case 0x0F: return "Satellite";
        case 0x10: return "Encryption";
        case 0x11: return "Signal Processing";
        case 0x12: return "Processing Accelerator";
        case 0x13: return "Non-Essential Instrumentation";
        case 0x40: return "Co-Processor";
        case 0xFF: return "Unassigned";
        default:   return "Unknown";
    }
}

static int pci_device_exists(uint8_t bus, uint8_t device, uint8_t function) {
    return pci_config_read16(bus, device, function, 0x00) != 0xFFFF;
}

int pci_probe_device(uint8_t bus, uint8_t device, uint8_t function, PCI_DEVICE_INFO *info) {
    uint16_t vendor_id;
    int i;

    vendor_id = pci_config_read16(bus, device, function, 0x00);
    if (vendor_id == 0xFFFF) {
        return 0;
    }

    if (!info) {
        return 1;
    }

    info->bus = bus;
    info->device = device;
    info->function = function;

    info->vendor_id = vendor_id;
    info->device_id = pci_config_read16(bus, device, function, 0x02);

    info->command = pci_config_read16(bus, device, function, 0x04);
    info->status  = pci_config_read16(bus, device, function, 0x06);

    info->revision_id = pci_config_read8(bus, device, function, 0x08);
    info->prog_if     = pci_config_read8(bus, device, function, 0x09);
    info->subclass    = pci_config_read8(bus, device, function, 0x0A);
    info->class_code  = pci_config_read8(bus, device, function, 0x0B);

    info->header_type = (uint8_t)(pci_config_read8(bus, device, function, 0x0E) & 0x7F);

    for (i = 0; i < 6; i++) {
        info->bar[i] = pci_config_read32(bus, device, function, (uint8_t)(0x10 + i * 4));
    }

    info->subsystem_vendor_id = pci_config_read16(bus, device, function, 0x2C);
    info->subsystem_device_id = pci_config_read16(bus, device, function, 0x2E);

    info->irq_line = pci_config_read8(bus, device, function, 0x3C);
    info->irq_pin  = pci_config_read8(bus, device, function, 0x3D);

    return 1;
}

static void pci_print_bar(CONSOLE *con, uint32_t bar_index, uint32_t bar_value) {
    if ((bar_value & ~0xFULL) == 0 && (bar_value & 0x1) == 0) {
        console_printf(con, "  BAR%u: %x (unused)\n",
                       (unsigned int)bar_index,
                       (unsigned int)bar_value);
        return;
    }

    if (bar_value & 0x1) {
        console_printf(con, "  BAR%u: %x  I/O  base=%x\n",
                       (unsigned int)bar_index,
                       (unsigned int)bar_value,
                       (unsigned int)(bar_value & ~0x3U));
    } else {
        uint32_t type = (bar_value >> 1) & 0x3U;
        const char *type_name = "32-bit";

        if (type == 0x2) {
            type_name = "64-bit";
        }

        console_printf(con, "  BAR%u: %x  MMIO %s base=%x\n",
                       (unsigned int)bar_index,
                       (unsigned int)bar_value,
                       type_name,
                       (unsigned int)(bar_value & ~0xFULL));
    }
}

void pci_scan_and_print(CONSOLE *con) {
    uint32_t found = 0;

    if (!con) {
        return;
    }

    console_printf(con, "PCI scan:\n");

    for (uint16_t bus = 0; bus < 256; bus++) {
        for (uint8_t device = 0; device < 32; device++) {
            uint8_t max_functions = 1;

            if (!pci_device_exists((uint8_t)bus, device, 0)) {
                continue;
            }

            {
                uint8_t header = pci_config_read8((uint8_t)bus, device, 0, 0x0E);
                if (header & 0x80) {
                    max_functions = 8;
                }
            }

            for (uint8_t function = 0; function < max_functions; function++) {
                PCI_DEVICE_INFO info;

                if (!pci_probe_device((uint8_t)bus, device, function, &info)) {
                    continue;
                }

                found++;

                console_printf(con,
                               "  %u:%u.%u  ven=%x dev=%x  class=%x sub=%x if=%x hdr=%x  %s\n",
                               (unsigned int)info.bus,
                               (unsigned int)info.device,
                               (unsigned int)info.function,
                               (unsigned int)info.vendor_id,
                               (unsigned int)info.device_id,
                               (unsigned int)info.class_code,
                               (unsigned int)info.subclass,
                               (unsigned int)info.prog_if,
                               (unsigned int)info.header_type,
                               pci_class_name(info.class_code, info.subclass));
            }
        }
    }

    console_printf(con, "PCI devices found: %u\n", (unsigned int)found);
}

void pci_dump_device(CONSOLE *con, uint8_t bus, uint8_t device, uint8_t function) {
    PCI_DEVICE_INFO info;
    int i;

    if (!con) {
        return;
    }

    if (!pci_probe_device(bus, device, function, &info)) {
        console_printf(con, "PCI device %u:%u.%u not found.\n",
                       (unsigned int)bus,
                       (unsigned int)device,
                       (unsigned int)function);
        return;
    }

    console_printf(con, "PCI device dump %u:%u.%u\n",
                   (unsigned int)bus,
                   (unsigned int)device,
                   (unsigned int)function);

    console_printf(con, "  vendor id:           %x\n", (unsigned int)info.vendor_id);
    console_printf(con, "  device id:           %x\n", (unsigned int)info.device_id);
    console_printf(con, "  class code:          %x\n", (unsigned int)info.class_code);
    console_printf(con, "  subclass:            %x\n", (unsigned int)info.subclass);
    console_printf(con, "  prog if:             %x\n", (unsigned int)info.prog_if);
    console_printf(con, "  revision id:         %x\n", (unsigned int)info.revision_id);
    console_printf(con, "  header type:         %x\n", (unsigned int)info.header_type);
    console_printf(con, "  class name:          %s\n", pci_class_name(info.class_code, info.subclass));
    console_printf(con, "  command:             %x\n", (unsigned int)info.command);
    console_printf(con, "  status:              %x\n", (unsigned int)info.status);
    console_printf(con, "  subsystem vendor id: %x\n", (unsigned int)info.subsystem_vendor_id);
    console_printf(con, "  subsystem device id: %x\n", (unsigned int)info.subsystem_device_id);
    console_printf(con, "  irq line:            %x\n", (unsigned int)info.irq_line);
    console_printf(con, "  irq pin:             %x\n", (unsigned int)info.irq_pin);

    for (i = 0; i < 6; i++) {
        pci_print_bar(con, (uint32_t)i, info.bar[i]);
    }
}
