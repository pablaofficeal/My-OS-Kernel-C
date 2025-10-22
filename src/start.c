// Multiboot заголовок - ДОЛЖЕН БЫТЬ ПЕРВЫМ!
#define MULTIBOOT_HEADER_MAGIC 0x1BADB002
#define MULTIBOOT_HEADER_FLAGS 0x00000003  // ALIGN | MEM_INFO (без VIDEO_MODE для совместимости)
#define MULTIBOOT_HEADER_CHECKSUM -(MULTIBOOT_HEADER_MAGIC + MULTIBOOT_HEADER_FLAGS)

// Убеждаемся, что заголовок находится в самом начале
__attribute__((section(".multiboot")))
__attribute__((aligned(4)))
__attribute__((used))  // Предотвращает удаление компилятором
__attribute__((externally_visible))  // Делаем видимым для линковщика

// Multiboot2 заголовок для UEFI
#define MULTIBOOT2_HEADER_MAGIC 0xe85250d6
#define MULTIBOOT2_HEADER_ARCHITECTURE 0  // i386
#define MULTIBOOT2_HEADER_LENGTH 32  // Длина с консольным тегом
#define MULTIBOOT2_HEADER_CHECKSUM -(MULTIBOOT2_HEADER_MAGIC + MULTIBOOT2_HEADER_ARCHITECTURE + MULTIBOOT2_HEADER_LENGTH)

// Multiboot заголовок - byte-packed
__attribute__((section(".multiboot")))
__attribute__((aligned(4)))
__attribute__((used))
const unsigned char multiboot_header[] = {
    0x02, 0xb0, 0xad, 0x1b, // magic (0x1BADB002)
    0x07, 0x00, 0x00, 0x00, // flags (0x00000007)
    0xf7, 0x4f, 0x52, 0xe4, // checksum (-(magic+flags))
    0x00, 0x00, 0x10, 0x00, // header_addr (0x00100000)
    0x00, 0x00, 0x10, 0x00, // load_addr (0x00100000)
    0x00, 0x00, 0x00, 0x00, // load_end_addr (0)
    0x00, 0x00, 0x00, 0x00, // bss_end_addr (0)
    0x00, 0x00, 0x10, 0x00, // entry_addr (0x00100000)
    0x00, 0x00, 0x00, 0x00, // mode_type (0)
    0x00, 0x04, 0x00, 0x00, // width (1024)
    0x00, 0x03, 0x00, 0x00, // height (768)
    0x20, 0x00, 0x00, 0x00  // depth (32)
};

// Multiboot2 заголовок для UEFI - byte-packed
__attribute__((section(".multiboot2")))
__attribute__((aligned(8)))
__attribute__((used))
__attribute__((externally_visible))
const unsigned char multiboot2_header[] = {
    // Header
    0xd6, 0x50, 0x52, 0xe8, // magic (0xe85250d6)
    0x00, 0x00, 0x00, 0x00, // architecture (0)
    0x20, 0x00, 0x00, 0x00, // header_length (32)
    0x2a, 0xaf, 0xad, 0x17, // checksum (-(magic+arch+length))
    // Console tag (type 2, size 12, flags 3)
    0x02, 0x00, 0x00, 0x00, // tag_type = 2
    0x0c, 0x00, 0x00, 0x00, // tag_size = 12
    0x03, 0x00, 0x00, 0x00, // flags = 3
    // End tag (type 0, size 8)
    0x00, 0x00, 0x00, 0x00, // tag_type = 0
    0x08, 0x00, 0x00, 0x00  // tag_size = 8
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