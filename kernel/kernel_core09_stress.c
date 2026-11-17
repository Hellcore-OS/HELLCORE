#include <stdint.h>
#include "hellcore_boot_info.h"
#include "pmm.h"

extern void hellcore_load_gdt(void);
extern void hellcore_load_idt(void);

static inline void outb(uint16_t port, uint8_t value) {
    __asm__ volatile ("outb %0, %1" : : "a"(value), "Nd"(port));
}

static inline uint8_t inb(uint16_t port) {
    uint8_t value;
    __asm__ volatile ("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static void serial_write(const char *s) {
    while (*s) {
        while (!(inb(0x3FD) & 0x20)) {}
        outb(0x3F8, (uint8_t)*s++);
    }
}

static void serial_hex(uint64_t value) {
    static const char digits[] = "0123456789ABCDEF";
    serial_write("0x");

    for (int i = 60; i >= 0; i -= 4) {
        char c[2] = { digits[(value >> i) & 15], 0 };
        serial_write(c);
    }
}

__attribute__((noreturn))
void kernel_main(const HELLCORE_BOOT_INFO *boot) {
    serial_write("\r\nHELLCORE KERNEL ENTRY REACHED\r\n");

    if (!boot || boot->magic != HELLCORE_BOOT_MAGIC ||
        boot->version != HELLCORE_BOOT_VERSION ||
        boot->structure_size < sizeof(HELLCORE_BOOT_INFO)) {
        serial_write("HELLCORE BOOT INFO VALIDATION FAILED\r\n");
        for (;;) {
            __asm__ volatile ("cli; hlt");
        }
    }

    serial_write("HELLCORE BOOT INFO VALIDATED\r\n");

    hellcore_load_gdt();
    serial_write("HELLCORE GDT LOADED SUCCESSFULLY\r\n");

    hellcore_load_idt();
    serial_write("HELLCORE IDT LOADED SUCCESSFULLY\r\n");

    if (boot->memory_map_address && boot->memory_map_size)
        serial_write("HELLCORE MEMORY MAP RECEIVED\r\n");

    if (boot->framebuffer_address && boot->framebuffer_size)
        serial_write("HELLCORE FRAMEBUFFER RECEIVED\r\n");

    if (boot->acpi_rsdp_address)
        serial_write("HELLCORE ACPI RSDP RECEIVED\r\n");

    HELLCORE_PMM_STATS pmm_stats;

    serial_write("HELLCORE PMM: SCANNING MEMORY MAP\r\n");

    if (!hellcore_pmm_init(boot, &pmm_stats)) {
        serial_write("HELLCORE PMM: MEMORY MAP VALIDATION FAILED\r\n");
        for (;;) {
            __asm__ volatile ("cli; hlt");
        }
    }

    serial_write("HELLCORE PMM: INVENTORY COMPLETE\r\n");
    serial_write("HELLCORE PMM USABLE RANGES: ");
    serial_hex(pmm_stats.usable_range_count);
    serial_write("\r\n");

    serial_write("HELLCORE PMM USABLE PAGES: ");
    serial_hex(pmm_stats.usable_page_count);
    serial_write("\r\n");

    HELLCORE_PMM_STATS before_test;
    HELLCORE_PMM_STATS after_test;
    uint64_t test_page;
    hellcore_pmm_get_stats(&before_test);
    serial_write("HELLCORE PMM TEST: ALLOCATING PAGE\r\n");
    test_page = hellcore_pmm_alloc_page();
    if (!test_page || (test_page & 4095ULL)) {
        serial_write("HELLCORE PMM TEST FAILED: ALLOCATION\r\n");
        for (;;) { __asm__ volatile ("cli; hlt"); }
    }
    hellcore_pmm_get_stats(&after_test);
    if (after_test.free_page_count + 1 != before_test.free_page_count ||
        after_test.allocated_page_count != before_test.allocated_page_count + 1) {
        serial_write("HELLCORE PMM TEST FAILED: ALLOCATION COUNTS\r\n");
        for (;;) { __asm__ volatile ("cli; hlt"); }
    }
    serial_write("HELLCORE PMM ALLOCATED PAGE: ");
    serial_hex(test_page);
    serial_write("\r\n");
    if (!hellcore_pmm_free_page(test_page)) {
        serial_write("HELLCORE PMM TEST FAILED: FREE\r\n");
        for (;;) { __asm__ volatile ("cli; hlt"); }
    }
    if (hellcore_pmm_free_page(test_page)) {
        serial_write("HELLCORE PMM TEST FAILED: DOUBLE FREE ACCEPTED\r\n");
        for (;;) { __asm__ volatile ("cli; hlt"); }
    }
    if (hellcore_pmm_free_page(0x100000ULL)) {
        serial_write("HELLCORE PMM TEST FAILED: KERNEL MEMORY RELEASED\r\n");
        for (;;) { __asm__ volatile ("cli; hlt"); }
    }
    if (hellcore_pmm_free_page(boot->kernel_stack_base)) {
        serial_write("HELLCORE PMM TEST FAILED: STACK MEMORY RELEASED\r\n");
        for (;;) { __asm__ volatile ("cli; hlt"); }
    }
    hellcore_pmm_get_stats(&after_test);
    if (after_test.free_page_count != before_test.free_page_count ||
        after_test.allocated_page_count != before_test.allocated_page_count) {
        serial_write("HELLCORE PMM TEST FAILED: FINAL COUNTS\r\n");
        for (;;) { __asm__ volatile ("cli; hlt"); }
    }
    serial_write("HELLCORE PMM TEST PASSED: ALLOCATE AND FREE\r\n");
    serial_write("HELLCORE PMM TEST PASSED: DOUBLE FREE BLOCKED\r\n");
    serial_write("HELLCORE PMM TEST PASSED: KERNEL AND STACK PROTECTED\r\n");

    /* Sequential, single-core PMM stress test. */
    uint64_t stress_pages[64];
    HELLCORE_PMM_STATS stress_before;
    HELLCORE_PMM_STATS stress_after;

    hellcore_pmm_get_stats(&stress_before);

    if (stress_before.free_page_count < 64) {
        serial_write("HELLCORE PMM STRESS FAILED: INSUFFICIENT FREE PAGES\r\n");
        for (;;) { __asm__ volatile ("cli; hlt"); }
    }

    serial_write("HELLCORE PMM STRESS: ALLOCATING 64 PAGES\r\n");

    for (uint64_t i = 0; i < 64; ++i) {
        uint64_t page = hellcore_pmm_alloc_page();

        if (!page || page < 0x100000ULL ||
            (page & (HELLCORE_PAGE_SIZE - 1))) {
            serial_write("HELLCORE PMM STRESS FAILED: INVALID PAGE\r\n");
            for (;;) { __asm__ volatile ("cli; hlt"); }
        }

        for (uint64_t j = 0; j < i; ++j) {
            if (stress_pages[j] == page) {
                serial_write("HELLCORE PMM STRESS FAILED: DUPLICATE PAGE\r\n");
                for (;;) { __asm__ volatile ("cli; hlt"); }
            }
        }

        stress_pages[i] = page;
    }

    hellcore_pmm_get_stats(&stress_after);

    if (stress_after.free_page_count != stress_before.free_page_count - 64 ||
        stress_after.allocated_page_count !=
            stress_before.allocated_page_count + 64) {
        serial_write("HELLCORE PMM STRESS FAILED: ALLOCATION ACCOUNTING\r\n");
        for (;;) { __asm__ volatile ("cli; hlt"); }
    }

    serial_write("HELLCORE PMM STRESS PASSED: 64 UNIQUE ALLOCATIONS\r\n");

    for (uint64_t i = 0; i < 64; ++i) {
        if (!hellcore_pmm_free_page(stress_pages[i])) {
            serial_write("HELLCORE PMM STRESS FAILED: PAGE RELEASE\r\n");
            for (;;) { __asm__ volatile ("cli; hlt"); }
        }
    }

    hellcore_pmm_get_stats(&stress_after);

    if (stress_after.free_page_count != stress_before.free_page_count ||
        stress_after.allocated_page_count !=
            stress_before.allocated_page_count) {
        serial_write("HELLCORE PMM STRESS FAILED: FREE ACCOUNTING\r\n");
        for (;;) { __asm__ volatile ("cli; hlt"); }
    }

    serial_write("HELLCORE PMM STRESS PASSED: 64 PAGE RELEASES\r\n");

    uint64_t reused_page = hellcore_pmm_alloc_page();
    int reuse_found = 0;

    for (uint64_t i = 0; i < 64; ++i) {
        if (stress_pages[i] == reused_page)
            reuse_found = 1;
    }

    if (!reused_page || !reuse_found ||
        !hellcore_pmm_free_page(reused_page)) {
        serial_write("HELLCORE PMM STRESS FAILED: PAGE REUSE\r\n");
        for (;;) { __asm__ volatile ("cli; hlt"); }
    }

    hellcore_pmm_get_stats(&stress_after);

    if (stress_after.free_page_count != stress_before.free_page_count ||
        stress_after.allocated_page_count !=
            stress_before.allocated_page_count) {
        serial_write("HELLCORE PMM STRESS FAILED: FINAL ACCOUNTING\r\n");
        for (;;) { __asm__ volatile ("cli; hlt"); }
    }

    serial_write("HELLCORE PMM STRESS PASSED: RELEASED PAGE REUSED\r\n");
    serial_write("HELLCORE PMM STRESS PASSED: FINAL COUNTS RESTORED\r\n");

    serial_write("HELLCORE KERNEL INITIALIZATION COMPLETE\r\n");
    serial_write("Created by DJ Lucifer\r\n");

    for (;;) {
        __asm__ volatile ("cli; hlt");
    }
}
