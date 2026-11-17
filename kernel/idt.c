#include <stdint.h>

extern void hellcore_interrupt_halt(void);
extern void *exception_stub_table[32];

typedef struct __attribute__((packed)) {
    uint16_t offset_low;
    uint16_t selector;
    uint8_t ist;
    uint8_t attributes;
    uint16_t offset_middle;
    uint32_t offset_high;
    uint32_t reserved;
} HELLCORE_IDT_GATE;

typedef struct __attribute__((packed)) {
    uint16_t limit;
    uint64_t base;
} HELLCORE_IDTR;

static HELLCORE_IDT_GATE idt[256]
    __attribute__((aligned(16)));

static void set_gate(unsigned int vector, uint64_t handler)
{
    idt[vector].offset_low = (uint16_t)handler;
    idt[vector].selector = 0x08;
    idt[vector].ist = 0;
    idt[vector].attributes = 0x8E;
    idt[vector].offset_middle = (uint16_t)(handler >> 16);
    idt[vector].offset_high = (uint32_t)(handler >> 32);
    idt[vector].reserved = 0;
}

void hellcore_load_idt(void)
{
    uint64_t halt_handler =
        (uint64_t)(uintptr_t)hellcore_interrupt_halt;

    for (unsigned int i = 0; i < 256; i++) {
        set_gate(i, halt_handler);
    }

    for (unsigned int i = 0; i < 32; i++) {
        set_gate(i, (uint64_t)(uintptr_t)exception_stub_table[i]);
    }

    HELLCORE_IDTR idtr = {
        .limit = sizeof(idt) - 1,
        .base = (uint64_t)(uintptr_t)idt
    };

    __asm__ volatile ("lidt %0" : : "m"(idtr) : "memory");
}
