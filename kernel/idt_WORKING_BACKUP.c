#include <stdint.h>

extern void hellcore_interrupt_halt(void);

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

void hellcore_load_idt(void)
{
    uint64_t handler =
        (uint64_t)(uintptr_t)hellcore_interrupt_halt;

    for (unsigned int i = 0; i < 256; i++) {
        idt[i].offset_low = (uint16_t)handler;
        idt[i].selector = 0x08;
        idt[i].ist = 0;
        idt[i].attributes = 0x8E;
        idt[i].offset_middle = (uint16_t)(handler >> 16);
        idt[i].offset_high = (uint32_t)(handler >> 32);
        idt[i].reserved = 0;
    }

    HELLCORE_IDTR idtr = {
        .limit = sizeof(idt) - 1,
        .base = (uint64_t)(uintptr_t)idt
    };

    __asm__ volatile ("lidt %0" : : "m"(idtr) : "memory");
}
