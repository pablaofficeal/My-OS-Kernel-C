; boot.asm - Загрузчик для BIOS
; Загружает ядро с диска и передает управление

[BITS 16]
[ORG 0x7C00]

; Начало загрузчика
start:
    ; Инициализация сегментов
    cli                     ; Отключаем прерывания
    xor ax, ax              ; Обнуляем AX
    mov ds, ax              ; Устанавливаем DS = 0
    mov es, ax              ; Устанавливаем ES = 0
    mov ss, ax              ; Устанавливаем SS = 0
    mov sp, 0x7C00          ; Устанавливаем стек
    
    sti                     ; Включаем прерывания
    
    ; Сохраняем номер диска
    mov [boot_drive], dl
    
    ; Выводим сообщение о загрузке
    mov si, msg_loading
    call print_string
    
    ; Загружаем ядро с диска
    mov bx, KERNEL_LOAD_SEGMENT
    mov es, bx
    mov bx, KERNEL_LOAD_OFFSET
    mov dh, KERNEL_SECTORS   ; Количество секторов для загрузки
    mov dl, [boot_drive]     ; Номер диска
    call disk_load
    
    ; Проверяем, что загрузка прошла успешно
    cmp ax, 0
    jne load_error
    
    ; Выводим сообщение об успешной загрузке
    mov si, msg_loaded
    call print_string
    
    ; Переходим в защищенный режим
    call switch_to_pm
    
    ; Сюда мы не должны попасть
    jmp $

; Функция загрузки с диска
; ES:BX - адрес буфера
; DH - количество секторов
; DL - номер диска
disk_load:
    pusha
    push dx                 ; Сохраняем DX (DH содержит количество секторов)
    
    mov ah, 0x02            ; Функция чтения секторов
    mov al, dh              ; Количество секторов
    mov ch, 0x00            ; Цилиндр 0
    mov cl, 0x02            ; Сектор 2 (сектор 1 - это загрузчик)
    mov dh, 0x00            ; Головка 0
    
    int 0x13                ; Прерывание BIOS для работы с диском
    
    jc disk_error           ; Если ошибка (CF = 1)
    
    pop dx
    cmp al, dh              ; Проверяем, что загрузили нужное количество секторов
    jne disk_error
    
    popa
    mov ax, 0               ; Успех
    ret

disk_error:
    mov si, msg_disk_error
    call print_string
    mov ax, 1               ; Ошибка
    popa
    ret

load_error:
    mov si, msg_load_error
    call print_string
    jmp $

; Функция вывода строки
; SI - адрес строки (заканчивается нулем)
print_string:
    pusha
    mov ah, 0x0E            ; Функция вывода символа
.loop:
    lodsb                   ; Загружаем байт из [SI] в AL и увеличиваем SI
    cmp al, 0               ; Проверяем конец строки
    je .done
    int 0x10                ; Выводим символ
    jmp .loop
.done:
    popa
    ret

; Переход в защищенный режим
switch_to_pm:
    cli                     ; Отключаем прерывания
    
    ; Загружаем GDT
    lgdt [gdt_descriptor]
    
    ; Включаем A20 линию
    call enable_a20
    
    ; Устанавливаем бит защищенного режима в CR0
    mov eax, cr0
    or eax, 1
    mov cr0, eax
    
    ; Переходим в 32-битный код
    jmp CODE_SEG:pm_start

; Включение A20 линии
enable_a20:
    ; Метод 1: Через клавиатурный контроллер
    call wait_8042
    mov al, 0xAD
    out 0x64, al            ; Отключаем клавиатуру
    
    call wait_8042
    mov al, 0xD0
    out 0x64, al            ; Читаем выходной порт
    
    call wait_8042_data
    in al, 0x60
    push ax
    
    call wait_8042
    mov al, 0xD1
    out 0x64, al            ; Записываем в выходной порт
    
    call wait_8042
    pop ax
    or al, 2                ; Устанавливаем бит A20
    out 0x60, al
    
    call wait_8042
    mov al, 0xAE
    out 0x64, al            ; Включаем клавиатуру
    
    ret

wait_8042:
    in al, 0x64
    test al, 2
    jnz wait_8042
    ret

wait_8042_data:
    in al, 0x64
    test al, 1
    jz wait_8042_data
    ret

[BITS 32]
; Защищенный режим
pm_start:
    ; Устанавливаем сегменты данных
    mov ax, DATA_SEG
    mov ds, ax
    mov ss, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    
    ; Устанавливаем стек
    mov ebp, 0x90000
    mov esp, ebp
    
    ; Передаем управление ядру
    jmp KERNEL_LOAD_SEGMENT:KERNEL_LOAD_OFFSET

; Данные
boot_drive db 0

; Константы
KERNEL_LOAD_SEGMENT equ 0x1000
KERNEL_LOAD_OFFSET equ 0x0000
KERNEL_SECTORS equ 50

; Сегменты GDT
CODE_SEG equ gdt_code - gdt_start
DATA_SEG equ gdt_data - gdt_start

; GDT (Global Descriptor Table)
gdt_start:
    ; Нулевой дескриптор
    gdt_null:
        dd 0x0
        dd 0x0
    
    ; Сегмент кода
    gdt_code:
        dw 0xFFFF           ; Лимит (биты 0-15)
        dw 0x0              ; База (биты 0-15)
        db 0x0              ; База (биты 16-23)
        db 10011010b        ; Флаги доступа
        db 11001111b        ; Флаги и лимит (биты 16-19)
        db 0x0              ; База (биты 24-31)
    
    ; Сегмент данных
    gdt_data:
        dw 0xFFFF           ; Лимит (биты 0-15)
        dw 0x0              ; База (биты 0-15)
        db 0x0              ; База (биты 16-23)
        db 10010010b        ; Флаги доступа
        db 11001111b        ; Флаги и лимит (биты 16-19)
        db 0x0              ; База (биты 24-31)

gdt_end:

gdt_descriptor:
    dw gdt_end - gdt_start - 1  ; Размер GDT
    dd gdt_start                 ; Адрес GDT

; Сообщения
msg_loading db 'Loading kernel...', 13, 10, 0
msg_loaded db 'Kernel loaded!', 13, 10, 0
msg_disk_error db 'Disk read error!', 13, 10, 0
msg_load_error db 'Kernel load failed!', 13, 10, 0

; Заполнение до 510 байт
times 510-($-$$) db 0

; Сигнатура загрузочного сектора
dw 0xAA55


