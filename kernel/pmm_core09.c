#include "pmm.h"

#define EFI_LOADER_DATA 2U
#define EFI_CONVENTIONAL_MEMORY 7U
#define PMM_MIN_ADDRESS 0x100000ULL
#define PMM_FIRST_PAGE (PMM_MIN_ADDRESS / HELLCORE_PAGE_SIZE)

typedef struct {
    uint32_t Type;
    uint32_t Padding;
    uint64_t PhysicalStart;
    uint64_t VirtualStart;
    uint64_t NumberOfPages;
    uint64_t Attribute;
} HELLCORE_EFI_MEMORY_DESCRIPTOR;

static uint8_t *available_map;
static uint8_t *managed_map;
static uint64_t bitmap_capacity;
static HELLCORE_PMM_STATS current_stats;
static int allocator_ready;

static int bit_get(const uint8_t *map, uint64_t bit) {
    return (map[bit >> 3] >> (bit & 7)) & 1;
}

static void bit_set(uint8_t *map, uint64_t bit) {
    map[bit >> 3] |= (uint8_t)(1U << (bit & 7));
}

static void bit_clear(uint8_t *map, uint64_t bit) {
    map[bit >> 3] &= (uint8_t)~(1U << (bit & 7));
}

static int descriptor_end(
    const HELLCORE_EFI_MEMORY_DESCRIPTOR *d,
    uint64_t *end
) {
    if (!d->NumberOfPages ||
        d->NumberOfPages > UINT64_MAX / HELLCORE_PAGE_SIZE)
        return 0;

    uint64_t bytes = d->NumberOfPages * HELLCORE_PAGE_SIZE;

    if (d->PhysicalStart > UINT64_MAX - bytes)
        return 0;

    *end = d->PhysicalStart + bytes;
    return 1;
}

/* Bitmap backing must be covered by EFI_LOADER_DATA descriptors. */
static int range_is_loader_data(
    const HELLCORE_BOOT_INFO *boot,
    uint64_t start,
    uint64_t end
) {
    const uint8_t *map =
        (const uint8_t *)(uintptr_t)boot->memory_map_address;

    uint64_t count = boot->memory_map_size / boot->descriptor_size;
    uint64_t cursor = start;

    while (cursor < end) {
        uint64_t furthest = cursor;

        for (uint64_t i = 0; i < count; ++i) {
            const HELLCORE_EFI_MEMORY_DESCRIPTOR *d =
                (const HELLCORE_EFI_MEMORY_DESCRIPTOR *)
                (map + i * boot->descriptor_size);

            if (d->Type != EFI_LOADER_DATA || !d->NumberOfPages)
                continue;

            uint64_t descriptor_limit;

            if (!descriptor_end(d, &descriptor_limit))
                return 0;

            if (d->PhysicalStart <= cursor &&
                descriptor_limit > furthest)
                furthest = descriptor_limit;
        }

        if (furthest == cursor)
            return 0;

        cursor = furthest;
    }

    return 1;
}

/* Reject overlapping conventional-memory descriptors. */
static int conventional_ranges_valid(
    const HELLCORE_BOOT_INFO *boot
) {
    const uint8_t *map =
        (const uint8_t *)(uintptr_t)boot->memory_map_address;

    uint64_t count = boot->memory_map_size / boot->descriptor_size;

    for (uint64_t i = 0; i < count; ++i) {
        const HELLCORE_EFI_MEMORY_DESCRIPTOR *a =
            (const HELLCORE_EFI_MEMORY_DESCRIPTOR *)
            (map + i * boot->descriptor_size);

        if (a->Type != EFI_CONVENTIONAL_MEMORY ||
            !a->NumberOfPages)
            continue;

        uint64_t a_end;

        if (a->PhysicalStart % HELLCORE_PAGE_SIZE ||
            !descriptor_end(a, &a_end))
            return 0;

        for (uint64_t j = i + 1; j < count; ++j) {
            const HELLCORE_EFI_MEMORY_DESCRIPTOR *b =
                (const HELLCORE_EFI_MEMORY_DESCRIPTOR *)
                (map + j * boot->descriptor_size);

            if (b->Type != EFI_CONVENTIONAL_MEMORY ||
                !b->NumberOfPages)
                continue;

            uint64_t b_end;

            if (b->PhysicalStart % HELLCORE_PAGE_SIZE ||
                !descriptor_end(b, &b_end))
                return 0;

            if (a->PhysicalStart < b_end &&
                b->PhysicalStart < a_end)
                return 0;
        }
    }

    return 1;
}

