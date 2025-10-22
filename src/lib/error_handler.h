#ifndef ERROR_HANDLER_H
#define ERROR_HANDLER_H

// Типы ошибок
typedef enum {
    ERROR_INFO,
    ERROR_WARNING,
    ERROR_CRITICAL
} error_type_t;

// Функции обработки ошибок
void handle_error(const char* error_msg, error_type_t type);
int safe_execute(int (*func)(), const char* operation_name);
int get_error_count(void);
void reset_error_count(void);

#endif // ERROR_HANDLER_H