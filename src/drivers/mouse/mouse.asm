; mouse.asm - Драйвер мыши PS/2 на ассемблере
; Поддерживает стандартную PS/2 мышь с 3 кнопками

[BITS 32]

; Порты PS/2 контроллера
PS2_DATA_PORT equ 0x60
PS2_STATUS_PORT equ 0x64
PS2_COMMAND_PORT equ 0x64

; Команды для мыши
MOUSE_CMD_RESET equ 0xFF
MOUSE_CMD_ENABLE equ 0xF4
MOUSE_CMD_DISABLE equ 0xF5
MOUSE_CMD_SET_DEFAULTS equ 0xF6
MOUSE_CMD_SET_SAMPLE_RATE equ 0xF3
MOUSE_CMD_GET_DEVICE_ID equ 0xF2

; Глобальные переменные для состояния мыши
section .bss
mouse_x: resd 1
mouse_y: resd 1
mouse_buttons: resb 1      ; Только байт для кнопок
mouse_initialized: resd 1
mouse_packet: resb 3
mouse_packet_index: resd 1

section .text

; Инициализация мыши
; Возвращает: 0 при успехе, -1 при ошибке
global mouse_init
mouse_init:
    push ebp
    mov ebp, esp
    push eax
    push ebx
    push ecx
    push edx
    
    ; Проверяем, не инициализирована ли уже мышь
    cmp dword [mouse_initialized], 1
    je .already_init
    
    ; Отключаем прерывания мыши
    call mouse_disable
    
    ; Сбрасываем мышь
    mov al, MOUSE_CMD_RESET
    call mouse_write_command
    call mouse_wait_response
    cmp al, 0xFA  ; ACK
    jne .error
    
    ; Ждем завершения сброса
    call mouse_wait_response
    cmp al, 0xAA  ; Self-test passed
    jne .error
    
    ; Устанавливаем параметры по умолчанию
    mov al, MOUSE_CMD_SET_DEFAULTS
    call mouse_write_command
    call mouse_wait_response
    cmp al, 0xFA
    jne .error
    
    ; Устанавливаем частоту опроса (100 Hz)
    mov al, MOUSE_CMD_SET_SAMPLE_RATE
    call mouse_write_command
    call mouse_wait_response
    cmp al, 0xFA
    jne .error
    
    mov al, 100  ; 100 Hz
    call mouse_write_data
    call mouse_wait_response
    cmp al, 0xFA
    jne .error
    
    ; Включаем мышь
    mov al, MOUSE_CMD_ENABLE
    call mouse_write_command
    call mouse_wait_response
    cmp al, 0xFA
    jne .error
    
    ; Устанавливаем флаг инициализации
    mov dword [mouse_initialized], 1
    
    ; Инициализируем позицию мыши в центре экрана
    mov dword [mouse_x], 512  ; SCREEN_WIDTH / 2
    mov dword [mouse_y], 384  ; SCREEN_HEIGHT / 2
    mov dword [mouse_buttons], 0
    mov dword [mouse_packet_index], 0
    
    mov eax, 0  ; Успех
    jmp .done
    
.already_init:
    mov eax, 0
    jmp .done
    
.error:
    mov dword [mouse_initialized], 0
    mov eax, -1
    
.done:
    pop edx
    pop ecx
    pop ebx
    pop eax
    pop ebp
    ret

; Отключение мыши
global mouse_disable
mouse_disable:
    push eax
    mov al, MOUSE_CMD_DISABLE
    call mouse_write_command
    call mouse_wait_response
    pop eax
    ret

; Включение мыши
global mouse_enable
mouse_enable:
    push eax
    mov al, MOUSE_CMD_ENABLE
    call mouse_write_command
    call mouse_wait_response
    pop eax
    ret

; Обработка прерывания мыши (IRQ 12)
; Вызывается из обработчика прерываний
global mouse_irq_handler
mouse_irq_handler:
    pushad
    
    ; Читаем байт данных
    in al, PS2_DATA_PORT
    
    ; Обрабатываем пакет (3 байта для стандартной мыши)
    mov ebx, [mouse_packet_index]
    mov [mouse_packet + ebx], al
    inc ebx
    mov [mouse_packet_index], ebx
    
    ; Если получили все 3 байта, обрабатываем пакет
    cmp ebx, 3
    jne .done
    
    ; Сбрасываем индекс
    mov dword [mouse_packet_index], 0
    
    ; Обрабатываем пакет
    call mouse_process_packet
    
