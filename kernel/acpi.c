#include "acpi.h"
#include "console.h"
#include <stddef.h>

static int acpi_initialized = 0;
static acpi_rsdp2_t *rsdp = NULL;
static acpi_rsdt_t *rsdt = NULL;
static acpi_xsdt_t *xsdt = NULL;
static int use_xsdt = 0;

/* Helper: memory compare */
static int acpi_memcmp(const void *a, const void *b, uint64_t size) {
    const uint8_t *pa = (const uint8_t*)a;
    const uint8_t *pb = (const uint8_t*)b;
    for (uint64_t i = 0; i < size; i++) {
        if (pa[i] != pb[i]) {
            return 1;
        }
    }
    return 0;
}

/* Helper: calculate checksum */
static uint8_t acpi_checksum(const void *data, uint64_t length) {
    const uint8_t *bytes = (const uint8_t*)data;
    uint8_t sum = 0;
    
    for (uint64_t i = 0; i < length; i++) {
        sum += bytes[i];
    }
    
    return sum;
}

/* Validate RSDP */
static int acpi_validate_rsdp(acpi_rsdp_t *r) {
    if (!r) {
        return 0;
    }
    
    if (acpi_memcmp(r->signature, ACPI_SIG_RSDP, 8) != 0) {
        return 0;
    }
    
    if (acpi_checksum(r, 20) != 0) {
        return 0;
    }
    
    return 1;
}

/* Validate RSDP 2.0 */
static int acpi_validate_rsdp2(acpi_rsdp2_t *r) {
    if (!acpi_validate_rsdp(&r->rsdp1)) {
        return 0;
    }
    
    if (r->rsdp1.revision < 2) {
        return 0;
    }
    
    if (acpi_checksum(r, r->length) != 0) {
        return 0;
    }
    
    return 1;
}

/* Validate SDT header */
static int acpi_validate_sdt(acpi_sdt_header_t *header) {
    if (!header) {
        return 0;
    }
    
    if (acpi_checksum(header, header->length) != 0) {
        return 0;
    }
    
    return 1;
}

void acpi_init(uint64_t rsdp_address) {
    acpi_rsdp_t *r1;
    acpi_rsdp2_t *r2;
    
    if (rsdp_address == 0) {
        return;
    }
    
    r1 = (acpi_rsdp_t*)(uintptr_t)rsdp_address;
    
    if (!acpi_validate_rsdp(r1)) {
        return;
    }
    
    rsdp = (acpi_rsdp2_t*)r1;
    
    /* Check if ACPI 2.0+ */
    if (r1->revision >= 2) {
        r2 = (acpi_rsdp2_t*)r1;
        if (acpi_validate_rsdp2(r2)) {
            if (r2->xsdt_address != 0) {
                xsdt = (acpi_xsdt_t*)(uintptr_t)r2->xsdt_address;
                if (acpi_validate_sdt(&xsdt->header)) {
                    use_xsdt = 1;
                } else {
                    xsdt = NULL;
                }
            }
        }
    }
    
    /* Try RSDT if XSDT not available */
    if (!use_xsdt && r1->rsdt_address != 0) {
        rsdt = (acpi_rsdt_t*)(uintptr_t)r1->rsdt_address;
        if (!acpi_validate_sdt(&rsdt->header)) {
            rsdt = NULL;
            return;
        }
    }
    
    acpi_initialized = 1;
}

int acpi_is_initialized(void) {
    return acpi_initialized;
}

acpi_sdt_header_t* acpi_find_table(const char *signature) {
    uint32_t entry_count;
    uint32_t i;
    
    if (!acpi_initialized || !signature) {
        return NULL;
    }
    
    if (use_xsdt && xsdt) {
        entry_count = (xsdt->header.length - sizeof(acpi_sdt_header_t)) / 8;
        
        for (i = 0; i < entry_count; i++) {
            acpi_sdt_header_t *header = (acpi_sdt_header_t*)(uintptr_t)xsdt->entries[i];
            
            if (!header) {
                continue;
            }
            
            if (acpi_memcmp(header->signature, signature, 4) == 0) {
                if (acpi_validate_sdt(header)) {
                    return header;
                }
            }
        }
    } else if (rsdt) {
        entry_count = (rsdt->header.length - sizeof(acpi_sdt_header_t)) / 4;
        
        for (i = 0; i < entry_count; i++) {
            acpi_sdt_header_t *header = (acpi_sdt_header_t*)(uintptr_t)rsdt->entries[i];
            
            if (!header) {
                continue;
            }
            
            if (acpi_memcmp(header->signature, signature, 4) == 0) {
                if (acpi_validate_sdt(header)) {
                    return header;
                }
            }
        }
    }
    
    return NULL;
}

