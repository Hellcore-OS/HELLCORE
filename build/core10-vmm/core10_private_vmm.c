#include <stdint.h>
#include "pmm.h"
#include "core10_private_vmm.h"

#define PAGE_SIZE 4096ULL

#define PTE_PRESENT (1ULL << 0)
#define PTE_WRITE   (1ULL << 1)
#define PTE_USER    (1ULL << 2)
#define PTE_HUGE    (1ULL << 7)
#define PTE_NX      (1ULL << 63)

#define CR4_LA57 (1ULL << 12)
#define EFER_MSR 0xC0000080U
#define EFER_NXE (1ULL << 11)

static uint64_t active_root;
static uint64_t physical_mask;
static uint32_t physical_width;
static uint8_t private_slots[512];
static int initialized;
static int32_t allocation_budget = -1;

static uint64_t read_cr3(void)
{
    uint64_t value;
    __asm__ volatile ("mov %%cr3, %0" : "=r"(value));
    return value;
}

static uint64_t read_cr4(void)
{
    uint64_t value;
    __asm__ volatile ("mov %%cr4, %0" : "=r"(value));
    return value;
}

static uint64_t read_msr(uint32_t msr)
{
    uint32_t low, high;

    __asm__ volatile (
        "rdmsr"
        : "=a"(low), "=d"(high)
        : "c"(msr)
    );

    return ((uint64_t)high << 32) | low;
}

static int canonical(uint64_t address)
{
    uint64_t upper = address >> 48;

    return (address & (1ULL << 47))
        ? upper == 0xFFFF
        : upper == 0;
}

static void invalidate_page(uint64_t address)
{
    __asm__ volatile (
        "invlpg (%0)"
        :
        : "r"(address)
        : "memory"
    );
}

/*
 * Translate using the active page tables.
 * Output flags:
 * bit 0 = writable
 * bit 1 = user
 * bit 2 = NX
 */
static int walk(
    uint64_t address,
    uint64_t *physical,
    uint64_t *flags
)
{
    if (!canonical(address))
        return 0;

    volatile uint64_t *pml4 =
        (volatile uint64_t *)(uintptr_t)active_root;

    uint64_t i4 = (address >> 39) & 0x1FF;
    uint64_t i3 = (address >> 30) & 0x1FF;
    uint64_t i2 = (address >> 21) & 0x1FF;
    uint64_t i1 = (address >> 12) & 0x1FF;

    uint64_t e4 = pml4[i4];

    if (!(e4 & PTE_PRESENT))
        return 0;

    volatile uint64_t *pdpt =
        (volatile uint64_t *)(uintptr_t)(e4 & physical_mask);

    uint64_t e3 = pdpt[i3];

    if (!(e3 & PTE_PRESENT))
        return 0;

    uint64_t writable =
        ((e4 & PTE_WRITE) && (e3 & PTE_WRITE)) ? 1ULL : 0;

    uint64_t user =
        ((e4 & PTE_USER) && (e3 & PTE_USER)) ? 1ULL : 0;

    uint64_t nx = (e4 | e3) & PTE_NX;

    if (e3 & PTE_HUGE) {
        *physical =
            (e3 & (physical_mask & ~((1ULL << 30) - 1))) |
            (address & ((1ULL << 30) - 1));

        *flags = (writable ? CORE10_VMM_FLAG_WRITE : 0) |
                 (user ? CORE10_VMM_FLAG_USER : 0) |
                 (nx ? CORE10_VMM_FLAG_NX : 0);
        return 1;
    }

    volatile uint64_t *pd =
        (volatile uint64_t *)(uintptr_t)(e3 & physical_mask);

    uint64_t e2 = pd[i2];

    if (!(e2 & PTE_PRESENT))
        return 0;

    writable = writable && ((e2 & PTE_WRITE) != 0);
    user = user && ((e2 & PTE_USER) != 0);
    nx |= e2 & PTE_NX;

    if (e2 & PTE_HUGE) {
        *physical =
            (e2 & (physical_mask & ~((1ULL << 21) - 1))) |
            (address & ((1ULL << 21) - 1));

        *flags = (writable ? CORE10_VMM_FLAG_WRITE : 0) |
                 (user ? CORE10_VMM_FLAG_USER : 0) |
                 (nx ? CORE10_VMM_FLAG_NX : 0);
        return 1;
    }

    volatile uint64_t *pt =
        (volatile uint64_t *)(uintptr_t)(e2 & physical_mask);

    uint64_t e1 = pt[i1];

    if (!(e1 & PTE_PRESENT))
        return 0;

    writable = writable && ((e1 & PTE_WRITE) != 0);
    user = user && ((e1 & PTE_USER) != 0);
    nx |= e1 & PTE_NX;

    *physical = (e1 & physical_mask) | (address & 0xFFF);

    *flags = (writable ? CORE10_VMM_FLAG_WRITE : 0) |
             (user ? CORE10_VMM_FLAG_USER : 0) |
             (nx ? CORE10_VMM_FLAG_NX : 0);

    return 1;
}

