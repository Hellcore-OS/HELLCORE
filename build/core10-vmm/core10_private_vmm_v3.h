#ifndef HELLCORE_CORE10_PRIVATE_VMM_V3_H
#define HELLCORE_CORE10_PRIVATE_VMM_V3_H

#include <stdint.h>

#define CORE10_V3_READ    (1ULL << 0)
#define CORE10_V3_WRITE   (1ULL << 1)
#define CORE10_V3_EXECUTE (1ULL << 2)
#define CORE10_V3_USER    (1ULL << 3)

#define CORE10_V3_FLAG_WRITE (1ULL << 0)
#define CORE10_V3_FLAG_USER  (1ULL << 1)
#define CORE10_V3_FLAG_NX    (1ULL << 2)

typedef struct {
    uint32_t reserved_slots;
    uint32_t owned_tables;
    uint32_t mappings_created;
    uint32_t mappings_removed;
    uint32_t allocation_rollbacks;
    uint32_t ownership_failures;
} CORE10_V3_REPORT;

int hellcore_core10_v3_init(uint64_t root, uint32_t physical_bits);
int hellcore_core10_v3_find_private_address(uint64_t *address);

int hellcore_core10_v3_map_page(
    uint64_t virtual_address,
    uint64_t physical_address,
    uint64_t flags
);

int hellcore_core10_v3_unmap_page(uint64_t virtual_address);

int hellcore_core10_v3_translate(
    uint64_t virtual_address,
    uint64_t *physical_address
);

int hellcore_core10_v3_get_flags(
    uint64_t virtual_address,
    uint64_t *flags
);

uint64_t hellcore_core10_v3_root_entry(uint32_t index);
int hellcore_core10_v3_is_owned(uint64_t physical_address);

void hellcore_core10_v3_test_fail_after(int32_t allocations);
void hellcore_core10_v3_get_report(CORE10_V3_REPORT *report);

#endif
