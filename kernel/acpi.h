#ifndef ACPI_H
#define ACPI_H

#include <stdint.h>
#include "console.h"

/* ACPI Table signatures */
#define ACPI_SIG_RSDP  "RSD PTR "
#define ACPI_SIG_RSDT  "RSDT"
#define ACPI_SIG_XSDT  "XSDT"
#define ACPI_SIG_FADT  "FACP"
#define ACPI_SIG_MADT  "APIC"
#define ACPI_SIG_HPET  "HPET"
#define ACPI_SIG_MCFG  "MCFG"

/* RSDP structure */
typedef struct __attribute__((packed)) {
    char signature[8];
    uint8_t checksum;
    char oem_id[6];
    uint8_t revision;
    uint32_t rsdt_address;
} acpi_rsdp_t;

/* RSDP 2.0 structure */
typedef struct __attribute__((packed)) {
    acpi_rsdp_t rsdp1;
    uint32_t length;
    uint64_t xsdt_address;
    uint8_t extended_checksum;
    uint8_t reserved[3];
} acpi_rsdp2_t;

/* ACPI SDT Header */
typedef struct __attribute__((packed)) {
    char signature[4];
    uint32_t length;
    uint8_t revision;
    uint8_t checksum;
    char oem_id[6];
    char oem_table_id[8];
    uint32_t oem_revision;
    uint32_t creator_id;
    uint32_t creator_revision;
} acpi_sdt_header_t;

/* RSDT structure */
typedef struct __attribute__((packed)) {
    acpi_sdt_header_t header;
    uint32_t entries[];
} acpi_rsdt_t;

/* XSDT structure */
typedef struct __attribute__((packed)) {
    acpi_sdt_header_t header;
    uint64_t entries[];
} acpi_xsdt_t;

/* MADT (APIC) structures */
typedef struct __attribute__((packed)) {
    acpi_sdt_header_t header;
    uint32_t local_apic_address;
    uint32_t flags;
} acpi_madt_t;

typedef struct __attribute__((packed)) {
    uint8_t type;
    uint8_t length;
} acpi_madt_entry_header_t;

#define ACPI_MADT_TYPE_LOCAL_APIC       0
#define ACPI_MADT_TYPE_IO_APIC          1
#define ACPI_MADT_TYPE_INT_OVERRIDE     2

typedef struct __attribute__((packed)) {
    acpi_madt_entry_header_t header;
    uint8_t processor_id;
    uint8_t apic_id;
    uint32_t flags;
} acpi_madt_local_apic_t;

typedef struct __attribute__((packed)) {
    acpi_madt_entry_header_t header;
    uint8_t io_apic_id;
    uint8_t reserved;
    uint32_t io_apic_address;
    uint32_t global_system_interrupt_base;
} acpi_madt_io_apic_t;

/* ACPI functions */
void acpi_init(uint64_t rsdp_address);
int acpi_is_initialized(void);

acpi_sdt_header_t* acpi_find_table(const char *signature);

void acpi_print_info(CONSOLE *con);
void acpi_print_table(CONSOLE *con, acpi_sdt_header_t *header);

uint32_t acpi_get_cpu_count(void);
uint64_t acpi_get_local_apic_address(void);

#endif
