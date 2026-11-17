#ifndef HELLCORE_CORE10_VMM_TAKEOVER_H
#define HELLCORE_CORE10_VMM_TAKEOVER_H

#include <stdint.h>

typedef struct {
    uint64_t old_cr3;
    uint64_t new_cr3;
    uint64_t new_root;
    uint32_t physical_bits;
    uint32_t entries_checked;
    uint32_t root_validated;
    uint32_t switched;
    uint32_t readback_validated;
} CORE10_VMM_TAKEOVER_REPORT;

int hellcore_core10_takeover_root(
    CORE10_VMM_TAKEOVER_REPORT *report
);

#endif
