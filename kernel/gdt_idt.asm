bits 64
default rel

section .rodata
align 8

gdt64:
    dq 0x0000000000000000
    dq 0x00AF9B000000FFFF
    dq 0x00CF93000000FFFF
gdt64_end:

gdt64_pointer:
    dw gdt64_end - gdt64 - 1
    dq gdt64

section .text
global hellcore_load_gdt
global hellcore_interrupt_halt

hellcore_load_gdt:
    cli
    lgdt [rel gdt64_pointer]

    push qword 0x08
    lea rax, [rel .reload_cs]
    push rax
    retfq

.reload_cs:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov ss, ax
    xor eax, eax
    ret

hellcore_interrupt_halt:
    cli
.halt:
    hlt
    jmp .halt
