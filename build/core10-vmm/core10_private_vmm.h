#ifndef HELLCORE_CORE10_PRIVATE_VMM_H
#define HELLCORE_CORE10_PRIVATE_VMM_H

#include <stdint.h>

#define CORE10_VMM_READ    (1ULL << 0)
#define CORE10_VMM_WRITE   (1ULL << 1)
#define CORE10_VMM_EXECUTE (1ULL << 2)
#define CORE10_VMM_USER    (1ULL << 3)

#define CORE10_VMM_FLAG_WRITE (1ULL << 0)
#define CORE10_VMM_FLAG_USER  (1ULL << 1)
#define CORE10_VMM_FLAG_NX    (1ULL << 2)

int hellcore_core10_vmm_init(uint64_t root, uint32_t physical_bits);

int hellcore_core10_vmm_find_private_address(uint64_t *address);

int hellcore_core10_vmm_map_page(
    uint64_t virtual_address,
    uint64_t physical_address,
    uint64_t flags
);

int hellcore_core10_vmm_unmap_page(uint64_t virtual_address);

int hellcore_core10_vmm_translate(
    uint64_t virtual_address,
    uint64_t *physical_address
);

int hellcore_core10_vmm_get_flags(
    uint64_t virtual_address,
    uint64_t *flags
);

uint64_t hellcore_core10_vmm_root_entry(uint32_t index);

void hellcore_core10_vmm_test_fail_after(int32_t allocations);

#endif