int hellcore_pmm_init(
    const HELLCORE_BOOT_INFO *boot,
    HELLCORE_PMM_STATS *stats
) {
    allocator_ready = 0;

    if (stats) {
        stats->usable_range_count = 0;
        stats->usable_page_count = 0;
        stats->free_page_count = 0;
        stats->allocated_page_count = 0;
    }

    if (!boot || !stats)
        return 0;

    if (boot->magic != HELLCORE_BOOT_MAGIC ||
        boot->version != HELLCORE_BOOT_VERSION ||
        boot->structure_size < sizeof(HELLCORE_BOOT_INFO))
        return 0;

    if (!boot->memory_map_address ||
        !boot->memory_map_size ||
        !boot->descriptor_size ||
        boot->descriptor_size <
            sizeof(HELLCORE_EFI_MEMORY_DESCRIPTOR) ||
        boot->descriptor_size > boot->memory_map_size ||
        boot->memory_map_size % boot->descriptor_size)
        return 0;

    if (boot->memory_map_address >
        UINT64_MAX - boot->memory_map_size)
        return 0;

    if (!boot->pmm_bitmap_address ||
        !boot->pmm_bitmap_size ||
        boot->pmm_bitmap_address % HELLCORE_PAGE_SIZE ||
        boot->pmm_bitmap_size % HELLCORE_PAGE_SIZE ||
        boot->pmm_bitmap_address >
            UINT64_MAX - boot->pmm_bitmap_size)
        return 0;

    if (!conventional_ranges_valid(boot))
        return 0;

    const uint8_t *map =
        (const uint8_t *)(uintptr_t)boot->memory_map_address;

    uint64_t count = boot->memory_map_size / boot->descriptor_size;
    uint64_t highest_page = 0;
    uint64_t ranges = 0;
    uint64_t pages = 0;

    for (uint64_t i = 0; i < count; ++i) {
        const HELLCORE_EFI_MEMORY_DESCRIPTOR *d =
            (const HELLCORE_EFI_MEMORY_DESCRIPTOR *)
            (map + i * boot->descriptor_size);

        if (d->Type != EFI_CONVENTIONAL_MEMORY ||
            !d->NumberOfPages)
            continue;

        uint64_t end;

        if (!descriptor_end(d, &end))
            return 0;

        uint64_t end_page = end / HELLCORE_PAGE_SIZE;

        if (end_page > highest_page)
            highest_page = end_page;

        uint64_t first_page =
            d->PhysicalStart / HELLCORE_PAGE_SIZE;

        if (first_page < PMM_FIRST_PAGE)
            first_page = PMM_FIRST_PAGE;

        if (end_page <= first_page)
            continue;

        uint64_t eligible_pages = end_page - first_page;

        if (pages > UINT64_MAX - eligible_pages ||
            ranges == UINT64_MAX)
            return 0;

        pages += eligible_pages;
        ++ranges;
    }

    if (highest_page <= PMM_FIRST_PAGE ||
        highest_page > UINT64_MAX - 7)
        return 0;

    uint64_t bitmap_bytes = (highest_page + 7) / 8;

    if (bitmap_bytes > UINT64_MAX / 2 ||
        bitmap_bytes * 2 > boot->pmm_bitmap_size)
        return 0;

    uint64_t bitmap_end =
        boot->pmm_bitmap_address + boot->pmm_bitmap_size;

    if (!range_is_loader_data(
            boot, boot->pmm_bitmap_address, bitmap_end))
        return 0;

    bitmap_capacity = highest_page;

    available_map =
        (uint8_t *)(uintptr_t)boot->pmm_bitmap_address;

    managed_map = available_map + bitmap_bytes;

    for (uint64_t i = 0; i < bitmap_bytes; ++i) {
        available_map[i] = 0xFF;
        managed_map[i] = 0;
    }

    current_stats.usable_range_count = ranges;
    current_stats.usable_page_count = pages;
    current_stats.free_page_count = 0;
    current_stats.allocated_page_count = 0;

    for (uint64_t i = 0; i < count; ++i) {
        const HELLCORE_EFI_MEMORY_DESCRIPTOR *d =
            (const HELLCORE_EFI_MEMORY_DESCRIPTOR *)
            (map + i * boot->descriptor_size);

        if (d->Type != EFI_CONVENTIONAL_MEMORY ||
            !d->NumberOfPages)
            continue;

        uint64_t first_page =
            d->PhysicalStart / HELLCORE_PAGE_SIZE;

        if (first_page < PMM_FIRST_PAGE)
            first_page = PMM_FIRST_PAGE;

        uint64_t end_page =
            (d->PhysicalStart / HELLCORE_PAGE_SIZE) +
            d->NumberOfPages;

        for (uint64_t p = first_page; p < end_page; ++p) {
            bit_clear(available_map, p);
            bit_set(managed_map, p);
            ++current_stats.free_page_count;
        }
    }

    if (current_stats.free_page_count != pages)
        return 0;

    *stats = current_stats;
    allocator_ready = 1;
    return 1;
}

uint64_t hellcore_pmm_alloc_page(void) {
    if (!allocator_ready)
        return 0;

    for (uint64_t i = PMM_FIRST_PAGE; i < bitmap_capacity; ++i) {
        if (bit_get(managed_map, i) &&
            !bit_get(available_map, i)) {
            bit_set(available_map, i);
            --current_stats.free_page_count;
            ++current_stats.allocated_page_count;
            return i * HELLCORE_PAGE_SIZE;
        }
    }

    return 0;
}

int hellcore_pmm_free_page(uint64_t physical_address) {
    if (!allocator_ready ||
        physical_address < PMM_MIN_ADDRESS ||
        physical_address % HELLCORE_PAGE_SIZE)
        return 0;

    uint64_t index = physical_address / HELLCORE_PAGE_SIZE;

    if (index >= bitmap_capacity ||
        !bit_get(managed_map, index) ||
        !bit_get(available_map, index))
        return 0;

    bit_clear(available_map, index);
    --current_stats.allocated_page_count;
    ++current_stats.free_page_count;

    return 1;
}

void hellcore_pmm_get_stats(HELLCORE_PMM_STATS *stats) {
    if (stats && allocator_ready)
        *stats = current_stats;
}
