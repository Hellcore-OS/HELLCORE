#include "vmm.h"
#include "../include/hellcore_boot_info.h"

#define PAGE_SIZE 4096ULL
#define PAGE_MASK 0x000FFFFFFFFFF000ULL
#define ADDRESS_LIMIT 0x100000000ULL

#define PTE_PRESENT  (1ULL << 0)
#define PTE_WRITE    (1ULL << 1)
#define PTE_HUGE     (1ULL << 7)
#define PTE_NX       (1ULL << 63)

#define EFER_MSR 0xC0000080U
#define EFER_NXE (1ULL << 11)

static uint64_t *root_table;

static void zero_page(uint64_t address) {
    volatile uint64_t *p = (volatile uint64_t *)(uintptr_t)address;

    for (uint64_t i = 0; i < PAGE_SIZE / sizeof(uint64_t); ++i)
        p[i] = 0;
}

static uint64_t allocate_table(void) {
    uint64_t page = hellcore_pmm_alloc_page();

    if (!page || page >= ADDRESS_LIMIT) {
        if (page)
            hellcore_pmm_free_page(page);
        return 0;
    }

    zero_page(page);
    return page;
}

static void cpuid(
    uint32_t leaf,
    uint32_t subleaf,
    uint32_t *a,
    uint32_t *b,
    uint32_t *c,
    uint32_t *d
) {
    __asm__ volatile (
        "cpuid"
        : "=a"(*a), "=b"(*b), "=c"(*c), "=d"(*d)
        : "a"(leaf), "c"(subleaf)
    );
}

static uint64_t read_msr(uint32_t msr) {
    uint32_t low;
    uint32_t high;

    __asm__ volatile (
        "rdmsr"
        : "=a"(low), "=d"(high)
        : "c"(msr)
    );

    return ((uint64_t)high << 32) | low;
}

static void write_msr(uint32_t msr, uint64_t value) {
    uint32_t low = (uint32_t)value;
    uint32_t high = (uint32_t)(value >> 32);

    __asm__ volatile (
        "wrmsr"
        :
        : "c"(msr), "a"(low), "d"(high)
    );
}

static int cpu_supports_nx(void) {
    uint32_t a, b, c, d;

    cpuid(0x80000000U, 0, &a, &b, &c, &d);

    if (a < 0x80000001U)
        return 0;

    cpuid(0x80000001U, 0, &a, &b, &c, &d);
    return (d & (1U << 20)) != 0;
}

static int cpu_uses_five_level_paging(void) {
    uint64_t cr4;

    __asm__ volatile ("mov %%cr4, %0" : "=r"(cr4));

    return (cr4 & (1ULL << 12)) != 0;
}


#define PTE_USER (1ULL << 2)

static int canonical_address(uint64_t address) {
    uint64_t upper = address >> 48;
    return ((address & (1ULL << 47)) == 0)
        ? upper == 0
        : upper == 0xFFFF;
}

static uint64_t *get_or_create_table(uint64_t *parent, uint64_t index) {
    uint64_t entry = parent[index];

    if (entry & PTE_PRESENT) {
        if (entry & PTE_HUGE)
            return 0;

        return (uint64_t *)(uintptr_t)(entry & PAGE_MASK);
    }

    uint64_t page = allocate_table();

    if (!page)
        return 0;

    parent[index] = page | PTE_PRESENT | PTE_WRITE | PTE_USER;
    return (uint64_t *)(uintptr_t)page;
}

int hellcore_vmm_map_page(
    uint64_t virtual_address,
    uint64_t physical_address,
    uint64_t flags
) {
    if (!root_table ||
        (virtual_address & (PAGE_SIZE - 1)) ||
        (physical_address & (PAGE_SIZE - 1)) ||
        physical_address >= ADDRESS_LIMIT ||
        !canonical_address(virtual_address))
        return 0;

    uint64_t pml4_index = (virtual_address >> 39) & 0x1FF;
    uint64_t pdpt_index = (virtual_address >> 30) & 0x1FF;
    uint64_t pd_index = (virtual_address >> 21) & 0x1FF;
    uint64_t pt_index = (virtual_address >> 12) & 0x1FF;

    uint64_t *pdpt = get_or_create_table(root_table, pml4_index);
    if (!pdpt)
        return 0;

    uint64_t *pd = get_or_create_table(pdpt, pdpt_index);
    if (!pd)
        return 0;

    uint64_t *pt = get_or_create_table(pd, pd_index);
    if (!pt)
        return 0;

    uint64_t entry = physical_address | PTE_PRESENT;

    if (flags & HELLCORE_VMM_MAP_WRITE)
        entry |= PTE_WRITE;

    if (flags & HELLCORE_VMM_MAP_USER)
        entry |= PTE_USER;

    if (!(flags & HELLCORE_VMM_MAP_EXECUTE))
        entry |= PTE_NX;

    pt[pt_index] = entry;

    __asm__ volatile ("invlpg (%0)" : : "r"(virtual_address) : "memory");

    return 1;
}

