#include <stdint.h>
#include "hellcore_boot_info.h"

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

    if (boot->memory_map_address && boot->memory_map_size)
        serial_write("HELLCORE MEMORY MAP RECEIVED\r\n");

    if (boot->framebuffer_address && boot->framebuffer_size)
        serial_write("HELLCORE FRAMEBUFFER RECEIVED\r\n");

    if (boot->acpi_rsdp_address)
        serial_write("HELLCORE ACPI RSDP RECEIVED\r\n");

    serial_write("HELLCORE KERNEL INITIALIZATION COMPLETE\r\n");
    serial_write("Created by DJ Lucifer\r\n");

    for (;;) {
        __asm__ volatile ("cli; hlt");
    }
}
