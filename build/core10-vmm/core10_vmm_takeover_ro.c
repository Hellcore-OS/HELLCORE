#include <stdint.h>
#include "pmm.h"
#include "core10_vmm_takeover.h"

#define PAGE_SIZE       4096ULL
#define PTE_PRESENT     (1ULL << 0)
#define PTE_WRITE       (1ULL << 1)
#define PTE_HUGE        (1ULL << 7)
#define PTE_ADDRESS     0x000FFFFFFFFFF000ULL
#define CR4_LA57        (1ULL << 12)
#define CR4_PCIDE       (1ULL << 17)
#define CR3_PWT         (1ULL << 3)
#define CR3_PCD         (1ULL << 4)

static void read_cpuid(
    uint32_t leaf,
    uint32_t *a,
    uint32_t *b,
    uint32_t *c,
    uint32_t *d
) {
    __asm__ volatile (
        "cpuid"
        : "=a"(*a), "=b"(*b), "=c"(*c), "=d"(*d)
        : "a"(leaf), "c"(0)
    );
}

static uint64_t read_cr3(void) {
    uint64_t value;
    __asm__ volatile ("mov %%cr3, %0" : "=r"(value));
    return value;
}

static uint64_t read_cr4(void) {
    uint64_t value;
    __asm__ volatile ("mov %%cr4, %0" : "=r"(value));
    return value;
}

static void write_cr3(uint64_t value) {
    __asm__ volatile (
        "mov %0, %%cr3"
        :
        : "r"(value)
        : "memory"
    );
}

static int canonical(uint64_t address) {
    uint64_t upper = address >> 48;

    return (address & (1ULL << 47))
        ? upper == 0xFFFF
        : upper == 0;
}

/*
 * Read-only page-table walk.
 * Page-table pages are accessed through the inherited identity map.
 * flags bit 0 = writable.
 */
static int translate(
    uint64_t root,
    uint64_t address,
    uint64_t mask,
    uint64_t *physical,
    uint32_t *flags
) {
    if (!canonical(address))
        return 0;

    uint64_t i4 = (address >> 39) & 0x1FF;
    uint64_t i3 = (address >> 30) & 0x1FF;
    uint64_t i2 = (address >> 21) & 0x1FF;
    uint64_t i1 = (address >> 12) & 0x1FF;

    volatile uint64_t *pml4 =
        (volatile uint64_t *)(uintptr_t)root;

    uint64_t e4 = pml4[i4];
    if (!(e4 & PTE_PRESENT))
        return 0;

    volatile uint64_t *pdpt =
        (volatile uint64_t *)(uintptr_t)(e4 & mask);

    uint64_t e3 = pdpt[i3];
    if (!(e3 & PTE_PRESENT))
        return 0;

    uint32_t writable =
        ((e4 & PTE_WRITE) && (e3 & PTE_WRITE)) ? 1U : 0U;

    if (e3 & PTE_HUGE) {
        *physical =
            (e3 & (mask & ~((1ULL << 30) - 1))) |
            (address & ((1ULL << 30) - 1));
        *flags = writable;
        return 1;
    }

    volatile uint64_t *pd =
        (volatile uint64_t *)(uintptr_t)(e3 & mask);

    uint64_t e2 = pd[i2];
    if (!(e2 & PTE_PRESENT))
        return 0;

    writable = writable && ((e2 & PTE_WRITE) != 0);

    if (e2 & PTE_HUGE) {
        *physical =
            (e2 & (mask & ~((1ULL << 21) - 1))) |
            (address & ((1ULL << 21) - 1));
        *flags = writable;
        return 1;
    }

    volatile uint64_t *pt =
        (volatile uint64_t *)(uintptr_t)(e2 & mask);

    uint64_t e1 = pt[i1];
    if (!(e1 & PTE_PRESENT))
        return 0;

    writable = writable && ((e1 & PTE_WRITE) != 0);

    *physical = (e1 & mask) | (address & 0xFFF);
    *flags = writable;

    return 1;
}

static int identity_page_is_mapped(
    uint64_t root,
    uint64_t page,
    uint64_t mask
) {
    uint64_t first_physical = 0;
    uint64_t last_physical = 0;
    uint32_t first_flags = 0;
    uint32_t last_flags = 0;

    if ((page & 0xFFF) != 0)
        return 0;

    if (page > UINT64_MAX - (PAGE_SIZE - 1))
        return 0;

    if (!translate(root, page, mask,
                   &first_physical, &first_flags))
        return 0;

    if (!translate(root, page + PAGE_SIZE - 1, mask,
                   &last_physical, &last_flags))
        return 0;

    return first_physical == page &&
           last_physical == page + PAGE_SIZE - 1;
}