static int identity_writable(uint64_t page)
{
    uint64_t first, last, flags;

    if ((page & 0xFFF) ||
        page > UINT64_MAX - (PAGE_SIZE - 1))
        return 0;

    if (!walk(page, &first, &flags) ||
        first != page ||
        !(flags & CORE10_VMM_FLAG_WRITE))
        return 0;

    if (!walk(page + PAGE_SIZE - 1, &last, &flags) ||
        last != page + PAGE_SIZE - 1 ||
        !(flags & CORE10_VMM_FLAG_WRITE))
        return 0;

    return 1;
}

static uint64_t allocate_table(void)
{
    if (allocation_budget == 0)
        return 0;

    if (allocation_budget > 0)
        --allocation_budget;

    uint64_t page = hellcore_pmm_alloc_page();

    if (!page ||
        (page & 0xFFF) ||
        (page & ~physical_mask)) {
        if (page && !(page & 0xFFF))
            hellcore_pmm_free_page(page);
        return 0;
    }

    if (!identity_writable(page)) {
        hellcore_pmm_free_page(page);
        return 0;
    }

    volatile uint64_t *table =
        (volatile uint64_t *)(uintptr_t)page;

    for (uint32_t i = 0; i < 512; ++i)
        table[i] = 0;

    return page;
}

static void release_table(uint64_t page)
{
    if (page)
        hellcore_pmm_free_page(page);
}

int hellcore_core10_vmm_init(
    uint64_t root,
    uint32_t physical_bits
)
{
    if (initialized ||
        physical_bits < 36 ||
        physical_bits > 52 ||
        !root ||
        (root & 0xFFF))
        return 0;

    if (read_cr4() & CR4_LA57)
        return 0;

    if (!(read_msr(EFER_MSR) & EFER_NXE))
        return 0;

    physical_width = physical_bits;
    physical_mask =
        ((1ULL << physical_width) - 1) & ~0xFFFULL;

    if (root & ~physical_mask)
        return 0;

    if ((read_cr3() & physical_mask) != root)
        return 0;

    active_root = root;

    for (uint32_t i = 0; i < 512; ++i)
        private_slots[i] = 0;

    initialized = 1;
    return 1;
}

int hellcore_core10_vmm_find_private_address(uint64_t *address)
{
    if (!initialized || !address)
        return 0;

    volatile uint64_t *pml4 =
        (volatile uint64_t *)(uintptr_t)active_root;

    /*
     * Search the upper canonical half for an unused PML4 slot.
     * The selected slot is not published or modified here.
     */
    for (uint32_t i = 256; i < 512; ++i) {
        if (!(pml4[i] & PTE_PRESENT) && !private_slots[i]) {
            *address =
                0xFFFF000000000000ULL |
                ((uint64_t)i << 39) |
                0x12345000ULL;
            return 1;
        }
    }

    return 0;
}

