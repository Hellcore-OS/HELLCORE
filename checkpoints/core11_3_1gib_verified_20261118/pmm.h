#ifndef HELLCORE_PMM_H
#define HELLCORE_PMM_H

#include <stdint.h>
#include "../include/hellcore_boot_info.h"

#define HELLCORE_PAGE_SIZE 4096ULL

typedef struct {
    uint64_t usable_range_count;
    uint64_t usable_page_count;
    uint64_t free_page_count;
    uint64_t allocated_page_count;
} HELLCORE_PMM_STATS;

int hellcore_pmm_init(
    const HELLCORE_BOOT_INFO *boot,
    HELLCORE_PMM_STATS *stats
);

uint64_t hellcore_pmm_alloc_page(void);

int hellcore_pmm_free_page(uint64_t physical_address);

void hellcore_pmm_get_stats(HELLCORE_PMM_STATS *stats);

#endif
