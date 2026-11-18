#include "vmm.h"
#include "../include/hellcore_boot_info.h"

#define PAGE_SIZE 4096ULL
#define PAGE_MASK 0x000FFFFFFFFFF000ULL
#ifdef HELLCORE_VMM_HOST_TEST
#define ADDRESS_LIMIT UINT64_MAX
#else
#define ADDRESS_LIMIT 0x100000000ULL
#endif

#define PTE_PRESENT  (1ULL << 0)
#define PTE_WRITE    (1ULL << 1)
#define PTE_HUGE     (1ULL << 7)
#define PTE_NX       (1ULL << 63)

#define EFER_MSR 0xC0000080U
#define EFER_NXE (1ULL << 11)

static uint64_t *root_table;

#ifdef HELLCORE_VMM_HOST_TEST
void hellcore_vmm_host_test_set_root(uint64_t *root) {
    root_table = root;
}
#endif
static int map_boot_memory(const HELLCORE_BOOT_INFO *boot);

typedef struct {
    uint32_t Type;
    uint32_t Padding;
    uint64_t PhysicalStart;
    uint64_t VirtualStart;
    uint64_t NumberOfPages;
    uint64_t Attribute;
} HELLCORE_EFI_MEMORY_DESCRIPTOR;

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
#ifdef HELLCORE_VMM_HOST_TEST
    (void)subleaf;
    *a = (leaf == 0x80000000U) ? 0x80000001U : 0;
    *b = 0;
    *c = 0;
    *d = (leaf == 0x80000001U) ? (1U << 20) : 0;
#else
    __asm__ volatile (
        "cpuid"
        : "=a"(*a), "=b"(*b), "=c"(*c), "=d"(*d)
        : "a"(leaf), "c"(subleaf)
    );
#endif
}

static uint64_t read_msr(uint32_t msr) {
    uint32_t low;
    uint32_t high;

#ifdef HELLCORE_VMM_HOST_TEST
    (void)msr;
    low = 0;
    high = 0;
#else
    __asm__ volatile (
        "rdmsr"
        : "=a"(low), "=d"(high)
        : "c"(msr)
    );
#endif

    return ((uint64_t)high << 32) | low;
}

static void write_msr(uint32_t msr, uint64_t value) {
    uint32_t low = (uint32_t)value;
    uint32_t high = (uint32_t)(value >> 32);

#ifdef HELLCORE_VMM_HOST_TEST
    (void)msr;
    (void)low;
    (void)high;
#else
    __asm__ volatile (
        "wrmsr"
        :
        : "c"(msr), "a"(low), "d"(high)
    );
#endif
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

#ifdef HELLCORE_VMM_HOST_TEST
    cr4 = 0;
#else
    __asm__ volatile ("mov %%cr4, %0" : "=r"(cr4));
#endif

    return (cr4 & (1ULL << 12)) != 0;
}


#define PTE_USER (1ULL << 2)

static int canonical_address(uint64_t address) {
    uint64_t upper = address >> 48;
    return ((address & (1ULL << 47)) == 0)
        ? upper == 0
        : upper == 0xFFFF;
}

