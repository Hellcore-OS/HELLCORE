#ifndef HELLCORE_PMM_H
#define HELLCORE_PMM_H

#include <stdint.h>
#include "../include/hellcore_boot_info.h"

#define HELLCORE_PMM_MAX_RANGES 128

typedef struct {
    uint64_t usable_range_count;
    uint64_t usable_page_count;
} HELLCORE_PMM_STATS;

int hellcore_pmm_init(
    const HELLCORE_BOOT_INFO *boot,
    HELLCORE_PMM_STATS *stats
);

#endif
