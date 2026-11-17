#include <stdint.h>

static inline void outb(uint16_t port, uint8_t value)
{
    __asm__ volatile ("outb %0, %1" : : "a"(value), "Nd"(port));
}

static inline uint8_t inb(uint16_t port)
{
    uint8_t value;
    __asm__ volatile ("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static void serial_write(const char *text)
{
    while (*text) {
        while (!(inb(0x3FD) & 0x20)) {}
        outb(0x3F8, (uint8_t)*text++);
    }
}

static void serial_hex(uint64_t value)
{
    static const char digits[] = "0123456789ABCDEF";

    for (int shift = 60; shift >= 0; shift -= 4) {
        while (!(inb(0x3FD) & 0x20)) {}
        outb(0x3F8, digits[(value >> shift) & 0xF]);
    }
}

__attribute__((noreturn))
void hellcore_exception_handler(uint64_t vector,
                                uint64_t error_code,
                                uint64_t instruction_pointer)
{
    serial_write("\r\n!!! HELLCORE CPU EXCEPTION !!!\r\n");

    serial_write("EXCEPTION VECTOR: ");
    serial_hex(vector);
    serial_write("\r\nERROR CODE: ");
    serial_hex(error_code);
    serial_write("\r\nINSTRUCTION RIP: ");
    serial_hex(instruction_pointer);
    serial_write("\r\nCreated by DJ Lucifer\r\n");

    for (;;) {
        __asm__ volatile ("cli; hlt");
    }
}
