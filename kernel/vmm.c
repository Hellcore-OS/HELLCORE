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

typedef struct {
    uint64_t *parent;
    uint64_t index;
    uint64_t original_entry;
    uint64_t allocated_page;
} VMM_MAP_JOURNAL_ENTRY;

#define VMM_MAP_JOURNAL_CAPACITY 3

static uint64_t *get_or_create_table(
    uint64_t *parent,
    uint64_t index,
    uint64_t flags,
    VMM_MAP_JOURNAL_ENTRY *journal,
    uint64_t *journal_count
) {
    uint64_t entry = parent[index];

    if (entry & PTE_PRESENT) {
        if (entry & PTE_HUGE)
            return 0;

        if ((flags & HELLCORE_VMM_MAP_USER) &&
            !(entry & PTE_USER)) {

            if (*journal_count >= VMM_MAP_JOURNAL_CAPACITY)
                return 0;

            journal[*journal_count].parent = parent;
            journal[*journal_count].index = index;
            journal[*journal_count].original_entry = entry;
            journal[*journal_count].allocated_page = 0;
            ++(*journal_count);

            parent[index] |= PTE_USER;
        }

        return (uint64_t *)(uintptr_t)(entry & PAGE_MASK);
    }

    if (*journal_count >= VMM_MAP_JOURNAL_CAPACITY)
        return 0;

    uint64_t page = allocate_table();

    if (!page)
        return 0;

    uint64_t new_entry = page | PTE_PRESENT | PTE_WRITE;

    if (flags & HELLCORE_VMM_MAP_USER)
        new_entry |= PTE_USER;

    journal[*journal_count].parent = parent;
    journal[*journal_count].index = index;
    journal[*journal_count].original_entry = entry;
    journal[*journal_count].allocated_page = page;
    ++(*journal_count);

    parent[index] = new_entry;

    return (uint64_t *)(uintptr_t)page;
}

