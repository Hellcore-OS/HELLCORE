bits 64
default rel

section .text
global kernel_entry
extern kernel_main

; Entry contract:
; RDI = HELLCORE_BOOT_INFO pointer
; RSP = kernel stack top, 16-byte aligned

kernel_entry:
    ; Clear direction flag for C code
    cld

    ; Align stack before calling System V C function
    and rsp, -16
    call kernel_main

.halt:
    cli
    hlt
    jmp .halt
