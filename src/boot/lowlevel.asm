global init_stack
global halt_cpu

section .text
init_stack:
    cli
    cld
    mov esp, 0x90000
    mov ebp, esp
    ret

halt_cpu:
    hlt
    ret