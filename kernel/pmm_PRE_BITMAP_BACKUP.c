#include "pmm.h"

#define EFI_CONVENTIONAL_MEMORY 7ULL
#define HELLCORE_PAGE_SIZE 4096ULL

typedef struct {
    uint32_t Type;
    uint32_t Padding;
    uint64_t PhysicalStart;
    uint64_t VirtualStart;
    uint64_t NumberOfPages;
    uint64_t Attribute;
} HELLCORE_EFI_MEMORY_DESCRIPTOR;

int hellcore_pmm_init(
    const HELLCORE_BOOT_INFO *boot,
    HELLCORE_PMM_STATS *stats
) {
    if (!boot || !stats)
        return 0;

    stats->usable_range_count = 0;
    stats->usable_page_count = 0;

    if (boot->magic != HELLCORE_BOOT_MAGIC ||
        boot->version != HELLCORE_BOOT_VERSION ||
        boot->structure_size < sizeof(HELLCORE_BOOT_INFO))
        return 0;

    if (!boot->memory_map_address ||
        !boot->memory_map_size ||
        boot->descriptor_size < sizeof(HELLCORE_EFI_MEMORY_DESCRIPTOR) ||
        boot->descriptor_size > boot->memory_map_size)
        return 0;

    if (boot->memory_map_size % boot->descriptor_size != 0)
        return 0;

    if (boot->memory_map_address >
        UINT64_MAX - boot->memory_map_size)
        return 0;

    const uint8_t *map =
        (const uint8_t *)(uintptr_t)boot->memory_map_address;

    uint64_t descriptor_count =
        boot->memory_map_size / boot->descriptor_size;

    for (uint64_t i = 0; i < descriptor_count; ++i) {
        const HELLCORE_EFI_MEMORY_DESCRIPTOR *descriptor =
            (const HELLCORE_EFI_MEMORY_DESCRIPTOR *)
            (map + i * boot->descriptor_size);

        if (descriptor->Type != EFI_CONVENTIONAL_MEMORY)
            continue;

        if (descriptor->NumberOfPages == 0)
            continue;

        if (descriptor->PhysicalStart % HELLCORE_PAGE_SIZE != 0)
            return 0;

        if (descriptor->NumberOfPages >
            UINT64_MAX / HELLCORE_PAGE_SIZE)
            return 0;

        uint64_t bytes =
            descriptor->NumberOfPages * HELLCORE_PAGE_SIZE;

        if (descriptor->PhysicalStart > UINT64_MAX - bytes)
            return 0;

        if (stats->usable_range_count == UINT64_MAX ||
            stats->usable_page_count >
                UINT64_MAX - descriptor->NumberOfPages)
            return 0;

        stats->usable_range_count++;
        stats->usable_page_count += descriptor->NumberOfPages;
    }

    return 1;
}