int hellcore_vmm_unmap_page(uint64_t virtual_address) {
    if (!root_table ||
        (virtual_address & (PAGE_SIZE - 1)) ||
        !canonical_address(virtual_address))
        return 0;

    uint64_t pml4_index = (virtual_address >> 39) & 0x1FF;
    uint64_t pdpt_index = (virtual_address >> 30) & 0x1FF;
    uint64_t pd_index = (virtual_address >> 21) & 0x1FF;
    uint64_t pt_index = (virtual_address >> 12) & 0x1FF;

    uint64_t entry = root_table[pml4_index];
    if (!(entry & PTE_PRESENT) || (entry & PTE_HUGE))
        return 0;

    uint64_t *pdpt = (uint64_t *)(uintptr_t)(entry & PAGE_MASK);

    entry = pdpt[pdpt_index];
    if (!(entry & PTE_PRESENT) || (entry & PTE_HUGE))
        return 0;

    uint64_t *pd = (uint64_t *)(uintptr_t)(entry & PAGE_MASK);

    entry = pd[pd_index];
    if (!(entry & PTE_PRESENT) || (entry & PTE_HUGE))
        return 0;

    uint64_t *pt = (uint64_t *)(uintptr_t)(entry & PAGE_MASK);

    if (!(pt[pt_index] & PTE_PRESENT))
        return 0;

    pt[pt_index] = 0;

    __asm__ volatile ("invlpg (%0)" : : "r"(virtual_address) : "memory");

    return 1;
}

uint64_t hellcore_vmm_translate(uint64_t virtual_address) {
    if (!root_table || !canonical_address(virtual_address))
        return 0;

    uint64_t pml4_index = (virtual_address >> 39) & 0x1FF;
    uint64_t pdpt_index = (virtual_address >> 30) & 0x1FF;
    uint64_t pd_index = (virtual_address >> 21) & 0x1FF;
    uint64_t pt_index = (virtual_address >> 12) & 0x1FF;

    uint64_t entry = root_table[pml4_index];
    if (!(entry & PTE_PRESENT))
        return 0;

    uint64_t *pdpt = (uint64_t *)(uintptr_t)(entry & PAGE_MASK);

    entry = pdpt[pdpt_index];
    if (!(entry & PTE_PRESENT))
        return 0;

    uint64_t *pd = (uint64_t *)(uintptr_t)(entry & PAGE_MASK);

    entry = pd[pd_index];
    if (!(entry & PTE_PRESENT))
        return 0;

    if (entry & PTE_HUGE)
        return (entry & 0x000FFFFFFFE00000ULL) |
               (virtual_address & 0x1FFFFFULL);

    uint64_t *pt = (uint64_t *)(uintptr_t)(entry & PAGE_MASK);

    entry = pt[pt_index];
    if (!(entry & PTE_PRESENT))
        return 0;

    return (entry & PAGE_MASK) |
           (virtual_address & (PAGE_SIZE - 1));
}

uint64_t hellcore_vmm_get_root(void) {
    return (uint64_t)(uintptr_t)root_table;
}

int hellcore_vmm_init(const HELLCORE_BOOT_INFO *boot) {
    if (!boot ||
        boot->magic != HELLCORE_BOOT_MAGIC ||
        boot->version != HELLCORE_BOOT_VERSION ||
        boot->structure_size < sizeof(HELLCORE_BOOT_INFO))
        return 0;

    if (!boot->memory_map_address ||
        !boot->memory_map_size ||
        !boot->descriptor_size ||
        boot->descriptor_size < 40 ||
        boot->memory_map_size % boot->descriptor_size)
        return 0;

    if (cpu_uses_five_level_paging())
        return 0;

    if (!cpu_supports_nx())
        return 0;

    uint64_t efer = read_msr(EFER_MSR);
    write_msr(EFER_MSR, efer | EFER_NXE);

    uint64_t cr0;
    __asm__ volatile ("mov %%cr0, %0" : "=r"(cr0));
    cr0 |= (1ULL << 16);
    __asm__ volatile ("mov %0, %%cr0" : : "r"(cr0) : "memory");

    uint64_t new_root = allocate_table();

    if (!new_root)
        return 0;

    root_table = (uint64_t *)(uintptr_t)new_root;

    return 1;
}
