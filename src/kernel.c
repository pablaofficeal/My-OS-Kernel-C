// kernel.c - ОБНОВЛЕННЫЙ
#include "drivers/screen.h"
#include "drivers/text_output.h"
#include "shell/shell.h"
#include "fs/fat16.h"
#include "fs/disk.h"
#include "drivers/usb/usb_driver.h"
#include "drivers/wifi/wifi.h"

void kernel_main() {
    // First try basic text output
    clear_screen();
    printf("MyOS Kernel Starting...\n");
    
    // Initialize framebuffer graphics
    if (!init_framebuffer()) {
        printf("Framebuffer init failed, using text mode\n");
        // Continue with text mode if framebuffer fails
    } else {
        printf("Framebuffer initialized successfully\n");
    }
    
    // Create desktop
    desktop_t* desktop = create_desktop();
    if (!desktop) {
        printf("Desktop creation failed\n");
        return;
    }
    
    // Initialize disk and filesystem
    disk_init();
    fat16_init();
    
    // Initialize other drivers
    usb_controller_init();
    wifi_init();
    
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