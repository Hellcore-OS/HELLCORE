#include "core10_paging_inspector.h"

#define PTE_PRESENT (1ULL << 0)
#define PTE_WRITE   (1ULL << 1)
#define PTE_USER    (1ULL << 2)
#define PTE_HUGE    (1ULL << 7)
#define PTE_NX      (1ULL << 63)

static void read_cpuid(
    uint32_t leaf, uint32_t subleaf,
    uint32_t *a, uint32_t *b,
    uint32_t *c, uint32_t *d
) {
    __asm__ volatile (
        "cpuid"
        : "=a"(*a), "=b"(*b), "=c"(*c), "=d"(*d)
        : "a"(leaf), "c"(subleaf)
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

static int canonical(uint64_t address) {
    uint64_t upper = address >> 48;
    return (address & (1ULL << 47))
        ? upper == 0xFFFF
        : upper == 0;
}

/*
 * This inspector reads the inherited page tables through their
 * identity-mapped physical addresses. It does not modify any entry.
 */
static int translate(
    uint64_t root,
    uint64_t address,
    uint64_t physical_mask,
    uint64_t *physical,
    uint64_t *flags
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
        (volatile uint64_t *)(uintptr_t)(e4 & physical_mask);

    uint64_t e3 = pdpt[i3];
    if (!(e3 & PTE_PRESENT))
        return 0;

    uint64_t effective_write =
        (e4 & PTE_WRITE) && (e3 & PTE_WRITE);
    uint64_t effective_user =
        (e4 & PTE_USER) && (e3 & PTE_USER);
    uint64_t effective_nx = (e4 | e3) & PTE_NX;

    if (e3 & PTE_HUGE) {
        *physical =
            (e3 & (physical_mask & ~((1ULL << 30) - 1))) |
            (address & ((1ULL << 30) - 1));
        *flags = (effective_write ? 1ULL : 0) |
                 (effective_user ? 2ULL : 0) |
                 (effective_nx ? 4ULL : 0) | 8ULL;
        return 1;
    }

    volatile uint64_t *pd =
        (volatile uint64_t *)(uintptr_t)(e3 & physical_mask);

    uint64_t e2 = pd[i2];
    if (!(e2 & PTE_PRESENT))
        return 0;

    effective_write = effective_write && (e2 & PTE_WRITE);
    effective_user = effective_user && (e2 & PTE_USER);
    effective_nx |= e2 & PTE_NX;

    if (e2 & PTE_HUGE) {
        *physical =
            (e2 & (physical_mask & ~((1ULL << 21) - 1))) |
            (address & ((1ULL << 21) - 1));
        *flags = (effective_write ? 1ULL : 0) |
                 (effective_user ? 2ULL : 0) |
                 (effective_nx ? 4ULL : 0) | 8ULL;
        return 1;
    }

    volatile uint64_t *pt =
        (volatile uint64_t *)(uintptr_t)(e2 & physical_mask);

    uint64_t e1 = pt[i1];
    if (!(e1 & PTE_PRESENT))
        return 0;

    effective_write = effective_write && (e1 & PTE_WRITE);
    effective_user = effective_user && (e1 & PTE_USER);
    effective_nx |= e1 & PTE_NX;

    *physical = (e1 & physical_mask) | (address & 0xFFF);
    *flags = (effective_write ? 1ULL : 0) |
             (effective_user ? 2ULL : 0) |
             (effective_nx ? 4ULL : 0);
    return 1;
}

static void check_mapping(
    CORE10_PAGING_REPORT *r,
    const char *name,
    uint64_t address,
    uint64_t mask
) {
    if (!address || r->count >= CORE10_MAX_MAPPING_CHECKS)
        return;

    CORE10_MAPPING_CHECK *c = &r->checks[r->count++];
    c->name = name;
    c->virtual_address = address;
    c->physical_address = 0;
    c->flags = 0;
    c->passed = 0;

    if (translate(r->cr3 & mask, address, mask,
                  &c->physical_address, &c->flags) &&
        c->physical_address == address) {
        c->passed = 1;
        r->passed++;
    } else {
        r->failed++;
    }
}

int hellcore_core10_inspect_inherited(
    const HELLCORE_BOOT_INFO *boot,
    CORE10_PAGING_REPORT *report
) {
    if (!boot || !report)
        return 0;

    report->cr3 = 0;
    report->physical_bits = 0;
    report->count = 0;
    report->passed = 0;
    report->failed = 0;

    uint32_t a, b, c, d;
    read_cpuid(0x80000000U, 0, &a, &b, &c, &d);
    if (a < 0x80000008U)
        return 0;

    read_cpuid(0x80000008U, 0, &a, &b, &c, &d);
    uint32_t bits = a & 0xFF;

    if (bits < 36 || bits > 52)
        return 0;

    report->physical_bits = bits;

    uint64_t mask = ((1ULL << bits) - 1) & ~0xFFFULL;
    report->cr3 = read_cr3();

    if ((read_cr4() & (1ULL << 12)) != 0)
        return 0;

    uint64_t root = report->cr3 & mask;
    if (!root || (root & 0xFFF))
        return 0;

    uint64_t rsp;
    __asm__ volatile ("mov %%rsp, %0" : "=r"(rsp));

    check_mapping(report, "KERNEL ENTRY", 0x101000, mask);
    check_mapping(report, "CURRENT STACK", rsp, mask);
    check_mapping(report, "BOOT INFO", (uint64_t)(uintptr_t)boot, mask);
    check_mapping(report, "MEMORY MAP", boot->memory_map_address, mask);
    check_mapping(report, "STACK BASE", boot->kernel_stack_base, mask);

    if (boot->kernel_stack_size &&
        boot->kernel_stack_base <= UINT64_MAX - boot->kernel_stack_size) {
        check_mapping(report, "STACK END",
            boot->kernel_stack_base + boot->kernel_stack_size - 1, mask);
    }

    check_mapping(report, "PMM BITMAP", boot->pmm_bitmap_address, mask);
    check_mapping(report, "FRAMEBUFFER", boot->framebuffer_address, mask);

    if (boot->framebuffer_size &&
        boot->framebuffer_address <= UINT64_MAX - boot->framebuffer_size) {
        check_mapping(report, "FRAMEBUFFER END",
            boot->framebuffer_address + boot->framebuffer_size - 1, mask);
    }

    check_mapping(report, "ACPI RSDP", boot->acpi_rsdp_address, mask);

    return 1;
}