static int identity_page_is_writable(
    uint64_t root,
    uint64_t page,
    uint64_t mask
) {
    uint64_t first_physical = 0;
    uint64_t last_physical = 0;
    uint32_t first_flags = 0;
    uint32_t last_flags = 0;

    if ((page & 0xFFF) != 0)
        return 0;

    if (page > UINT64_MAX - (PAGE_SIZE - 1))
        return 0;

    if (!translate(root, page, mask,
                   &first_physical, &first_flags))
        return 0;

    if (!translate(root, page + PAGE_SIZE - 1, mask,
                   &last_physical, &last_flags))
        return 0;

    return first_physical == page &&
           last_physical == page + PAGE_SIZE - 1 &&
           first_flags && last_flags;
}

int hellcore_core10_takeover_root(
    CORE10_VMM_TAKEOVER_REPORT *report
) {
    if (!report)
        return 0;

    report->old_cr3 = 0;
    report->new_cr3 = 0;
    report->new_root = 0;
    report->physical_bits = 0;
    report->entries_checked = 0;
    report->root_validated = 0;
    report->switched = 0;
    report->readback_validated = 0;

    uint32_t a, b, c, d;

    read_cpuid(0x80000000U, &a, &b, &c, &d);
    if (a < 0x80000008U)
        return 0;

    read_cpuid(0x80000008U, &a, &b, &c, &d);

    uint32_t bits = a & 0xFF;
    if (bits < 36 || bits > 52)
        return 0;

    report->physical_bits = bits;

    uint64_t mask = ((1ULL << bits) - 1) & ~0xFFFULL;

    uint64_t old_cr3 = read_cr3();
    uint64_t cr4 = read_cr4();

    report->old_cr3 = old_cr3;

    /* Five-level paging and PCID are not supported by this test. */
    if (cr4 & (CR4_LA57 | CR4_PCIDE))
        return 0;

    uint64_t old_root = old_cr3 & mask;

    if (!old_root || (old_root & 0xFFF))
        return 0;

    if (!identity_page_is_mapped(old_root, old_root, mask))
        return 0;

    /* Allocate the replacement PML4 through the existing PMM. */
    uint64_t new_root = hellcore_pmm_alloc_page();

    if (!new_root || (new_root & 0xFFF) ||
        (new_root & ~mask)) {
        if (new_root && !(new_root & 0xFFF))
            hellcore_pmm_free_page(new_root);
        return 0;
    }

    report->new_root = new_root;

    /*
     * Do not dereference the new page until the inherited
     * address space proves that the entire page is mapped
     * identity and writable.
     */
    if (!identity_page_is_writable(old_root, new_root, mask)) {
        hellcore_pmm_free_page(new_root);
        return 0;
    }

    volatile uint64_t *old_table =
        (volatile uint64_t *)(uintptr_t)old_root;

    volatile uint64_t *new_table =
        (volatile uint64_t *)(uintptr_t)new_root;

    /* Copy all 512 entries, including non-present entries. */
    for (uint32_t i = 0; i < 512; ++i)
        new_table[i] = old_table[i];

    /* Validate every copied entry before changing CR3. */
    for (uint32_t i = 0; i < 512; ++i) {
        uint64_t entry = new_table[i];

        if (entry != old_table[i]) {
            hellcore_pmm_free_page(new_root);
            return 0;
        }

        if (entry & PTE_PRESENT) {
            uint64_t address = entry & PTE_ADDRESS;

            /* PML4 entries cannot be huge-page entries. */
            if ((entry & PTE_HUGE) ||
                (address & ~mask) ||
                !address) {
                hellcore_pmm_free_page(new_root);
                return 0;
            }
        }

        report->entries_checked++;
    }

    report->root_validated = 1;

    /*
     * Preserve the inherited PWT/PCD settings.
     * PCID was explicitly rejected above.
     */
    uint64_t new_cr3 =
        new_root | (old_cr3 & (CR3_PWT | CR3_PCD));

    report->new_cr3 = new_cr3;

    /* The only CR3 write in this implementation. */
    write_cr3(new_cr3);
    report->switched = 1;

    uint64_t actual_cr3 = read_cr3();

    if ((actual_cr3 & mask) != new_root) {
        /* Restore the original root if readback fails. */
        write_cr3(old_cr3);
        report->switched = 0;
        hellcore_pmm_free_page(new_root);
        return 0;
    }

    report->readback_validated = 1;

    /* Keep the new root allocated while it is active. */
    return 1;
}
