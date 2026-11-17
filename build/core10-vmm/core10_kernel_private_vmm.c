#include <stdint.h>
#include "hellcore_boot_info.h"
#include "pmm.h"
#include "core10_paging_diag.h"
#include "core10_paging_inspector.h"
#include "core10_vmm_takeover.h"
#include "core10_private_vmm_v3.h"

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


static __attribute__((noreturn))
void v3_fail(const char *message)
{
    serial_write("CORE 1.0 V3 TEST FAILED: ");
    serial_write(message);
    serial_write("\r\n");
    for (;;) { __asm__ volatile ("cli; hlt"); }
}

static void v3_expect(int condition, const char *message)
{
    if (!condition)
        v3_fail(message);

    serial_write("CORE 1.0 V3 TEST PASSED: ");
    serial_write(message);
    serial_write("\r\n");
}

static int v3_stats_equal(
    const HELLCORE_PMM_STATS *a,
    const HELLCORE_PMM_STATS *b
)
{
    return a->usable_range_count == b->usable_range_count &&
           a->usable_page_count == b->usable_page_count &&
           a->free_page_count == b->free_page_count &&
           a->allocated_page_count == b->allocated_page_count;
}

static void v3_private_mapping_tests(uint64_t root, uint32_t physical_bits)
{
    uint64_t data[5] = {0};
    uint64_t base = 0;
    uint64_t failure_va[3] = {0};
    uint64_t physical = 0;
    uint64_t flags = 0;
    uint64_t shared0;
    uint64_t shared1;

    HELLCORE_PMM_STATS before;
    HELLCORE_PMM_STATS after;

    serial_write("\r\nHELLCORE CORE 1.0 PRIVATE VMM V3 TESTS\r\n");

    v3_expect(
        hellcore_core10_v3_init(root, physical_bits),
        "V3 initialization"
    );

    shared0 = hellcore_core10_v3_root_entry(0);
    shared1 = hellcore_core10_v3_root_entry(1);

    for (uint32_t i = 0; i < 5; ++i) {
        data[i] = hellcore_pmm_alloc_page();
        if (!data[i] || (data[i] & 4095ULL))
            v3_fail("data-frame allocation");
    }

    v3_expect(1, "five PMM data frames allocated");

    v3_expect(
        hellcore_core10_v3_find_private_address(&base),
        "private PML4 slot reservation"
    );

    for (uint32_t i = 0; i < 3; ++i) {
        v3_expect(
            hellcore_core10_v3_find_private_address(&failure_va[i]),
            "additional rollback-test slot reservation"
        );
    }

    /* First-branch allocation failure at each allocation boundary. */
    for (int32_t budget = 0; budget < 3; ++budget) {
        uint32_t slot = (uint32_t)((failure_va[budget] >> 39) & 511);

        hellcore_pmm_get_stats(&before);
        hellcore_core10_v3_test_fail_after(budget);

        int result = hellcore_core10_v3_map_page(
            failure_va[budget],
            data[4],
            CORE10_V3_READ
        );

        hellcore_core10_v3_test_fail_after(-1);
        hellcore_pmm_get_stats(&after);

        v3_expect(!result, "injected allocation failure rejected");
        v3_expect(
            hellcore_core10_v3_root_entry(slot) == 0,
            "failed first branch remains unpublished"
        );
        v3_expect(
            v3_stats_equal(&before, &after),
            "first-branch rollback restores PMM statistics"
        );
    }

    /* Verify that an unreserved upper-half slot cannot be mapped. */
    uint64_t unreserved_va = 0;
    for (int32_t i = 511; i >= 256; --i) {
        if ((uint32_t)i == ((base >> 39) & 511))
            continue;

        if ((uint32_t)i == ((failure_va[0] >> 39) & 511) ||
            (uint32_t)i == ((failure_va[1] >> 39) & 511) ||
            (uint32_t)i == ((failure_va[2] >> 39) & 511))
            continue;

        if (hellcore_core10_v3_root_entry((uint32_t)i) == 0) {
            unreserved_va =
                0xFFFF000000000000ULL |
                ((uint64_t)i << 39) |
                0x12345000ULL;
            break;
        }
    }

    v3_expect(unreserved_va != 0, "unreserved test slot located");
    v3_expect(
        !hellcore_core10_v3_map_page(
            unreserved_va, data[4], CORE10_V3_READ
        ),
        "unreserved PML4 mapping rejected"
    );

    /* Create the first private branch. */
    v3_expect(
        hellcore_core10_v3_map_page(
            base, data[0],
            CORE10_V3_READ | CORE10_V3_WRITE
        ),
        "first private mapping"
    );

    /* Fail creation of a new PD branch under the existing PML4 slot. */
    uint64_t new_pd_va = base + 0x40000000ULL;

    hellcore_pmm_get_stats(&before);
    hellcore_core10_v3_test_fail_after(1);

    int pd_result = hellcore_core10_v3_map_page(
        new_pd_va, data[2], CORE10_V3_READ
    );

    hellcore_core10_v3_test_fail_after(-1);
    hellcore_pmm_get_stats(&after);

    v3_expect(!pd_result, "new PD allocation failure rejected");
    v3_expect(
        v3_stats_equal(&before, &after),
        "new PD rollback restores PMM statistics"
    );
    v3_expect(
        !hellcore_core10_v3_translate(new_pd_va, &physical),
        "failed PD branch has no visible mapping"
    );

    /* Fail creation of a new PT branch under the existing PD. */
    uint64_t new_pt_va = base + 0x200000ULL;

    hellcore_pmm_get_stats(&before);
    hellcore_core10_v3_test_fail_after(0);

    int pt_result = hellcore_core10_v3_map_page(
        new_pt_va, data[2], CORE10_V3_READ
    );

    hellcore_core10_v3_test_fail_after(-1);
    hellcore_pmm_get_stats(&after);

    v3_expect(!pt_result, "new PT allocation failure rejected");
    v3_expect(
        v3_stats_equal(&before, &after),
        "new PT rollback restores PMM statistics"
    );
    v3_expect(
        !hellcore_core10_v3_translate(new_pt_va, &physical),
        "failed PT branch has no visible mapping"
    );

    /* Multiple mappings in the same and separate lower-level branches. */
    v3_expect(
        hellcore_core10_v3_map_page(
            base + 0x1000ULL, data[1], CORE10_V3_READ
        ),
        "second mapping in existing PT"
    );

    v3_expect(
        hellcore_core10_v3_map_page(
            new_pt_va, data[2], CORE10_V3_READ
        ),
        "new PT branch mapping"
    );

    v3_expect(
        hellcore_core10_v3_map_page(
            new_pd_va, data[3], CORE10_V3_READ
        ),
        "new PD branch mapping"
    );

    v3_expect(
        hellcore_core10_v3_translate(base, &physical) &&
        physical == data[0],
        "first address translation"
    );

    v3_expect(
        hellcore_core10_v3_translate(base + 0x1000ULL, &physical) &&
        physical == data[1],
        "same-PT address translation"
    );

    v3_expect(
        hellcore_core10_v3_translate(new_pt_va, &physical) &&
        physical == data[2],
        "new-PT address translation"
    );

    v3_expect(
        hellcore_core10_v3_translate(new_pd_va, &physical) &&
        physical == data[3],
        "new-PD address translation"
    );

    v3_expect(
        hellcore_core10_v3_get_flags(base, &flags) &&
        (flags & CORE10_V3_FLAG_WRITE) &&
        (flags & CORE10_V3_FLAG_NX),
        "writable non-executable permissions"
    );

    v3_expect(
        hellcore_core10_v3_get_flags(base + 0x1000ULL, &flags) &&
        !(flags & CORE10_V3_FLAG_WRITE) &&
        (flags & CORE10_V3_FLAG_NX),
        "read-only non-executable permissions"
    );

    v3_expect(
        !hellcore_core10_v3_map_page(
            base, data[4], CORE10_V3_READ
        ),
        "duplicate mapping rejected"
    );

    v3_expect(
        !hellcore_core10_v3_map_page(
            base + 0x5000ULL, data[4],
            CORE10_V3_READ | CORE10_V3_WRITE | CORE10_V3_EXECUTE
        ),
        "writable executable mapping rejected"
    );

    v3_expect(
        hellcore_core10_v3_root_entry(0) == shared0 &&
        hellcore_core10_v3_root_entry(1) == shared1,
        "inherited lower-half root entries unchanged"
    );

    v3_expect(
        hellcore_core10_v3_unmap_page(base),
        "first mapping unmap"
    );

    v3_expect(
        hellcore_core10_v3_unmap_page(base + 0x1000ULL),
        "same-PT mapping unmap"
    );

    v3_expect(
        hellcore_core10_v3_unmap_page(new_pt_va),
        "new-PT mapping unmap"
    );

    v3_expect(
        hellcore_core10_v3_unmap_page(new_pd_va),
        "new-PD mapping unmap"
    );

    v3_expect(
        !hellcore_core10_v3_translate(base, &physical) &&
        !hellcore_core10_v3_translate(base + 0x1000ULL, &physical) &&
        !hellcore_core10_v3_translate(new_pt_va, &physical) &&
        !hellcore_core10_v3_translate(new_pd_va, &physical),
        "unmapped virtual addresses no longer translate"
    );

    for (uint32_t i = 0; i < 5; ++i) {
        if (!hellcore_pmm_free_page(data[i]))
            v3_fail("data-frame release");
    }

    CORE10_V3_REPORT report;
    hellcore_core10_v3_get_report(&report);

    serial_write("V3 OWNED TABLES: ");
    serial_hex(report.owned_tables);
    serial_write("\r\nV3 MAPPINGS CREATED: ");
    serial_hex(report.mappings_created);
    serial_write("\r\nV3 MAPPINGS REMOVED: ");
    serial_hex(report.mappings_removed);
    serial_write("\r\nV3 ALLOCATION ROLLBACKS: ");
    serial_hex(report.allocation_rollbacks);
    serial_write("\r\n");

    v3_expect(
        report.mappings_created == 4 &&
        report.mappings_removed == 4 &&
        report.owned_tables == 6,
        "V3 mapping and ownership totals"
    );

    serial_write("HELLCORE CORE 1.0 PRIVATE VMM V3 TESTS COMPLETE\r\n");
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

    HELLCORE_CORE10_PAGING_STATE paging;
    serial_write("\r\nHELLCORE CORE 1.0 PAGING DIAGNOSTICS\r\n");
    if (!hellcore_core10_read_paging_state(&paging)) {
        serial_write("CORE 1.0 PAGING DIAGNOSTIC FAILED\r\n");
        for (;;) { __asm__ volatile ("cli; hlt"); }
    }
    serial_write("CR0: "); serial_hex(paging.cr0); serial_write("\r\n");
    serial_write("CR3: "); serial_hex(paging.cr3); serial_write("\r\n");
    serial_write("CR4: "); serial_hex(paging.cr4); serial_write("\r\n");
    serial_write("EFER: "); serial_hex(paging.efer); serial_write("\r\n");
    serial_write("PHYSICAL ADDRESS BITS: "); serial_hex(paging.physical_address_bits); serial_write("\r\n");
    serial_write("NX SUPPORTED: "); serial_hex(paging.nx_supported); serial_write("\r\n");
    serial_write("NX ENABLED: "); serial_hex(paging.nx_enabled); serial_write("\r\n");
    serial_write("LA57 SUPPORTED: "); serial_hex(paging.la57_supported); serial_write("\r\n");
    serial_write("LA57 ACTIVE: "); serial_hex(paging.la57_active); serial_write("\r\n");
    serial_write("CORE 1.0 PAGING DIAGNOSTICS COMPLETE\r\n");

    serial_write("HELLCORE IDT LOADED SUCCESSFULLY\r\n");

    serial_write("\r\nHELLCORE BOOT ADDRESS DIAGNOSTICS\r\n");

    serial_write("BOOT INFO ADDRESS: ");
    serial_hex((uint64_t)(uintptr_t)boot);
    serial_write("\r\n");

    serial_write("MEMORY MAP ADDRESS: ");
    serial_hex(boot->memory_map_address);
    serial_write("\r\n");

    serial_write("MEMORY MAP SIZE: ");
    serial_hex(boot->memory_map_size);
    serial_write("\r\n");

    serial_write("MEMORY MAP DESCRIPTOR SIZE: ");
    serial_hex(boot->descriptor_size);
    serial_write("\r\n");

    serial_write("FRAMEBUFFER ADDRESS: ");
    serial_hex(boot->framebuffer_address);
    serial_write("\r\n");

    serial_write("FRAMEBUFFER SIZE: ");
    serial_hex(boot->framebuffer_size);
    serial_write("\r\n");

    serial_write("ACPI RSDP ADDRESS: ");
    serial_hex(boot->acpi_rsdp_address);
    serial_write("\r\n");

    serial_write("KERNEL STACK BASE: ");
    serial_hex(boot->kernel_stack_base);
    serial_write("\r\n");

    serial_write("KERNEL STACK SIZE: ");
    serial_hex(boot->kernel_stack_size);
    serial_write("\r\n");

    serial_write("PMM BITMAP ADDRESS: ");
    serial_hex(boot->pmm_bitmap_address);
    serial_write("\r\n");

    serial_write("PMM BITMAP SIZE: ");
    serial_hex(boot->pmm_bitmap_size);
    serial_write("\r\n");

    serial_write("HELLCORE BOOT ADDRESS DIAGNOSTICS COMPLETE\r\n");

    CORE10_PAGING_REPORT inherited_report;

    serial_write("\r\nHELLCORE CORE 1.0 INHERITED PAGING INSPECTION\r\n");

    if (!hellcore_core10_inspect_inherited(boot, &inherited_report)) {
        serial_write("INHERITED PAGING INSPECTION FAILED\r\n");
        for (;;) { __asm__ volatile ("cli; hlt"); }
    }

    serial_write("ACTIVE CR3: ");
    serial_hex(inherited_report.cr3);
    serial_write("\r\n");

    serial_write("PHYSICAL ADDRESS WIDTH: ");
    serial_hex(inherited_report.physical_bits);
    serial_write("\r\n");

    for (uint32_t i = 0; i < inherited_report.count; ++i) {
        CORE10_MAPPING_CHECK *check = &inherited_report.checks[i];

        serial_write(check->passed ? "MAPPING PASS: " : "MAPPING FAIL: ");
        serial_write(check->name);
        serial_write("\r\nVIRTUAL: ");
        serial_hex(check->virtual_address);
        serial_write("\r\nPHYSICAL: ");
        serial_hex(check->physical_address);
        serial_write("\r\nEFFECTIVE FLAGS: ");
        serial_hex(check->flags);
        serial_write("\r\n");
    }

    serial_write("MAPPING CHECKS PASSED: ");
    serial_hex(inherited_report.passed);
    serial_write("\r\nMAPPING CHECKS FAILED: ");
    serial_hex(inherited_report.failed);
    serial_write("\r\n");

    serial_write("INHERITED PAGING INSPECTION COMPLETE\r\n");



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


    serial_write("\r\nHELLCORE CORE 1.0 SAFE VMM TAKEOVER\r\n");

    CORE10_VMM_TAKEOVER_REPORT takeover_report;

    if (!hellcore_core10_takeover_root(&takeover_report)) {
        serial_write("SAFE VMM TAKEOVER FAILED\r\n");
        serial_write("OLD CR3: ");
        serial_hex(takeover_report.old_cr3);
        serial_write("\r\nNEW ROOT: ");
        serial_hex(takeover_report.new_root);
        serial_write("\r\nENTRIES CHECKED: ");
        serial_hex(takeover_report.entries_checked);
        serial_write("\r\n");
        for (;;) { __asm__ volatile ("cli; hlt"); }
    }

    serial_write("SAFE VMM ROOT ALLOCATED\r\n");
    serial_write("OLD CR3: ");
    serial_hex(takeover_report.old_cr3);
    serial_write("\r\nNEW CR3: ");
    serial_hex(takeover_report.new_cr3);
    serial_write("\r\nNEW ROOT: ");
    serial_hex(takeover_report.new_root);
    serial_write("\r\nPHYSICAL ADDRESS WIDTH: ");
    serial_hex(takeover_report.physical_bits);
    serial_write("\r\nPML4 ENTRIES VALIDATED: ");
    serial_hex(takeover_report.entries_checked);
    serial_write("\r\n");

    if (!takeover_report.root_validated ||
        takeover_report.entries_checked != 512 ||
        !takeover_report.switched ||
        !takeover_report.readback_validated) {
        serial_write("SAFE VMM ROOT VALIDATION FAILED\r\n");
        for (;;) { __asm__ volatile ("cli; hlt"); }
    }

    serial_write("SAFE VMM CR3 SWITCH AND READBACK PASSED\r\n");

    CORE10_PAGING_REPORT post_report;

    if (!hellcore_core10_inspect_inherited(boot, &post_report)) {
        serial_write("POST-SWITCH PAGING INSPECTION FAILED\r\n");
        for (;;) { __asm__ volatile ("cli; hlt"); }
    }

    serial_write("POST-SWITCH MAPPING CHECKS PASSED: ");
    serial_hex(post_report.passed);
    serial_write("\r\nPOST-SWITCH MAPPING CHECKS FAILED: ");
    serial_hex(post_report.failed);
    serial_write("\r\n");

    if (post_report.count != CORE10_MAX_MAPPING_CHECKS ||
        post_report.passed != CORE10_MAX_MAPPING_CHECKS ||
        post_report.failed != 0) {
        serial_write("SAFE VMM POST-SWITCH MAPPING VALIDATION FAILED\r\n");
        for (;;) { __asm__ volatile ("cli; hlt"); }
    }

    serial_write("SAFE VMM POST-SWITCH MAPPINGS: 10/10 PASSED\r\n");

    v3_private_mapping_tests(
        takeover_report.new_root,
        takeover_report.physical_bits
    );

    serial_write("HELLCORE CORE 1.0 SAFE VMM TAKEOVER COMPLETE\r\n");

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
