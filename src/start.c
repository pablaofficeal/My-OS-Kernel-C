// start.c - Multiboot entry point
// No standard headers used, all types defined manually
typedef unsigned int uint32_t;

// Полный правильный Multiboot заголовок для GRUB
#define MULTIBOOT_HEADER_MAGIC 0x1BADB002
// Флаги: информация о памяти, модулях, разделяет память
#define MULTIBOOT_HEADER_FLAGS 0x3
#define MULTIBOOT_CHECKSUM -(MULTIBOOT_HEADER_MAGIC + MULTIBOOT_HEADER_FLAGS)

// Размещаем заголовок в секции .multiboot, GRUB ищет его в первых 8KB
__attribute__((section(".multiboot")))
struct {
    uint32_t magic;
    uint32_t flags;
    uint32_t checksum;
} multiboot_header = {
    MULTIBOOT_HEADER_MAGIC,
    MULTIBOOT_HEADER_FLAGS,
    MULTIBOOT_CHECKSUM
};

// Внешние функции из ассемблера
extern void init_stack();
extern void halt_cpu();
void halt_forever();

// Main kernel function declaration
extern __attribute__((noreturn)) void kernel_main();
extern __attribute__((noreturn)) void halt_forever();
__attribute__((noreturn)) void _start() {
    // Initialize stack - FIRST operation, NOTHING WORKS WITHOUT THIS
    init_stack();
    
    // Run main kernel function
    kernel_main();
    
    // If kernel_main returns (it NEVER should), halt forever
    halt_forever();
}