#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../kernel/vmm.c"

#define TEST_PAGE_MASK 0x000FFFFFFFFFF000ULL
#define TEST_PTE_USER  (1ULL << 2)

static void *allocated_pages[64];
static size_t allocated_count;

uint64_t hellcore_pmm_alloc_page(void)
{
    if (allocated_count >= 64)
        return 0;

    void *page = aligned_alloc(4096, 4096);
    if (!page)
        return 0;

    memset(page, 0, 4096);
    allocated_pages[allocated_count++] = page;

    return (uint64_t)(uintptr_t)page;
}

int hellcore_pmm_free_page(uint64_t address)
{
    for (size_t i = 0; i < allocated_count; ++i) {
        if (allocated_pages[i] &&
            (uint64_t)(uintptr_t)allocated_pages[i] == address) {
            free(allocated_pages[i]);
            allocated_pages[i] = NULL;
            return 1;
        }
    }

    return 0;
}

static int failures;
static int tests;

static void check(int condition, const char *name)
{
    ++tests;
    if (condition) {
        printf("PASS: %s\n", name);
    } else {
        printf("FAIL: %s\n", name);
        ++failures;
    }
}

static uint64_t *table_from_entry(uint64_t entry)
{
    return (uint64_t *)(uintptr_t)(entry & TEST_PAGE_MASK);
}

static int ancestors_have_user(uint64_t virtual_address)
{
    uint64_t pml4_index = (virtual_address >> 39) & 0x1FF;
    uint64_t pdpt_index = (virtual_address >> 30) & 0x1FF;
    uint64_t pd_index = (virtual_address >> 21) & 0x1FF;

    uint64_t e1 = root_table[pml4_index];
    if (!(e1 & 1) || !(e1 & TEST_PTE_USER))
        return 0;

    uint64_t *pdpt = table_from_entry(e1);
    uint64_t e2 = pdpt[pdpt_index];
    if (!(e2 & 1) || !(e2 & TEST_PTE_USER))
        return 0;

    uint64_t *pd = table_from_entry(e2);
    uint64_t e3 = pd[pd_index];

    return (e3 & 1) && (e3 & TEST_PTE_USER);
}

static uint64_t leaf_entry(uint64_t virtual_address)
{
    uint64_t pml4_index = (virtual_address >> 39) & 0x1FF;
    uint64_t pdpt_index = (virtual_address >> 30) & 0x1FF;
    uint64_t pd_index = (virtual_address >> 21) & 0x1FF;
    uint64_t pt_index = (virtual_address >> 12) & 0x1FF;

    uint64_t *pdpt = table_from_entry(root_table[pml4_index]);
    uint64_t *pd = table_from_entry(pdpt[pdpt_index]);
    uint64_t *pt = table_from_entry(pd[pd_index]);

    return pt[pt_index];
}

int main(void)
{
    uint64_t *root = (uint64_t *)(uintptr_t)
        hellcore_pmm_alloc_page();

    check(root != NULL, "allocate root page table");
    if (!root)
        return 1;

    hellcore_vmm_host_test_set_root(root);

    const uint64_t new_user_va = 0x0000010000000000ULL;
    const uint64_t shared_supervisor_va = 0x0000008000000000ULL;
    const uint64_t shared_user_va = shared_supervisor_va + 0x1000;

    uint64_t reported_flags = 0;

    /* Case 1: a new user mapping creates USER-enabled ancestors. */
    check(
        hellcore_vmm_map_page(
            new_user_va,
            0x00200000,
            HELLCORE_VMM_MAP_READ | HELLCORE_VMM_MAP_USER
        ),
        "map user page through newly created tables"
    );

    check(
        ancestors_have_user(new_user_va),
        "new mapping sets USER on all three intermediate levels"
    );

    check(
        (leaf_entry(new_user_va) & TEST_PTE_USER) != 0,
        "new user mapping sets USER on leaf"
    );

    /* Case 2: create supervisor-only ancestors first. */
    check(
        hellcore_vmm_map_page(
            shared_supervisor_va,
            0x00300000,
            HELLCORE_VMM_MAP_READ
        ),
        "create supervisor-only mapping"
    );

    check(
        !ancestors_have_user(shared_supervisor_va),
        "initial supervisor mapping keeps ancestors supervisor-only"
    );

    check(
        (leaf_entry(shared_supervisor_va) & TEST_PTE_USER) == 0,
        "initial supervisor leaf has USER clear"
    );

    /* Reuse those tables for a user mapping in the same 2 MiB region. */
    check(
        hellcore_vmm_map_page(
            shared_user_va,
            0x00301000,
            HELLCORE_VMM_MAP_READ | HELLCORE_VMM_MAP_USER
        ),
        "map user page through existing supervisor tables"
    );

    check(
        ancestors_have_user(shared_user_va),
        "existing intermediate levels are promoted to USER"
    );

    check(
        (leaf_entry(shared_user_va) & TEST_PTE_USER) != 0,
        "shared-branch user leaf has USER set"
    );

    check(
        (leaf_entry(shared_supervisor_va) & TEST_PTE_USER) == 0,
        "existing supervisor leaf remains supervisor-only"
    );

    /* Case 3: verify the public effective-permission API. */
    check(
        hellcore_vmm_get_flags(shared_user_va, &reported_flags) &&
        (reported_flags & HELLCORE_VMM_PAGE_USER),
        "get_flags reports USER for user mapping"
    );

    reported_flags = 0;

    check(
        hellcore_vmm_get_flags(
            shared_supervisor_va,
            &reported_flags
        ) &&
        !(reported_flags & HELLCORE_VMM_PAGE_USER),
        "get_flags denies USER for supervisor-only leaf"
    );

    reported_flags = 0;

    check(
        hellcore_vmm_get_flags(new_user_va, &reported_flags) &&
        (reported_flags & HELLCORE_VMM_PAGE_USER),
        "get_flags reports USER for new user branch"
    );

    printf("\nCORE11.1 USER permission tests: %d passed, %d failed\n",
           tests - failures, failures);

    for (size_t i = 0; i < allocated_count; ++i)
        free(allocated_pages[i]);

    return failures ? 1 : 0;
}