int hellcore_core10_vmm_map_page(
    uint64_t virtual_address,
    uint64_t physical_address,
    uint64_t flags
)
{
    if (!initialized ||
        !canonical(virtual_address) ||
        (virtual_address & 0xFFF) ||
        !physical_address ||
        (physical_address & 0xFFF) ||
        (physical_address & ~physical_mask))
        return 0;

    const uint64_t allowed =
        CORE10_VMM_READ |
        CORE10_VMM_WRITE |
        CORE10_VMM_EXECUTE |
        CORE10_VMM_USER;

    if (!(flags & CORE10_VMM_READ) ||
        (flags & ~allowed) ||
        ((flags & CORE10_VMM_WRITE) &&
         (flags & CORE10_VMM_EXECUTE)))
        return 0;

    uint64_t i4 = (virtual_address >> 39) & 0x1FF;
    uint64_t i3 = (virtual_address >> 30) & 0x1FF;
    uint64_t i2 = (virtual_address >> 21) & 0x1FF;
    uint64_t i1 = (virtual_address >> 12) & 0x1FF;

    volatile uint64_t *pml4 =
        (volatile uint64_t *)(uintptr_t)active_root;

    uint64_t user = (flags & CORE10_VMM_USER) ? PTE_USER : 0;
    uint64_t leaf_flags = PTE_PRESENT | user;

    if (flags & CORE10_VMM_WRITE)
        leaf_flags |= PTE_WRITE;

    if (!(flags & CORE10_VMM_EXECUTE))
        leaf_flags |= PTE_NX;

    uint64_t e4 = pml4[i4];
    uint64_t pdpt_page = 0;
    uint64_t pd_page = 0;
    uint64_t pt_page = 0;

    /*
     * An existing branch is only modifiable if this module
     * created it. Shared UEFI branches are never modified.
     */
    if (e4 & PTE_PRESENT) {
        if (!private_slots[i4])
            return 0;
    } else {
        pdpt_page = allocate_table();
        if (!pdpt_page)
            return 0;

        pd_page = allocate_table();
        if (!pd_page) {
            release_table(pdpt_page);
            return 0;
        }

        pt_page = allocate_table();
        if (!pt_page) {
            release_table(pd_page);
            release_table(pdpt_page);
            return 0;
        }

        volatile uint64_t *pdpt =
            (volatile uint64_t *)(uintptr_t)pdpt_page;
        volatile uint64_t *pd =
            (volatile uint64_t *)(uintptr_t)pd_page;
        volatile uint64_t *pt =
            (volatile uint64_t *)(uintptr_t)pt_page;

        pt[i1] = physical_address | leaf_flags;
        pd[i2] = pt_page | PTE_PRESENT | PTE_WRITE | user;
        pdpt[i3] = pd_page | PTE_PRESENT | PTE_WRITE | user;

        /* Publish the complete private branch last. */
        pml4[i4] =
            pdpt_page | PTE_PRESENT | PTE_WRITE | user;

        private_slots[i4] = 1;
        invalidate_page(virtual_address);
        return 1;
    }

    volatile uint64_t *pdpt =
        (volatile uint64_t *)(uintptr_t)(e4 & physical_mask);

    uint64_t e3 = pdpt[i3];

    if (e3 & PTE_PRESENT) {
        if (e3 & PTE_HUGE)
            return 0;

        pd_page = e3 & physical_mask;
    } else {
        pd_page = allocate_table();
        if (!pd_page)
            return 0;

        pt_page = allocate_table();
        if (!pt_page) {
            release_table(pd_page);
            return 0;
        }

        volatile uint64_t *pd =
            (volatile uint64_t *)(uintptr_t)pd_page;
        volatile uint64_t *pt =
            (volatile uint64_t *)(uintptr_t)pt_page;

        pt[i1] = physical_address | leaf_flags;
        pd[i2] = pt_page | PTE_PRESENT | PTE_WRITE | user;

        pdpt[i3] =
            pd_page | PTE_PRESENT | PTE_WRITE | user;

        pdpt[i3] |= PTE_USER * ((flags & CORE10_VMM_USER) != 0);
        pml4[i4] |= PTE_USER * ((flags & CORE10_VMM_USER) != 0);

        invalidate_page(virtual_address);
        return 1;
    }

    volatile uint64_t *pd =
        (volatile uint64_t *)(uintptr_t)pd_page;

    uint64_t e2 = pd[i2];

    if (e2 & PTE_PRESENT) {
        if (e2 & PTE_HUGE)
            return 0;

        pt_page = e2 & physical_mask;
    } else {
        pt_page = allocate_table();
        if (!pt_page)
            return 0;

        volatile uint64_t *pt =
            (volatile uint64_t *)(uintptr_t)pt_page;

        pt[i1] = physical_address | leaf_flags;

        pd[i2] = pt_page | PTE_PRESENT | PTE_WRITE | user;
        pdpt[i3] |= PTE_USER * ((flags & CORE10_VMM_USER) != 0);
        pml4[i4] |= PTE_USER * ((flags & CORE10_VMM_USER) != 0);

        invalidate_page(virtual_address);
        return 1;
    }

    volatile uint64_t *pt =
        (volatile uint64_t *)(uintptr_t)pt_page;

    if (pt[i1] & PTE_PRESENT)
        return 0;

    pt[i1] = physical_address | leaf_flags;

    pd[i2] |= PTE_USER * ((flags & CORE10_VMM_USER) != 0);
    pdpt[i3] |= PTE_USER * ((flags & CORE10_VMM_USER) != 0);
    pml4[i4] |= PTE_USER * ((flags & CORE10_VMM_USER) != 0);

    invalidate_page(virtual_address);
    return 1;
}

