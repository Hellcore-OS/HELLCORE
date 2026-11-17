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

static const char *exception_name(uint64_t vector)
{
    static const char *names[32] = {
        "Divide Error",
        "Debug",
        "Non-Maskable Interrupt",
        "Breakpoint",
        "Overflow",
        "BOUND Range Exceeded",
        "Invalid Opcode",
        "Device Not Available",
        "Double Fault",
        "Coprocessor Segment Overrun",
        "Invalid TSS",
        "Segment Not Present",
        "Stack-Segment Fault",
        "General Protection Fault",
        "Page Fault",
        "Reserved",
        "x87 Floating-Point Exception",
        "Alignment Check",
        "Machine Check",
        "SIMD Floating-Point Exception",
        "Virtualization Exception",
        "Control Protection Exception",
        "Reserved",
        "Reserved",
        "Reserved",
        "Reserved",
        "Reserved",
        "Reserved",
        "Hypervisor Injection Exception",
        "VMM Communication Exception",
        "Security Exception",
        "Reserved"
    };

    return vector < 32 ? names[vector] : "Unknown Exception";
}

static inline uint64_t read_cr2(void)
{
    uint64_t value;
    __asm__ volatile ("mov %%cr2, %0" : "=r"(value));
    return value;
}

__attribute__((noreturn))
void hellcore_exception_handler(uint64_t vector,
                                uint64_t error_code,
                                uint64_t instruction_pointer)
{
    serial_write("\r\n!!! HELLCORE CPU EXCEPTION !!!\r\n");

    serial_write("EXCEPTION: ");
    serial_write(exception_name(vector));

    serial_write("\r\nVECTOR: ");
    serial_hex(vector);

    serial_write("\r\nERROR CODE: ");
    serial_hex(error_code);

    serial_write("\r\nINSTRUCTION RIP: ");
    serial_hex(instruction_pointer);

    if (vector == 14) {
        serial_write("\r\nFAULT ADDRESS (CR2): ");
        serial_hex(read_cr2());
    }

    serial_write("\r\nCreated by DJ Lucifer\r\n");

    for (;;) {
        __asm__ volatile ("cli; hlt");
    }
}
