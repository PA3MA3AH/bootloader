#ifndef MEMORY_MAP_H
#define MEMORY_MAP_H

#include <stdint.h>
#include "../common/bootinfo.h"
#include "console.h"

const char *memory_type_name(uint32_t type);
void dump_memory_map(CONSOLE *con, BOOT_INFO *boot_info);

#endif
