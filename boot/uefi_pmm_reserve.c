#include "uefi_pmm_reserve.h"

#define EFI_SUCCESS 0ULL
#define EFI_BUFFER_TOO_SMALL (0x8000000000000000ULL | 5ULL)
#define EFI_LOADER_DATA 2U
#define EFI_CONVENTIONAL_MEMORY 7U
#define PAGE_SIZE 4096ULL

int hellcore_uefi_reserve_pmm_bitmaps(
    HELLCORE_ALLOCATE_PAGES allocate_pages,
    HELLCORE_GET_MEMORY_MAP get_memory_map,
    HELLCORE_ALLOCATE_POOL allocate_pool,
    HELLCORE_FREE_POOL free_pool,
    uint64_t *bitmap_address,
    uint64_t *bitmap_allocation_size)
{
    HELLCORE_UINTN map_size = 0;
    HELLCORE_UINTN map_key = 0;
    HELLCORE_UINTN descriptor_size = 0;
    uint32_t descriptor_version = 0;
    HELLCORE_EFI_MEMORY_DESCRIPTOR *map = 0;
    uint64_t highest_end = 0;
    uint64_t page_count;
    uint64_t bitmap_bytes;
    uint64_t total_bytes;
    uint64_t allocation_pages;
    uint64_t allocation_size;
    HELLCORE_EFI_STATUS status;

    if (!allocate_pages || !get_memory_map || !allocate_pool ||
        !free_pool || !bitmap_address || !bitmap_allocation_size)
        return 0;

    *bitmap_address = 0;
    *bitmap_allocation_size = 0;

    for (unsigned int attempt = 0; attempt < 5; ++attempt) {
        map_size = 0;
        status = get_memory_map(
            &map_size, 0, &map_key, &descriptor_size, &descriptor_version);

        if (status != EFI_BUFFER_TOO_SMALL ||
            descriptor_size < sizeof(HELLCORE_EFI_MEMORY_DESCRIPTOR) ||
            map_size == 0)
            return 0;

        if (map_size > UINT64_MAX - descriptor_size * 16ULL)
            return 0;

        map_size += descriptor_size * 16ULL;

        status = allocate_pool(EFI_LOADER_DATA, map_size, (void **)&map);
        if (status != EFI_SUCCESS || !map)
            return 0;

        {
            HELLCORE_UINTN actual_size = map_size;

            status = get_memory_map(
                &actual_size, map, &map_key,
                &descriptor_size, &descriptor_version);

            if (status == EFI_SUCCESS) {
                map_size = actual_size;
                break;
            }
        }

        free_pool(map);
        map = 0;
    }

    if (!map || status != EFI_SUCCESS ||
        descriptor_size < sizeof(HELLCORE_EFI_MEMORY_DESCRIPTOR) ||
        map_size % descriptor_size != 0) {
        if (map)
            free_pool(map);
        return 0;
    }

    for (uint64_t offset = 0; offset < map_size; offset += descriptor_size) {
        HELLCORE_EFI_MEMORY_DESCRIPTOR *d =
            (HELLCORE_EFI_MEMORY_DESCRIPTOR *)((uint8_t *)map + offset);

        if (d->Type != EFI_CONVENTIONAL_MEMORY || d->NumberOfPages == 0)
            continue;

        if (d->NumberOfPages > (UINT64_MAX - d->PhysicalStart) / PAGE_SIZE) {
            free_pool(map);
            return 0;
        }

        uint64_t end = d->PhysicalStart + d->NumberOfPages * PAGE_SIZE;

        if (end > highest_end)
            highest_end = end;
    }

    free_pool(map);

    if (highest_end == 0)
        return 0;

    page_count = highest_end / PAGE_SIZE;
    if (highest_end % PAGE_SIZE)
        ++page_count;

    if (page_count > UINT64_MAX - 7ULL)
        return 0;

    bitmap_bytes = (page_count + 7ULL) / 8ULL;

    if (bitmap_bytes > UINT64_MAX / 2ULL)
        return 0;

    total_bytes = bitmap_bytes * 2ULL;

    if (total_bytes > UINT64_MAX - (PAGE_SIZE - 1ULL))
        return 0;

    allocation_pages = (total_bytes + PAGE_SIZE - 1ULL) / PAGE_SIZE;

    if (allocation_pages == 0 ||
        allocation_pages > UINT64_MAX / PAGE_SIZE)
        return 0;

    allocation_size = allocation_pages * PAGE_SIZE;

    status = allocate_pages(
        0U, EFI_LOADER_DATA, allocation_pages, bitmap_address);

    if (status != EFI_SUCCESS || *bitmap_address == 0) {
        *bitmap_address = 0;
        return 0;
    }

    *bitmap_allocation_size = allocation_size;
    return 1;
}
