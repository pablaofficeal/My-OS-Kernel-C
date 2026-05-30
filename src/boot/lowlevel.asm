; lowlevel.asm - Низкоуровневые функции на ассемблере
[BITS 32]

section .text

; Простая функция вывода для отладки (VGA text mode)
; void simple_print(const char* str)
; Использует статическую переменную для позиции
section .bss
simple_print_pos: resd 1

section .text
global simple_print
simple_print:
    push ebp
    mov ebp, esp
    push esi
    push edi
    push ebx
    
    mov esi, [ebp + 8]  ; str
    mov edi, 0xB8000    ; VGA text buffer
    mov ecx, [simple_print_pos]  ; pos из статической переменной
    
.loop:
    mov al, [esi]
    test al, al
    jz .done
    
    cmp al, 10          ; '\n'
    je .newline
    
    ; Обычный символ
    mov ah, 0x07        ; Белый на черном
    mov [edi + ecx * 2], ax
    inc ecx
    
    ; Проверка границ
    cmp ecx, 80 * 25
    jl .next
    mov ecx, 0
    
.next:
    inc esi
    jmp .loop

.newline:
    ; Переход на новую строку
    mov eax, ecx
    mov edx, 0
    mov ebx, 80
    div ebx
    inc eax
    mul ebx
    mov ecx, eax
    cmp ecx, 80 * 25
    jl .next
    mov ecx, 0
    jmp .next

.done:
    mov [simple_print_pos], ecx  ; Сохраняем позицию
    pop ebx
    pop edi
    pop esi
    pop ebp
    ret

; Получение Multiboot info pointer из EBX
; void* get_multiboot_info_from_ebx(void)
global get_multiboot_info_from_ebx
get_multiboot_info_from_ebx:
    mov eax, ebx
    ret

; Инициализация стека
; void init_stack(void)
global init_stack
init_stack:
    cli                     ; Отключаем прерывания
    cld                     ; Очищаем флаг направления
    mov esp, 0x90000        ; Устанавливаем стек (выше ядра)
    mov ebp, esp            ; Устанавливаем базовый указатель стека
    ret

; Остановка процессора
; void halt_cpu(void)
global halt_cpu
halt_cpu:
    hlt
    ret