static void rollback_map_journal(
    VMM_MAP_JOURNAL_ENTRY *journal,
    uint64_t journal_count
) {
    while (journal_count > 0) {
        VMM_MAP_JOURNAL_ENTRY *item =
            &journal[--journal_count];

        item->parent[item->index] = item->original_entry;

        if (item->allocated_page)
            (void)hellcore_pmm_free_page(item->allocated_page);
    }
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

    VMM_MAP_JOURNAL_ENTRY journal[VMM_MAP_JOURNAL_CAPACITY];
    uint64_t journal_count = 0;

    uint64_t pml4_index = (virtual_address >> 39) & 0x1FF;
    uint64_t pdpt_index = (virtual_address >> 30) & 0x1FF;
    uint64_t pd_index = (virtual_address >> 21) & 0x1FF;
    uint64_t pt_index = (virtual_address >> 12) & 0x1FF;

    uint64_t *pdpt = get_or_create_table(
        root_table, pml4_index, flags, journal, &journal_count
    );

    if (!pdpt) {
        rollback_map_journal(journal, journal_count);
        return 0;
    }

    uint64_t *pd = get_or_create_table(
        pdpt, pdpt_index, flags, journal, &journal_count
    );

    if (!pd) {
        rollback_map_journal(journal, journal_count);
        return 0;
    }

    uint64_t *pt = get_or_create_table(
        pd, pd_index, flags, journal, &journal_count
    );

    if (!pt) {
        rollback_map_journal(journal, journal_count);
        return 0;
    }

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

int hellcore_vmm_translate(
    uint64_t virtual_address,
    uint64_t *physical_address
) {
    if (!physical_address)
        return 0;

    *physical_address = 0;

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

    if (entry & PTE_HUGE) {
        *physical_address =
            (entry & 0x000FFFFFC0000000ULL) |
            (virtual_address & 0x3FFFFFFFULL);
        return 1;
    }

    uint64_t *pd = (uint64_t *)(uintptr_t)(entry & PAGE_MASK);

    entry = pd[pd_index];
    if (!(entry & PTE_PRESENT))
        return 0;

    if (entry & PTE_HUGE) {
        *physical_address =
            (entry & 0x000FFFFFFFE00000ULL) |
            (virtual_address & 0x1FFFFFULL);
        return 1;
    }

    uint64_t *pt = (uint64_t *)(uintptr_t)(entry & PAGE_MASK);

    entry = pt[pt_index];
    if (!(entry & PTE_PRESENT))
        return 0;

    *physical_address =
        (entry & PAGE_MASK) |
        (virtual_address & (PAGE_SIZE - 1));

    return 1;
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

    if (entry & PTE_HUGE) {
        *flags = HELLCORE_VMM_PAGE_PRESENT |
                 ((effective & PTE_WRITE) ? HELLCORE_VMM_PAGE_WRITE : 0) |
                 ((effective & PTE_USER) ? HELLCORE_VMM_PAGE_USER : 0) |
                 ((effective_nx & PTE_NX) ? HELLCORE_VMM_PAGE_NX : 0) |
                 HELLCORE_VMM_PAGE_HUGE;
        return 1;
    }

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

static int map_boot_memory(const HELLCORE_BOOT_INFO *boot);

static int identity_range_is_mapped(uint64_t start, uint64_t size) {
    if (!size || start >= ADDRESS_LIMIT ||
        size > ADDRESS_LIMIT - start)
        return 0;

    uint64_t end = start + size;
    uint64_t first_page = start & ~(PAGE_SIZE - 1);

    if (end > UINT64_MAX - (PAGE_SIZE - 1))
        return 0;

    end = (end + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);

    if (end > ADDRESS_LIMIT)
        return 0;

    for (uint64_t address = first_page; address < end;
         address += PAGE_SIZE) {
        uint64_t physical = 0;

        if (!hellcore_vmm_translate(address, &physical) ||
            physical != address)
            return 0;
    }

    return 1;
}

static int verify_identity_permissions(
    uint64_t start,
    uint64_t pages,
    int expected_write,
    int expected_nx
) {
    if (!pages || start >= ADDRESS_LIMIT ||
        (start & (PAGE_SIZE - 1)) ||
        pages > (ADDRESS_LIMIT - start) / PAGE_SIZE)
        return 0;

    for (uint64_t i = 0; i < pages; ++i) {
        uint64_t address = start + i * PAGE_SIZE;
        uint64_t physical = 0;
        uint64_t flags = 0;

        if (!hellcore_vmm_translate(address, &physical) ||
            physical != address)
            return 0;

        if (!hellcore_vmm_get_flags(address, &flags))
            return 0;

        if (!(flags & HELLCORE_VMM_PAGE_PRESENT))
            return 0;

        if (!!(flags & HELLCORE_VMM_PAGE_WRITE) != !!expected_write)
            return 0;

        if (!!(flags & HELLCORE_VMM_PAGE_NX) != !!expected_nx)
            return 0;
    }

    return 1;
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

    if (!boot->kernel_stack_base || !boot->kernel_stack_size)
        return 0;

    if (!!boot->pmm_bitmap_address != !!boot->pmm_bitmap_size)
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

    /*
     * Map UEFI loader resources and conventional memory before
     * changing CR3. The current implementation uses identity mapping.
     */
    if (!map_boot_memory(boot))
        return 0;

    /*
     * Verify every boot resource that must remain accessible
     * after the address-space switch.
     */
    if (!identity_range_is_mapped(
            (uint64_t)(uintptr_t)boot,
            sizeof(HELLCORE_BOOT_INFO)))
        return 0;

    if (!identity_range_is_mapped(
            boot->memory_map_address,
            boot->memory_map_size))
        return 0;

    if (!identity_range_is_mapped(
            boot->kernel_stack_base,
            boot->kernel_stack_size))
        return 0;

    if (boot->pmm_bitmap_address &&
        !identity_range_is_mapped(
            boot->pmm_bitmap_address,
            boot->pmm_bitmap_size))
        return 0;

    /*
     * Map kernel ELF segments using ABI v2 records from the UEFI loader.
     * Validate all records before changing page tables.
     */
    if (!boot->kernel_segment_count ||
        boot->kernel_segment_count > HELLCORE_BOOT_MAX_KERNEL_SEGMENTS)
        return 0;

    uint64_t segment_pages[HELLCORE_BOOT_MAX_KERNEL_SEGMENTS];

    for (uint32_t i = 0; i < boot->kernel_segment_count; ++i) {
        const HELLCORE_KERNEL_SEGMENT *seg =
            &boot->kernel_segments[i];

        if (!seg->memory_size ||
            (seg->virtual_address & (PAGE_SIZE - 1)) ||
            (seg->physical_address & (PAGE_SIZE - 1)) ||
            !(seg->flags & HELLCORE_ELF_PF_R) ||
            (seg->flags & ~(HELLCORE_ELF_PF_R |
                            HELLCORE_ELF_PF_W |
                            HELLCORE_ELF_PF_X)) ||
            ((seg->flags & HELLCORE_ELF_PF_W) &&
             (seg->flags & HELLCORE_ELF_PF_X)))
            return 0;

        if (seg->memory_size > UINT64_MAX - seg->virtual_address ||
            seg->memory_size > UINT64_MAX - seg->physical_address)
            return 0;

        uint64_t pages =
            seg->memory_size / PAGE_SIZE +
            ((seg->memory_size % PAGE_SIZE) != 0);

        if (!pages ||
            seg->physical_address >= ADDRESS_LIMIT ||
            pages > (ADDRESS_LIMIT - seg->physical_address) / PAGE_SIZE ||
            pages > (UINT64_MAX - seg->virtual_address) / PAGE_SIZE)
            return 0;

        segment_pages[i] = pages;

        uint64_t virtual_end =
            seg->virtual_address + pages * PAGE_SIZE;
        uint64_t physical_end =
            seg->physical_address + pages * PAGE_SIZE;

        for (uint32_t j = 0; j < i; ++j) {
            const HELLCORE_KERNEL_SEGMENT *prev =
                &boot->kernel_segments[j];

            uint64_t prev_virtual_end =
                prev->virtual_address + segment_pages[j] * PAGE_SIZE;
            uint64_t prev_physical_end =
                prev->physical_address + segment_pages[j] * PAGE_SIZE;

            if ((seg->virtual_address < prev_virtual_end &&
                 prev->virtual_address < virtual_end) ||
                (seg->physical_address < prev_physical_end &&
                 prev->physical_address < physical_end))
                return 0;
        }
    }

    /* Apply ELF permissions. All non-executable pages receive NX. */
    for (uint32_t i = 0; i < boot->kernel_segment_count; ++i) {
        const HELLCORE_KERNEL_SEGMENT *seg =
            &boot->kernel_segments[i];

        uint64_t map_flags = HELLCORE_VMM_MAP_READ;

        if (seg->flags & HELLCORE_ELF_PF_W)
            map_flags |= HELLCORE_VMM_MAP_WRITE;

        if (seg->flags & HELLCORE_ELF_PF_X)
            map_flags |= HELLCORE_VMM_MAP_EXECUTE;

        for (uint64_t page = 0; page < segment_pages[i]; ++page) {
            uint64_t va = seg->virtual_address + page * PAGE_SIZE;
            uint64_t pa = seg->physical_address + page * PAGE_SIZE;

            if (!hellcore_vmm_map_page(va, pa, map_flags))
                return 0;
        }
    }

    /* Verify every mapped page against its ABI record. */
    for (uint32_t i = 0; i < boot->kernel_segment_count; ++i) {
        const HELLCORE_KERNEL_SEGMENT *seg =
            &boot->kernel_segments[i];

        int expected_write = !!(seg->flags & HELLCORE_ELF_PF_W);
        int expected_nx = !(seg->flags & HELLCORE_ELF_PF_X);

        for (uint64_t page = 0; page < segment_pages[i]; ++page) {
            uint64_t va = seg->virtual_address + page * PAGE_SIZE;
            uint64_t expected_pa =
                seg->physical_address + page * PAGE_SIZE;

            uint64_t actual_pa = 0;
            uint64_t flags = 0;

            if (!hellcore_vmm_translate(va, &actual_pa) ||
                actual_pa != expected_pa ||
                !hellcore_vmm_get_flags(va, &flags) ||
                !(flags & HELLCORE_VMM_PAGE_PRESENT) ||
                (!!(flags & HELLCORE_VMM_PAGE_WRITE) != expected_write) ||
                (!!(flags & HELLCORE_VMM_PAGE_NX) != expected_nx))
                return 0;
        }
    }

/*
     * CR3 activation is deliberately deferred.
     * This function only constructs and verifies the new page tables.
     */

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
