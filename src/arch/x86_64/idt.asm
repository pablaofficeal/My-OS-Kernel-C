[BITS 64]
section .text
global idt_load
global isr_stub_0
global isr_stub_3
global isr_stub_8
global isr_stub_13
global isr_stub_14
global isr_stub_32

extern isr_handler

idt_load:
    lidt [rdi]
    ret

%macro ISR_NOERR 1
isr_stub_%1:
    push 0          ; error dummy
    push %1         ; vector
    jmp isr_common
%endmacro

%macro ISR_ERR 1
isr_stub_%1:
    push %1
    jmp isr_common
%endmacro

ISR_NOERR 0
ISR_NOERR 3
ISR_ERR   8
ISR_ERR   13
ISR_ERR   14
ISR_NOERR 32

isr_common:
    push rax
    push rbx
    push rcx
    push rdx
    push rsi
    push rdi
    push rbp
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15
    mov rdi, [rsp + 15*8]      ; vector
    mov rsi, [rsp + 16*8]      ; err
    mov rdx, [rsp + 17*8]      ; rip
    mov rcx, [rsp + 18*8]      ; cs
    mov r8,  [rsp + 19*8]      ; rflags
    ; align stack
    sub rsp, 8
    call isr_handler
    add rsp, 8
    pop r15
    pop r14
    pop r13
    pop r12
    pop r11
    pop r10
    pop r9
    pop r8
    pop rbp
    pop rdi
    pop rsi
    pop rdx
    pop rcx
    pop rbx
    pop rax
    add rsp, 16 ; drop vector+err
    iretq
