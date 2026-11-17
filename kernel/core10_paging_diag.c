#include "core10_paging_diag.h"

#define EFER_MSR 0xC0000080U
#define EFER_NXE (1ULL << 11)

static void read_cpuid(
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

int hellcore_core10_read_paging_state(
    HELLCORE_CORE10_PAGING_STATE *state
) {
    if (!state)
        return 0;

    uint32_t a, b, c, d;
    uint32_t max_extended;
    uint32_t max_standard;

    __asm__ volatile ("mov %%cr0, %0" : "=r"(state->cr0));
    __asm__ volatile ("mov %%cr3, %0" : "=r"(state->cr3));
    __asm__ volatile ("mov %%cr4, %0" : "=r"(state->cr4));

    state->efer = read_msr(EFER_MSR);

    read_cpuid(0, 0, &max_standard, &b, &c, &d);
    read_cpuid(0x80000000U, 0, &max_extended, &b, &c, &d);

    state->physical_address_bits = 0;
    state->nx_supported = 0;
    state->la57_supported = 0;

    if (max_extended >= 0x80000001U) {
        read_cpuid(0x80000001U, 0, &a, &b, &c, &d);
        state->nx_supported = (d >> 20) & 1U;
    }

    if (max_extended >= 0x80000008U) {
        read_cpuid(0x80000008U, 0, &a, &b, &c, &d);
        state->physical_address_bits = a & 0xFFU;
    }

    if (max_standard >= 7) {
        read_cpuid(7, 0, &a, &b, &c, &d);
        state->la57_supported = (c >> 16) & 1U;
    }

    state->nx_enabled = (state->efer & EFER_NXE) != 0;
    state->la57_active = (state->cr4 & (1ULL << 12)) != 0;

    return 1;
}
