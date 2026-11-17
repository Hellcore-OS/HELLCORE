#ifndef HELLCORE_CORE10_PAGING_DIAG_H
#define HELLCORE_CORE10_PAGING_DIAG_H

#include <stdint.h>

typedef struct {
    uint64_t cr0;
    uint64_t cr3;
    uint64_t cr4;
    uint64_t efer;
    uint32_t physical_address_bits;
    uint32_t nx_supported;
    uint32_t nx_enabled;
    uint32_t la57_supported;
    uint32_t la57_active;
} HELLCORE_CORE10_PAGING_STATE;

int hellcore_core10_read_paging_state(
    HELLCORE_CORE10_PAGING_STATE *state
);

#endif
