#ifndef HELLCORE_VMM_H
#define HELLCORE_VMM_H

#include <stdint.h>
#include "pmm.h"

#define HELLCORE_VMM_PAGE_PRESENT  (1ULL << 0)
#define HELLCORE_VMM_PAGE_WRITE    (1ULL << 1)
#define HELLCORE_VMM_PAGE_USER     (1ULL << 2)
#define HELLCORE_VMM_PAGE_HUGE     (1ULL << 7)
#define HELLCORE_VMM_PAGE_NX       (1ULL << 63)

#define HELLCORE_VMM_MAP_READ      (1U << 0)
#define HELLCORE_VMM_MAP_WRITE     (1U << 1)
#define HELLCORE_VMM_MAP_EXECUTE   (1U << 2)
#define HELLCORE_VMM_MAP_USER      (1U << 3)

int hellcore_vmm_init(const HELLCORE_BOOT_INFO *boot);

int hellcore_vmm_map_page(
    uint64_t virtual_address,
    uint64_t physical_address,
    uint64_t flags
);

int hellcore_vmm_unmap_page(uint64_t virtual_address);

uint64_t hellcore_vmm_translate(uint64_t virtual_address);

int hellcore_vmm_get_flags(uint64_t virtual_address, uint64_t *flags);
uint64_t hellcore_vmm_get_root(void);

#endif
