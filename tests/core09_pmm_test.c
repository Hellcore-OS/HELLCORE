#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>

#include "../kernel/pmm.h"

#define TEST_LOADER_DATA 2U
#define TEST_CONVENTIONAL 7U
#define TEST_PAGE 4096ULL

typedef struct {
    uint32_t Type;
    uint32_t Padding;
    uint64_t PhysicalStart;
    uint64_t VirtualStart;
    uint64_t NumberOfPages;
    uint64_t Attribute;
} TEST_DESCRIPTOR;

#define CHECK(x, msg) do { \
    if (!(x)) { \
        printf("FAIL: %s\n", msg); \
        return 1; \
    } \
    printf("PASS: %s\n", msg); \
} while (0)

int main(void) {
    TEST_DESCRIPTOR map[4] = {0};

    map[0].Type = TEST_CONVENTIONAL;
    map[0].PhysicalStart = 0x80000;
    map[0].NumberOfPages = 128;

    map[1].Type = TEST_CONVENTIONAL;
    map[1].PhysicalStart = 0x100000;
    map[1].NumberOfPages = 16;

    map[2].Type = TEST_CONVENTIONAL;
    map[2].PhysicalStart = 0x200000;
    map[2].NumberOfPages = 16;

    map[3].Type = TEST_LOADER_DATA;
    map[3].PhysicalStart = 0;
    map[3].NumberOfPages = 1;

    uint8_t bitmap[4096] __attribute__((aligned(4096)));
    memset(bitmap, 0, sizeof(bitmap));
    map[3].PhysicalStart = (uint64_t)(uintptr_t)bitmap;

    HELLCORE_BOOT_INFO boot = {0};

    boot.magic = HELLCORE_BOOT_MAGIC;
    boot.version = HELLCORE_BOOT_VERSION;
    boot.structure_size = sizeof(boot);

    boot.memory_map_address = (uint64_t)(uintptr_t)map;
    boot.memory_map_size = sizeof(map);
    boot.descriptor_size = sizeof(TEST_DESCRIPTOR);
    boot.descriptor_version = 1;

    boot.pmm_bitmap_address = (uint64_t)(uintptr_t)bitmap;
    boot.pmm_bitmap_size = sizeof(bitmap);

    HELLCORE_PMM_STATS stats = {0};

    CHECK(
        hellcore_pmm_init(&boot, &stats),
        "PMM initializes with synthetic UEFI memory map"
    );

    CHECK(
        stats.usable_range_count == 2,
        "Two usable conventional-memory regions detected"
    );

    CHECK(
        stats.usable_page_count == 32,
        "Exactly 32 pages managed above 1 MiB"
    );

    CHECK(
        stats.free_page_count == 32 &&
        stats.allocated_page_count == 0,
        "Initial PMM accounting is correct"
    );

    uint64_t first = hellcore_pmm_alloc_page();

    CHECK(first >= 0x100000,
          "Allocation never returns memory below 1 MiB");

    CHECK(first % TEST_PAGE == 0,
          "First allocation is page-aligned");

    uint64_t allocated[32] = {0};
    allocated[0] = first;

    for (unsigned i = 1; i < 32; ++i) {
        allocated[i] = hellcore_pmm_alloc_page();

        CHECK(allocated[i] != 0,
              "Allocation succeeds until all pages are exhausted");

        CHECK(allocated[i] >= 0x100000 &&
              allocated[i] % TEST_PAGE == 0,
              "Every allocation respects the address boundary");
    }

    CHECK(
        hellcore_pmm_alloc_page() == 0,
        "Exhausted allocator returns zero"
    );

    hellcore_pmm_get_stats(&stats);

    CHECK(
        stats.free_page_count == 0 &&
        stats.allocated_page_count == 32,
        "Exhaustion accounting is correct"
    );

    for (unsigned i = 0; i < 32; ++i) {
        for (unsigned j = i + 1; j < 32; ++j) {
            CHECK(
                allocated[i] != allocated[j],
                "Allocated physical pages are unique"
            );
        }
    }

    CHECK(
        hellcore_pmm_free_page(first),
        "Valid allocated page can be freed"
    );

    CHECK(
        !hellcore_pmm_free_page(first),
        "Double-free is rejected"
    );

    CHECK(
        !hellcore_pmm_free_page(0x80000),
        "Free below 1 MiB is rejected"
    );

    CHECK(
        !hellcore_pmm_free_page(0x100001),
        "Unaligned free is rejected"
    );

    CHECK(
        !hellcore_pmm_free_page(0x300000),
        "Unmanaged page free is rejected"
    );

    uint64_t reused = hellcore_pmm_alloc_page();

    CHECK(
        reused == first,
        "Freed page can be allocated again"
    );

    hellcore_pmm_get_stats(&stats);

    CHECK(
        stats.free_page_count == 0 &&
        stats.allocated_page_count == 32,
        "Final PMM accounting is consistent"
    );

    printf("\nCORE 0.9 PMM TESTS COMPLETED\n");
    return 0;
}