static uint64_t *get_or_create_table(
    uint64_t *parent,
    uint64_t index,
    uint64_t flags
) {
    uint64_t entry = parent[index];

    if (entry & PTE_PRESENT) {
        if (entry & PTE_HUGE)
            return 0;

        if (flags & HELLCORE_VMM_MAP_USER) {
            parent[index] |= PTE_USER;
        }

        return (uint64_t *)(uintptr_t)(entry & PAGE_MASK);
    }

    uint64_t page = allocate_table();

    if (!page)
        return 0;

    uint64_t new_entry = page | PTE_PRESENT | PTE_WRITE;

    if (flags & HELLCORE_VMM_MAP_USER)
        new_entry |= PTE_USER;

    parent[index] = new_entry;
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

    uint64_t *pdpt = get_or_create_table(root_table, pml4_index, flags);
    if (!pdpt)
        return 0;

    uint64_t *pd = get_or_create_table(pdpt, pdpt_index, flags);
    if (!pd)
        return 0;

    uint64_t *pt = get_or_create_table(pd, pd_index, flags);
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

#ifndef HELLCORE_VMM_HOST_TEST
    __asm__ volatile ("invlpg (%0)" : : "r"(virtual_address) : "memory");
#else
    (void)virtual_address;
#endif

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

#ifndef HELLCORE_VMM_HOST_TEST
    __asm__ volatile ("invlpg (%0)" : : "r"(virtual_address) : "memory");
#else
    (void)virtual_address;
#endif

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

int hellcore_vmm_get_flags(uint64_t virtual_address, uint64_t *flags) {
    if (!root_table || !flags || !canonical_address(virtual_address))
        return 0;

    uint64_t pml4 = (virtual_address >> 39) & 0x1FF;
    uint64_t pdpt_index = (virtual_address >> 30) & 0x1FF;
    uint64_t pd_index = (virtual_address >> 21) & 0x1FF;
    uint64_t pt_index = (virtual_address >> 12) & 0x1FF;

    uint64_t entry = root_table[pml4];
    if (!(entry & PTE_PRESENT))
        return 0;
    uint64_t effective = entry;
    uint64_t effective_nx = entry & PTE_NX;

    uint64_t *pdpt = (uint64_t *)(uintptr_t)(entry & PAGE_MASK);
    entry = pdpt[pdpt_index];
    if (!(entry & PTE_PRESENT))
        return 0;
    effective &= entry;
    effective_nx |= entry & PTE_NX;

    uint64_t *pd = (uint64_t *)(uintptr_t)(entry & PAGE_MASK);
    entry = pd[pd_index];
    if (!(entry & PTE_PRESENT))
        return 0;
    effective &= entry;
    effective_nx |= entry & PTE_NX;

    if (entry & PTE_HUGE) {
        *flags = HELLCORE_VMM_PAGE_PRESENT |
                 ((effective & PTE_WRITE) ? HELLCORE_VMM_PAGE_WRITE : 0) |
                 ((effective & PTE_USER) ? HELLCORE_VMM_PAGE_USER : 0) |
                 ((effective_nx & PTE_NX) ? HELLCORE_VMM_PAGE_NX : 0) |
                 HELLCORE_VMM_PAGE_HUGE;
        return 1;
    }

    uint64_t *pt = (uint64_t *)(uintptr_t)(entry & PAGE_MASK);
    entry = pt[pt_index];
    if (!(entry & PTE_PRESENT))
        return 0;
    effective &= entry;
    effective_nx |= entry & PTE_NX;

    *flags = HELLCORE_VMM_PAGE_PRESENT |
             ((effective & PTE_WRITE) ? HELLCORE_VMM_PAGE_WRITE : 0) |
             ((effective & PTE_USER) ? HELLCORE_VMM_PAGE_USER : 0) |
             ((effective_nx & PTE_NX) ? HELLCORE_VMM_PAGE_NX : 0);
    return 1;
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
#ifdef HELLCORE_VMM_HOST_TEST
    cr0 = 0;
    (void)cr0;
#else
    __asm__ volatile ("mov %%cr0, %0" : "=r"(cr0));
    cr0 |= (1ULL << 16);
    __asm__ volatile ("mov %0, %%cr0" : : "r"(cr0) : "memory");
#endif

    uint64_t new_root = allocate_table();

    if (!new_root)
        return 0;

    root_table = (uint64_t *)(uintptr_t)new_root;

    return 1;
}

static int map_identity_range(uint64_t start, uint64_t pages) {
    if (!pages || (start & (PAGE_SIZE - 1)))
        return 0;

    if (pages > (UINT64_MAX - start) / PAGE_SIZE)
        return 0;

    uint64_t end = start + pages * PAGE_SIZE;

    if (end > ADDRESS_LIMIT)
        return 0;

    for (uint64_t address = start; address < end; address += PAGE_SIZE) {
        if (!hellcore_vmm_map_page(
                address,
                address,
                HELLCORE_VMM_MAP_READ | HELLCORE_VMM_MAP_WRITE))
            return 0;
    }

    return 1;
}

static int map_boot_memory(const HELLCORE_BOOT_INFO *boot) {
    const uint8_t *map =
        (const uint8_t *)(uintptr_t)boot->memory_map_address;

    uint64_t count = boot->memory_map_size / boot->descriptor_size;

    for (uint64_t i = 0; i < count; ++i) {
        const HELLCORE_EFI_MEMORY_DESCRIPTOR *desc =
            (const HELLCORE_EFI_MEMORY_DESCRIPTOR *)
            (map + i * boot->descriptor_size);

        switch (desc->Type) {
            case 1:  /* LoaderCode */
            case 2:  /* LoaderData */
            case 3:  /* BootServicesCode */
            case 4:  /* BootServicesData */
            case 7:  /* ConventionalMemory */
            case 9:  /* ACPIReclaimMemory */
            case 10: /* ACPIMemoryNVS */
                break;

            default:
                continue;
        }

        uint64_t start = desc->PhysicalStart;
        uint64_t pages = desc->NumberOfPages;

        if (!pages || start >= ADDRESS_LIMIT)
            continue;

        if (start & (PAGE_SIZE - 1))
            return 0;

        if (pages > (UINT64_MAX - start) / PAGE_SIZE)
            return 0;

        uint64_t max_pages = (ADDRESS_LIMIT - start) / PAGE_SIZE;

        if (pages > max_pages)
            pages = max_pages;

        if (pages && !map_identity_range(start, pages))
            return 0;
    }

    if (boot->acpi_rsdp_address) {
        uint64_t acpi_page =
            boot->acpi_rsdp_address & ~(PAGE_SIZE - 1);

        if (acpi_page >= ADDRESS_LIMIT)
            return 0;

        if (!map_identity_range(acpi_page, 1))
            return 0;
    }

    return 1;
}
