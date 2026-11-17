bits 64
default rel

section .text
global hellcore_enter_kernel

; Microsoft x64 entry:
; RCX = boot information pointer
; RDX = kernel entry address
; R8  = kernel stack base
; R9  = kernel stack size

hellcore_enter_kernel:
    cli
    cld

    mov rax, rdx
    mov rsp, r8
    add rsp, r9
    and rsp, -16

    ; Pass boot information using the System V x86-64 ABI.
    mov rdi, rcx
    xor rbp, rbp

    ; Transfer control to the kernel.
    jmp rax
