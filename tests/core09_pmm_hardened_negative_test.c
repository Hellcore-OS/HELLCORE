#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <inttypes.h>
#include "../kernel/pmm.h"

#define LOADER_DATA 2U
#define CONVENTIONAL 7U
#define PAGE_SIZE 4096ULL

typedef struct {
    uint32_t Type;
    uint32_t Padding;
    uint64_t PhysicalStart;
    uint64_t VirtualStart;
    uint64_t NumberOfPages;
    uint64_t Attribute;
} TEST_DESCRIPTOR;

typedef struct {
    TEST_DESCRIPTOR map[5];
    uint8_t bitmap[4096] __attribute__((aligned(4096)));
    HELLCORE_BOOT_INFO boot;
    HELLCORE_PMM_STATS stats;
} TEST_FIXTURE;

static int passed;
static int failed;

static void report(const char *name, int result) {
    if (result) {
        ++passed;
        printf("PASS: %s\n", name);
    } else {
        ++failed;
        printf("FAIL: %s\n", name);
    }
}

static void setup(TEST_FIXTURE *f) {
    memset(f, 0, sizeof(*f));

    f->map[0].Type = CONVENTIONAL;
    f->map[0].PhysicalStart = 0x80000;
    f->map[0].NumberOfPages = 128;

    f->map[1].Type = CONVENTIONAL;
    f->map[1].PhysicalStart = 0x100000;
    f->map[1].NumberOfPages = 16;

    f->map[2].Type = CONVENTIONAL;
    f->map[2].PhysicalStart = 0x200000;
    f->map[2].NumberOfPages = 16;

    f->map[3].Type = CONVENTIONAL;
    f->map[3].PhysicalStart = 0x300000;
    f->map[3].NumberOfPages = 16;

    f->map[4].Type = LOADER_DATA;
    f->map[4].PhysicalStart = (uint64_t)(uintptr_t)f->bitmap;
    f->map[4].NumberOfPages = 1;

    f->boot.magic = HELLCORE_BOOT_MAGIC;
    f->boot.version = HELLCORE_BOOT_VERSION;
    f->boot.structure_size = sizeof(f->boot);
    f->boot.memory_map_address = (uint64_t)(uintptr_t)f->map;
    f->boot.memory_map_size = sizeof(f->map);
    f->boot.descriptor_size = sizeof(TEST_DESCRIPTOR);
    f->boot.descriptor_version = 1;
    f->boot.pmm_bitmap_address = (uint64_t)(uintptr_t)f->bitmap;
    f->boot.pmm_bitmap_size = sizeof(f->bitmap);
}

static int test_overlap(void) {
    TEST_FIXTURE f;
    setup(&f);

    f.map[2].PhysicalStart = 0x108000;

    return !hellcore_pmm_init(&f.boot, &f.stats);
}

static int test_descriptor_size(void) {
    TEST_FIXTURE f;
    setup(&f);

    f.boot.descriptor_size = sizeof(TEST_DESCRIPTOR) - 1;
    f.boot.memory_map_size = f.boot.descriptor_size * 5;

    return !hellcore_pmm_init(&f.boot, &f.stats);
}

static int test_small_bitmap(void) {
    TEST_FIXTURE f;
    setup(&f);

    f.map[3].PhysicalStart = 0x100000000ULL;
    f.map[3].NumberOfPages = 1;

    return !hellcore_pmm_init(&f.boot, &f.stats);
}

static int test_bitmap_not_loader_data(void) {
    TEST_FIXTURE f;
    setup(&f);

    f.map[4].Type = 0;

    return !hellcore_pmm_init(&f.boot, &f.stats);
}

static int test_cross_type_overlap(void) {
    TEST_FIXTURE f;
    setup(&f);

    /* Conventional memory overlaps the LoaderData bitmap descriptor. */
    f.map[3].PhysicalStart = (uint64_t)(uintptr_t)f.bitmap;
    f.map[3].NumberOfPages = 1;

    return !hellcore_pmm_init(&f.boot, &f.stats);
}

static int test_map_overflow(void) {
    TEST_FIXTURE f;
    setup(&f);

    f.boot.memory_map_address = UINT64_MAX - 8;

    return !hellcore_pmm_init(&f.boot, &f.stats);
}

static int test_failed_reinitialization(void) {
    TEST_FIXTURE f;
    setup(&f);

    if (!hellcore_pmm_init(&f.boot, &f.stats))
        return 0;

    f.map[2].PhysicalStart = 0x108000;

    if (hellcore_pmm_init(&f.boot, &f.stats))
        return 0;

    return hellcore_pmm_alloc_page() == 0;
}

int main(void) {
    report("Overlapping conventional descriptors rejected",
           test_overlap());

    report("Invalid descriptor size rejected",
           test_descriptor_size());

    report("Insufficient bitmap storage rejected",
           test_small_bitmap());

    report("Bitmap outside loader-data memory rejected",
           test_bitmap_not_loader_data());

    report("Conventional/LoaderData overlap rejected",
           test_cross_type_overlap());

    report("Memory-map arithmetic overflow rejected",
           test_map_overflow());

    report("Failed reinitialization disables allocator",
           test_failed_reinitialization());

    printf("\nCORE 0.9 PMM NEGATIVE TEST SUMMARY\n");
    printf("Passed: %d\n", passed);
    printf("Failed: %d\n", failed);

    return failed == 0 ? 0 : 1;
}
