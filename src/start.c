// Multiboot заголовок - ДОЛЖЕН БЫТЬ ПЕРВЫМ!
#define MULTIBOOT_HEADER_MAGIC 0x1BADB002
#define MULTIBOOT_HEADER_FLAGS 0x00000003  // ALIGN | MEM_INFO
#define MULTIBOOT_HEADER_CHECKSUM -(MULTIBOOT_HEADER_MAGIC + MULTIBOOT_HEADER_FLAGS)

// Multiboot заголовок для ELF файлов
__attribute__((section(".multiboot")))
__attribute__((aligned(4)))
const struct {
    unsigned int magic;
    unsigned int flags;
    unsigned int checksum;
    unsigned int header_addr;
    unsigned int load_addr;
    unsigned int load_end_addr;
    unsigned int bss_end_addr;
    unsigned int entry_addr;
} multiboot_header = {
    MULTIBOOT_HEADER_MAGIC,
    MULTIBOOT_HEADER_FLAGS,
    MULTIBOOT_HEADER_CHECKSUM,
    0x100000,    // header_addr
    0x100000,    // load_addr
    0,           // load_end_addr
    0,           // bss_end_addr
    0x100000     // entry_addr
};

// Точка входа для ядра ОС

extern void kernel_main(void);

// Функция _start - точка входа, требуемая линковщиком
void _start(void) {
    // Инициализация стека и сегментов
    asm volatile(
        "cli\n"                    // Отключаем прерывания
        "cld\n"                    // Очищаем флаг направления
        "movl $0x100000, %esp\n"  // Устанавливаем стек
        "movl %esp, %ebp\n"        // Устанавливаем базовый указатель стека
        "pushl $0\n"               // Выравнивание стека
        "pushl %ebx\n"              // Сохраняем Multiboot magic
        "pushl %eax\n"              // Сохраняем Multiboot info
    );
    
    // Вызываем основную функцию ядра
    kernel_main();
    
    // Бесконечный цикл после завершения kernel_main
    while(1) {
        // Остановка процессора
        asm volatile("hlt");
    }
}