int hellcore_core10_vmm_unmap_page(uint64_t virtual_address)
{
    if (!initialized ||
        !canonical(virtual_address) ||
        (virtual_address & 0xFFF))
        return 0;

    uint64_t i4 = (virtual_address >> 39) & 0x1FF;
    uint64_t i3 = (virtual_address >> 30) & 0x1FF;
    uint64_t i2 = (virtual_address >> 21) & 0x1FF;
    uint64_t i1 = (virtual_address >> 12) & 0x1FF;

    if (!private_slots[i4])
        return 0;

    volatile uint64_t *pml4 =
        (volatile uint64_t *)(uintptr_t)active_root;

    uint64_t e4 = pml4[i4];
    if (!(e4 & PTE_PRESENT))
        return 0;

    volatile uint64_t *pdpt =
        (volatile uint64_t *)(uintptr_t)(e4 & physical_mask);

    uint64_t e3 = pdpt[i3];
    if (!(e3 & PTE_PRESENT) || (e3 & PTE_HUGE))
        return 0;

    volatile uint64_t *pd =
        (volatile uint64_t *)(uintptr_t)(e3 & physical_mask);

    uint64_t e2 = pd[i2];
    if (!(e2 & PTE_PRESENT) || (e2 & PTE_HUGE))
        return 0;

    volatile uint64_t *pt =
        (volatile uint64_t *)(uintptr_t)(e2 & physical_mask);

    if (!(pt[i1] & PTE_PRESENT))
        return 0;

    pt[i1] = 0;
    invalidate_page(virtual_address);
    return 1;
}

int hellcore_core10_vmm_translate(
    uint64_t virtual_address,
    uint64_t *physical_address
)
{
    uint64_t flags;

    if (!initialized || !physical_address)
        return 0;

    return walk(virtual_address, physical_address, &flags);
}

int hellcore_core10_vmm_get_flags(
    uint64_t virtual_address,
    uint64_t *flags
)
{
    uint64_t physical;

    if (!initialized || !flags)
        return 0;

    return walk(virtual_address, &physical, flags);
}

uint64_t hellcore_core10_vmm_root_entry(uint32_t index)
{
    if (!initialized || index >= 512)
        return 0;

    volatile uint64_t *pml4 =
        (volatile uint64_t *)(uintptr_t)active_root;

    return pml4[index];
}

void hellcore_core10_vmm_test_fail_after(int32_t allocations)
{
    allocation_budget = allocations;
}
