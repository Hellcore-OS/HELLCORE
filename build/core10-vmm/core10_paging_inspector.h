#ifndef HELLCORE_CORE10_PAGING_INSPECTOR_H
#define HELLCORE_CORE10_PAGING_INSPECTOR_H

#include <stdint.h>
#include "../../include/hellcore_boot_info.h"

#define CORE10_MAX_MAPPING_CHECKS 10

typedef struct {
    const char *name;
    uint64_t virtual_address;
    uint64_t physical_address;
    uint64_t flags;
    uint32_t passed;
} CORE10_MAPPING_CHECK;

typedef struct {
    uint64_t cr3;
    uint32_t physical_bits;
    uint32_t count;
    uint32_t passed;
    uint32_t failed;
    CORE10_MAPPING_CHECK checks[CORE10_MAX_MAPPING_CHECKS];
} CORE10_PAGING_REPORT;

int hellcore_core10_inspect_inherited(
    const HELLCORE_BOOT_INFO *boot,
    CORE10_PAGING_REPORT *report
);

#endif
