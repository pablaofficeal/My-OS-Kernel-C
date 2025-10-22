// kernel.c - ОБНОВЛЕННЫЙ
#define MULTIBOOT_HEADER_MAGIC 0x1BADB002
#define MULTIBOOT_HEADER_FLAGS 0x00000003
#define MULTIBOOT_HEADER_CHECKSUM -(MULTIBOOT_HEADER_MAGIC + MULTIBOOT_HEADER_FLAGS)

__attribute__((section(".multiboot")))
__attribute__((aligned(4)))
const unsigned int multiboot_header[] = {
    MULTIBOOT_HEADER_MAGIC,
    MULTIBOOT_HEADER_FLAGS,
    MULTIBOOT_HEADER_CHECKSUM
};

#include "drivers/screen.h"
#include "shell/shell.h"
#include "fs/fat16.h"
#include "fs/disk.h"
#include "drivers/usb/usb_driver.h"
#include "drivers/wifi/wifi.h"
#include "drivers/usb/usb_driver.h"
#include "drivers/wifi/wifi.h"

void kernel_main() {
    // Initialize framebuffer graphics
    if (!init_framebuffer()) {
        // If framebuffer init fails, we can't proceed
        return;
    }
    
    // Create desktop
    desktop_t* desktop = create_desktop();
    if (!desktop) {
        return;
    }
    
    // Initialize disk and filesystem
    init_disk();
    init_fat16();
    
    // Initialize other drivers
    init_usb_driver();
    init_wifi();
    
    // Create some demo windows
    window_t* welcome_window = create_window(100, 100, 400, 300, "Welcome to MyOS", WINDOW_FLAG_DECORATED);
    if (welcome_window) {
        // Add some content to the window
        draw_string(20, 50, "Welcome to MyOS!", COLOR_WHITE);
        draw_string(20, 70, "This is a framebuffer-based GUI.", COLOR_WHITE);
        draw_string(20, 90, "Click windows to interact.", COLOR_WHITE);
        window_paint(welcome_window);
    }
    
    window_t* terminal_window = create_window(150, 150, 350, 250, "Terminal", WINDOW_FLAG_DECORATED);
    if (terminal_window) {
        draw_string(20, 50, "Terminal Window", COLOR_WHITE);
        draw_string(20, 70, "Ready for input...", COLOR_WHITE);
        window_paint(terminal_window);
    }
    
    // Draw initial desktop
    desktop_paint(desktop);
    
    // Start shell (now in graphics mode)
    init_shell();
}