#include "error_handler.h"
#include "../drivers/text_output.h"

// Глобальный счетчик ошибок
static int error_count = 0;

// Упрощенная функция для обработки ошибок
void handle_error(const char* error_msg, error_type_t type) {
    error_count++;
    
    // Используем printf для вывода, так как она определена в text_output.h
    printf("[ERROR %d] ", error_count);
    
    switch(type) {
        case ERROR_CRITICAL:
            printf("CRITICAL: ");
            break;
        case ERROR_WARNING:
            printf("WARNING: ");
            break;
        case ERROR_INFO:
            printf("INFO: ");
            break;
        default:
            printf("UNKNOWN: ");
            break;
    }
    
    printf("%s\n", error_msg);
    
    // Для критических ошибок можем добавить дополнительные действия
    if (type == ERROR_CRITICAL) {
        printf("System will attempt to continue...\n");
    }
}

// Функция для безопасного выполнения операций
int safe_execute(int (*func)(), const char* operation_name) {
    printf("Executing: %s...\n", operation_name);
    
    int result = func();
    
    if (result != 0) {
        char error_msg[256];
        // Используем безопасную функцию форматирования вместо sprintf
        const char* failed_str = " failed with code ";
        const char* continue_str = ", continuing...";
        
        // Копируем строки вручную для безопасности
        int pos = 0;
        while (operation_name[pos] && pos < 100) {
            error_msg[pos] = operation_name[pos];
            pos++;
        }
        
        int i = 0;
        while (failed_str[i] && pos < 120) {
            error_msg[pos] = failed_str[i];
            pos++;
            i++;
        }
        
        // Добавляем код ошибки (просто цифру)
        error_msg[pos++] = '0' + (result % 10);
        
        i = 0;
        while (continue_str[i] && pos < 150) {
            error_msg[pos] = continue_str[i];
            pos++;
            i++;
        }
        error_msg[pos] = '\0';
        
        handle_error(error_msg, ERROR_WARNING);
        return result;
    }
    
    printf("SUCCESS: %s completed\n", operation_name);
    return 0;
}

// Получить количество ошибок
int get_error_count() {
    return error_count;
}

// Сбросить счетчик ошибок
void reset_error_count() {
    error_count = 0;
}