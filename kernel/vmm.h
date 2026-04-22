#ifndef VMM_H
#define VMM_H

#include <stdint.h>

#define VMM_PRESENT   (1ULL << 0)
#define VMM_WRITE     (1ULL << 1)
#define VMM_USER      (1ULL << 2)
#define VMM_PWT       (1ULL << 3)
#define VMM_PCD       (1ULL << 4)
#define VMM_GLOBAL    (1ULL << 8)
#define VMM_NO_EXEC   (1ULL << 63)

void vmm_init(void);

void vmm_map_page(uint64_t virt, uint64_t phys, uint64_t flags);
void vmm_unmap_page(uint64_t virt);
uint64_t vmm_translate(uint64_t virt);

void vmm_map_range(uint64_t virt, uint64_t phys, uint64_t size, uint64_t flags);

#endif
