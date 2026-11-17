bits 64
default rel

section .text

extern hellcore_exception_handler

%macro ISR_NO_ERROR 1
global exception_stub_%1
exception_stub_%1:
    push qword 0
    push qword %1
    jmp exception_common
%endmacro

%macro ISR_ERROR 1
global exception_stub_%1
exception_stub_%1:
    push qword %1
    jmp exception_common
%endmacro

ISR_NO_ERROR 0
ISR_NO_ERROR 1
ISR_NO_ERROR 2
ISR_NO_ERROR 3
ISR_NO_ERROR 4
ISR_NO_ERROR 5
ISR_NO_ERROR 6
ISR_NO_ERROR 7
ISR_ERROR    8
ISR_NO_ERROR 9
ISR_ERROR    10
ISR_ERROR    11
ISR_ERROR    12
ISR_ERROR    13
ISR_ERROR    14
ISR_NO_ERROR 15
ISR_NO_ERROR 16
ISR_ERROR    17
ISR_NO_ERROR 18
ISR_NO_ERROR 19
ISR_ERROR    20
ISR_ERROR    21
ISR_NO_ERROR 22
ISR_NO_ERROR 23
ISR_NO_ERROR 24
ISR_NO_ERROR 25
ISR_NO_ERROR 26
ISR_NO_ERROR 27
ISR_NO_ERROR 28
ISR_ERROR    29
ISR_ERROR    30
ISR_NO_ERROR 31

exception_common:
    cld
    mov rdi, [rsp]
    mov rsi, [rsp + 8]
    mov rdx, [rsp + 16]
    and rsp, -16
    call hellcore_exception_handler

.halt:
    cli
    hlt
    jmp .halt

section .rodata
align 8
global exception_stub_table

exception_stub_table:
%assign i 0
%rep 32
    dq exception_stub_%+i
%assign i i+1
%endrep
