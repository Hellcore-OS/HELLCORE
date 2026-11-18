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

    return (uint64_t)((uintptr_t)page & 0x00FFFFFFFFFFFFFFULL);
}

int hellcore_pmm_free_page(uint64_t address)
{
    for (size_t i = 0; i < allocated_count; ++i) {
        if (allocated_pages[i] &&
            ((uint64_t)(uintptr_t)allocated_pages[i] & 0x00FFFFFFFFFFFFFFULL) == address) {
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


static uint64_t *entry_pointer(uint64_t va, unsigned level)
{
    uint64_t a = (va >> 39) & 0x1FF;
    uint64_t b = (va >> 30) & 0x1FF;
    uint64_t c = (va >> 21) & 0x1FF;
    uint64_t d = (va >> 12) & 0x1FF;

    uint64_t *e4 = &root_table[a];
    if (level == 4) return e4;
    if (!(*e4 & PTE_PRESENT)) return NULL;

    uint64_t *pdpt = table_from_entry(*e4);
    uint64_t *e3 = &pdpt[b];
    if (level == 3) return e3;
    if (!(*e3 & PTE_PRESENT)) return NULL;

    uint64_t *pd = table_from_entry(*e3);
    uint64_t *e2 = &pd[c];
    if (level == 2) return e2;
    if (!(*e2 & PTE_PRESENT) || (*e2 & PTE_HUGE))
        return NULL;

    uint64_t *pt = table_from_entry(*e2);
    if (level == 1) return &pt[d];

    return NULL;
}

static void check_nx_level(
    uint64_t va,
    unsigned level,
    const char *name
) {
    uint64_t *entry = entry_pointer(va, level);
    if (!entry) {
        check(0, name);
        return;
    }

    uint64_t original = *entry;
    *entry |= PTE_NX;

    uint64_t flags = 0;
    int ok = hellcore_vmm_get_flags(va, &flags);

    check(ok && (flags & HELLCORE_VMM_PAGE_NX), name);

    *entry = original;
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
    uint64_t translated = UINT64_MAX;

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


    /* CORE11.2: NX must be restrictive at every paging level. */
    const uint64_t nx_va = 0x0000020000000000ULL;

    check(
        hellcore_vmm_map_page(
            nx_va, 0x00400000,
            HELLCORE_VMM_MAP_READ | HELLCORE_VMM_MAP_EXECUTE
        ),
        "create executable 4 KiB mapping for NX tests"
    );

    check(
        entry_pointer(nx_va, 4) &&
        entry_pointer(nx_va, 3) &&
        entry_pointer(nx_va, 2) &&
        entry_pointer(nx_va, 1),
        "locate all four 4 KiB paging levels"
    );


    reported_flags = 0;
    check(
        hellcore_vmm_get_flags(nx_va, &reported_flags) &&
        !(reported_flags & HELLCORE_VMM_PAGE_NX),
        "executable 4 KiB mapping initially reports NX clear"
    );

    check_nx_level(nx_va, 4, "PML4 NX restricts 4 KiB mapping");
    check_nx_level(nx_va, 3, "PDPT NX restricts 4 KiB mapping");
    check_nx_level(nx_va, 2, "PD NX restricts 4 KiB mapping");
    check_nx_level(nx_va, 1, "PT NX restricts 4 KiB mapping");

    /* Build a normal path, then replace its PDE with a 2 MiB leaf. */
    const uint64_t huge_va = 0x0000030000000000ULL;

    check(
        hellcore_vmm_map_page(
            huge_va, 0x00400000,
            HELLCORE_VMM_MAP_READ | HELLCORE_VMM_MAP_EXECUTE
        ),
        "create page-table path for 2 MiB huge-page test"
    );

    uint64_t *huge_pde = entry_pointer(huge_va, 2);
    check(huge_pde != NULL, "locate PDE for 2 MiB huge page");

    if (huge_pde) {
        uint64_t saved_pde = *huge_pde;

        *huge_pde = 0x00800000ULL | PTE_PRESENT | PTE_HUGE;

        reported_flags = 0;
        check(
            hellcore_vmm_get_flags(huge_va, &reported_flags) &&
            (reported_flags & HELLCORE_VMM_PAGE_HUGE) &&
            !(reported_flags & HELLCORE_VMM_PAGE_NX),
            "executable 2 MiB huge page initially reports NX clear"
        );

        check_nx_level(huge_va, 4, "PML4 NX restricts 2 MiB huge page");
        check_nx_level(huge_va, 3, "PDPT NX restricts 2 MiB huge page");
        check_nx_level(huge_va, 2, "huge PDE NX restricts huge page");

        *huge_pde = saved_pde;
    }


    /* CORE11.3: 1 GiB PDPT huge-page translation and permissions. */
    const uint64_t giga_va = 0x0000040000000000ULL;
    const uint64_t giga_base = 0x0000000080000000ULL;
    const uint64_t giga_offset = 0x0000000023456789ULL;

    check(
        hellcore_vmm_map_page(
            giga_va, 0x00400000,
            HELLCORE_VMM_MAP_READ | HELLCORE_VMM_MAP_EXECUTE |
            HELLCORE_VMM_MAP_WRITE | HELLCORE_VMM_MAP_USER
        ),
        "create page-table path for 1 GiB huge-page test"
    );

    uint64_t giga_pml4_index = (giga_va >> 39) & 0x1FF;
    uint64_t giga_pdpt_index = (giga_va >> 30) & 0x1FF;
    uint64_t *giga_pdpt = table_from_entry(root_table[giga_pml4_index]);
    uint64_t *giga_pdpte = &giga_pdpt[giga_pdpt_index];
    uint64_t saved_pdpte = *giga_pdpte;

    *giga_pdpte = giga_base | PTE_PRESENT | PTE_WRITE |
                  PTE_USER | PTE_HUGE;

    uint64_t giga_test_va = giga_va | giga_offset;

    translated = UINT64_MAX;
    check(
        hellcore_vmm_translate(giga_va, &translated) &&
        translated == giga_base,
        "1 GiB translation returns physical base"
    );

    translated = UINT64_MAX;
    check(
        hellcore_vmm_translate(giga_test_va, &translated) &&
        translated == (giga_base | giga_offset),
        "1 GiB translation preserves 30-bit page offset"
    );

    reported_flags = 0;
    check(
        hellcore_vmm_get_flags(giga_test_va, &reported_flags) &&
        (reported_flags & HELLCORE_VMM_PAGE_PRESENT) &&
        (reported_flags & HELLCORE_VMM_PAGE_HUGE) &&
        (reported_flags & HELLCORE_VMM_PAGE_WRITE) &&
        (reported_flags & HELLCORE_VMM_PAGE_USER) &&
        !(reported_flags & HELLCORE_VMM_PAGE_NX),
        "1 GiB huge page reports effective permissions"
    );

    uint64_t saved_pml4e = root_table[giga_pml4_index];

    root_table[giga_pml4_index] |= PTE_NX;
    reported_flags = 0;
    check(
        hellcore_vmm_get_flags(giga_test_va, &reported_flags) &&
        (reported_flags & HELLCORE_VMM_PAGE_NX),
        "PML4 NX restricts 1 GiB huge page"
    );
    root_table[giga_pml4_index] = saved_pml4e;

    *giga_pdpte |= PTE_NX;
    reported_flags = 0;
    check(
        hellcore_vmm_get_flags(giga_test_va, &reported_flags) &&
        (reported_flags & HELLCORE_VMM_PAGE_NX),
        "PDPT huge-entry NX restricts 1 GiB page"
    );

    *giga_pdpte = saved_pdpte;


    /* CORE11.4: 4 KiB translation. */
    const uint64_t va4 = 0x0000050000000000ULL;
    const uint64_t pa4 = 0x00A00000ULL;
    const uint64_t offset4 = 0xABCULL;

    check(
        hellcore_vmm_map_page(
            va4, pa4,
            HELLCORE_VMM_MAP_READ | HELLCORE_VMM_MAP_EXECUTE
        ),
        "create 4 KiB translation mapping"
    );

    translated = UINT64_MAX;
    check(
        hellcore_vmm_translate(va4, &translated) &&
        translated == pa4,
        "4 KiB translation returns physical base"
    );

    translated = UINT64_MAX;
    check(
        hellcore_vmm_translate(va4 | offset4, &translated) &&
        translated == (pa4 | offset4),
        "4 KiB translation preserves page offset"
    );

    /* CORE11.4: 2 MiB huge-page translation. */
    const uint64_t va2m = 0x0000070000000000ULL;
    const uint64_t pa2m = 0x01200000ULL;
    const uint64_t offset2m = 0x001ABCDEULL;

    check(
        hellcore_vmm_map_page(
            va2m, 0x00C00000ULL,
            HELLCORE_VMM_MAP_READ | HELLCORE_VMM_MAP_EXECUTE
        ),
        "create page-table path for 2 MiB translation"
    );

    uint64_t *pde2m = entry_pointer(va2m, 2);
    check(pde2m != NULL, "locate 2 MiB translation PDE");

    if (pde2m) {
        uint64_t saved = *pde2m;
        *pde2m = pa2m | PTE_PRESENT | PTE_HUGE;

        translated = UINT64_MAX;
        check(
            hellcore_vmm_translate(va2m, &translated) &&
            translated == pa2m,
            "2 MiB translation returns physical base"
        );

        translated = UINT64_MAX;
        check(
            hellcore_vmm_translate(va2m | offset2m, &translated) &&
            translated == (pa2m | offset2m),
            "2 MiB translation preserves page offset"
        );

        *pde2m = saved;
    } else {
        check(0, "2 MiB translation returns physical base");
        check(0, "2 MiB translation preserves page offset");
    }

    /* CORE11.4: physical address zero is valid. */
    const uint64_t zero_va = 0x0000050000001000ULL;

    check(
        hellcore_vmm_map_page(
            zero_va, 0,
            HELLCORE_VMM_MAP_READ | HELLCORE_VMM_MAP_EXECUTE
        ),
        "map a page at physical address zero"
    );

    translated = UINT64_MAX;
    check(
        hellcore_vmm_translate(zero_va, &translated) &&
        translated == 0,
        "physical address zero translates successfully"
    );

    translated = UINT64_MAX;
    check(
        hellcore_vmm_translate(zero_va | offset4, &translated) &&
        translated == offset4,
        "physical-zero mapping preserves page offset"
    );

    /* CORE11.4: failure handling. */
    const uint64_t unmapped_va = 0x0000090000000000ULL;

    translated = UINT64_MAX;
    check(
        !hellcore_vmm_translate(unmapped_va, &translated) &&
        translated == 0,
        "unmapped address fails and clears output"
    );

    const uint64_t noncanonical_va = 0x0000800000000000ULL;

    translated = UINT64_MAX;
    check(
        !hellcore_vmm_translate(noncanonical_va, &translated) &&
        translated == 0,
        "noncanonical address fails and clears output"
    );

    check(
        !hellcore_vmm_translate(va4, NULL),
        "null output pointer is rejected"
    );

    printf("\nCORE11.4 VMM regression tests: %d passed, %d failed\n",
           tests - failures, failures);

    for (size_t i = 0; i < allocated_count; ++i)
        free(allocated_pages[i]);

    return failures ? 1 : 0;
}