void acpi_print_info(CONSOLE *con) {
    if (!con || !acpi_initialized) {
        return;
    }
    
    console_printf(con, "ACPI Information:\n");
    
    if (rsdp) {
        console_printf(con, "  RSDP found at 0x%p\n", (void*)rsdp);
        console_printf(con, "  Revision: %u\n", (unsigned int)rsdp->rsdp1.revision);
        console_printf(con, "  OEM ID: %.6s\n", rsdp->rsdp1.oem_id);
    }
    
    if (use_xsdt && xsdt) {
        console_printf(con, "  Using XSDT at 0x%p\n", (void*)xsdt);
    } else if (rsdt) {
        console_printf(con, "  Using RSDT at 0x%p\n", (void*)rsdt);
    }
    
    /* Print available tables */
    uint32_t entry_count;
    uint32_t i;
    
    if (use_xsdt && xsdt) {
        entry_count = (xsdt->header.length - sizeof(acpi_sdt_header_t)) / 8;
        console_printf(con, "  Tables (%u):\n", entry_count);
        
        for (i = 0; i < entry_count; i++) {
            acpi_sdt_header_t *header = (acpi_sdt_header_t*)(uintptr_t)xsdt->entries[i];
            if (header) {
                console_printf(con, "    %.4s at 0x%p\n", header->signature, (void*)header);
            }
        }
    } else if (rsdt) {
        entry_count = (rsdt->header.length - sizeof(acpi_sdt_header_t)) / 4;
        console_printf(con, "  Tables (%u):\n", entry_count);
        
        for (i = 0; i < entry_count; i++) {
            acpi_sdt_header_t *header = (acpi_sdt_header_t*)(uintptr_t)rsdt->entries[i];
            if (header) {
                console_printf(con, "    %.4s at 0x%p\n", header->signature, (void*)header);
            }
        }
    }
}

void acpi_print_table(CONSOLE *con, acpi_sdt_header_t *header) {
    if (!con || !header) {
        return;
    }
    
    console_printf(con, "ACPI Table %.4s:\n", header->signature);
    console_printf(con, "  Length: %u bytes\n", header->length);
    console_printf(con, "  Revision: %u\n", (unsigned int)header->revision);
    console_printf(con, "  OEM ID: %.6s\n", header->oem_id);
    console_printf(con, "  OEM Table ID: %.8s\n", header->oem_table_id);
}

uint32_t acpi_get_cpu_count(void) {
    acpi_madt_t *madt;
    uint8_t *ptr;
    uint8_t *end;
    uint32_t count = 0;
    
    if (!acpi_initialized) {
        return 1; /* Default to 1 CPU */
    }
    
    madt = (acpi_madt_t*)acpi_find_table(ACPI_SIG_MADT);
    if (!madt) {
        return 1;
    }
    
    ptr = (uint8_t*)madt + sizeof(acpi_madt_t);
    end = (uint8_t*)madt + madt->header.length;
    
    while (ptr < end) {
        acpi_madt_entry_header_t *entry = (acpi_madt_entry_header_t*)ptr;
        
        if (entry->type == ACPI_MADT_TYPE_LOCAL_APIC) {
            acpi_madt_local_apic_t *lapic = (acpi_madt_local_apic_t*)entry;
            if (lapic->flags & 1) { /* CPU enabled */
                count++;
            }
        }
        
        ptr += entry->length;
    }
    
    return count > 0 ? count : 1;
}

uint64_t acpi_get_local_apic_address(void) {
    acpi_madt_t *madt;
    
    if (!acpi_initialized) {
        return 0xFEE00000; /* Default local APIC address */
    }
    
    madt = (acpi_madt_t*)acpi_find_table(ACPI_SIG_MADT);
    if (!madt) {
        return 0xFEE00000;
    }
    
    return (uint64_t)madt->local_apic_address;
}