.done:
    ; Отправляем EOI в контроллер прерываний
    mov al, 0x20
    out 0x20, al  ; Master PIC
    out 0xA0, al  ; Slave PIC
    
    popad
    iret

; Обработка пакета данных мыши
mouse_process_packet:
    push eax
    push ebx
    push ecx
    push edx
    
    ; Байт 0: Флаги
    mov al, [mouse_packet + 0]
    mov bl, al
    
    ; Байт 1: Дельта X
    movsx ecx, byte [mouse_packet + 1]
    
    ; Байт 2: Дельта Y
    movsx edx, byte [mouse_packet + 2]
    
    ; Обновляем позицию мыши
    add [mouse_x], ecx
    add [mouse_y], edx
    
    ; Ограничиваем координаты экраном
    cmp dword [mouse_x], 0
    jge .check_x_max
    mov dword [mouse_x], 0
.check_x_max:
    cmp dword [mouse_x], 1023  ; SCREEN_WIDTH - 1
    jle .check_y_min
    mov dword [mouse_x], 1023
.check_y_min:
    cmp dword [mouse_y], 0
    jge .check_y_max
    mov dword [mouse_y], 0
.check_y_max:
    cmp dword [mouse_y], 767  ; SCREEN_HEIGHT - 1
    jle .update_buttons
    mov dword [mouse_y], 767
    
    ; Обновляем состояние кнопок
.update_buttons:
    mov cl, 0
    test bl, 1  ; Левая кнопка
    jz .check_middle
    or cl, 1
.check_middle:
    test bl, 4  ; Средняя кнопка
    jz .check_right
    or cl, 4
.check_right:
    test bl, 2  ; Правая кнопка
    jz .buttons_done
    or cl, 2
.buttons_done:
    mov [mouse_buttons], cl
    
    pop edx
    pop ecx
    pop ebx
    pop eax
    ret

; Получение X координаты мыши
global mouse_get_x
mouse_get_x:
    mov eax, [mouse_x]
    ret

; Получение Y координаты мыши
global mouse_get_y
mouse_get_y:
    mov eax, [mouse_y]
    ret

; Получение состояния кнопок мыши
; Возвращает: бит 0 = левая, бит 1 = правая, бит 2 = средняя
global mouse_get_buttons
mouse_get_buttons:
    movzx eax, byte [mouse_buttons]  ; Загружаем только младший байт
    ret

; Проверка, инициализирована ли мышь
global mouse_is_initialized
mouse_is_initialized:
    mov eax, [mouse_initialized]
    ret

; Вспомогательные функции для работы с PS/2 контроллером

; Запись команды мыши
mouse_write_command:
    push eax
    push edx
    call mouse_wait_input
    mov al, 0xD4  ; Команда для отправки данных мыши
    mov dx, PS2_COMMAND_PORT
    out dx, al
    call mouse_wait_input
    pop edx
    pop eax
    ret

; Запись данных мыши
mouse_write_data:
    push edx
    call mouse_wait_input
    mov dx, PS2_DATA_PORT
    out dx, al
    call mouse_wait_input
    pop edx
    ret

; Ожидание готовности к вводу
mouse_wait_input:
    push eax
    push ecx
    mov ecx, 10000  ; Таймаут
.loop:
    in al, PS2_STATUS_PORT
    test al, 2  ; Бит 1 = входной буфер занят
    jz .done
    dec ecx
    jnz .loop
.done:
    pop ecx
    pop eax
    ret

; Ожидание ответа от мыши
mouse_wait_response:
    push ecx
    mov ecx, 10000  ; Таймаут
.loop:
    in al, PS2_STATUS_PORT
    test al, 1  ; Бит 0 = данные готовы
    jnz .read
    dec ecx
    jnz .loop
    mov al, 0  ; Таймаут
    jmp .done
.read:
    in al, PS2_DATA_PORT
.done:
    pop ecx
    ret

