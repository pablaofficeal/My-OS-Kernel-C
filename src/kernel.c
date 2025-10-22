// kernel.c - ОБНОВЛЕННЫЙ
#include "drivers/screen.h"
#include "drivers/text_output.h"
#include "shell/shell.h"
#include "fs/fat16.h"
#include "fs/disk.h"
#include "drivers/usb/usb_driver.h"
#include "drivers/wifi/wifi.h"
#include "lib/error_handler.h"

void kernel_main() {
    // Initialize error handling
    reset_error_count();
    
    // First try basic text output
    clear_screen();
    printf("MyOS Kernel Starting...\n");
    printf("Built-in error handling enabled\n");
    printf("Error counter initialized\n");
    
    // Initialize framebuffer graphics with error handling
    printf("Initializing framebuffer...\n");
    if (!init_framebuffer()) {
        handle_error("Framebuffer initialization failed, continuing in text mode", ERROR_WARNING);
        printf("System will continue with limited graphics capabilities\n");
        // Continue with text mode if framebuffer fails - don't crash
    } else {
        printf("SUCCESS: Framebuffer initialized successfully\n");
    }
    
    // Create desktop with error handling
    printf("Creating desktop environment...\n");
    desktop_t* desktop = create_desktop();
    if (!desktop) {
        handle_error("Desktop creation failed, falling back to text interface", ERROR_WARNING);
        printf("Continuing with basic text interface...\n");
        // Continue with text mode instead of crashing
        printf("System ready in text mode\n");
        init_shell();
        return;
    }
    printf("SUCCESS: Desktop created successfully\n");
    
    // Initialize disk and filesystem with error handling
    printf("Initializing disk subsystem...\n");
    if (safe_execute(disk_init, "Disk initialization") != 0) {
        handle_error("Disk operations will be unavailable", ERROR_INFO);
    } else {
        if (safe_execute(fat16_init, "FAT16 filesystem initialization") != 0) {
            handle_error("Filesystem operations will be limited", ERROR_INFO);
        }
    }
    
    // Initialize other drivers with error handling
    printf("Initializing USB controller...\n");
    if (safe_execute(usb_controller_init, "USB controller initialization") != 0) {
        handle_error("USB devices will not be available", ERROR_INFO);
    }
    
    printf("Initializing WiFi...\n");
    if (safe_execute(wifi_init, "WiFi initialization") != 0) {
        handle_error("Network features will not be available", ERROR_INFO);
    }
    
    // Create demo windows with error handling
    printf("Creating demo windows...\n");
    window_t* welcome_window = create_window(100, 100, 400, 300, "Welcome to MyOS", WINDOW_FLAG_DECORATED);
    if (welcome_window) {
        // Add some content to the window
        draw_string(20, 50, "Welcome to MyOS!", COLOR_WHITE);
        draw_string(20, 70, "This is a framebuffer-based GUI.", COLOR_WHITE);
        draw_string(20, 90, "Click windows to interact.", COLOR_WHITE);
        window_paint(welcome_window);
        printf("Welcome window created successfully\n");
    } else {
        handle_error("Could not create welcome window", ERROR_WARNING);
    }
    
    window_t* terminal_window = create_window(150, 150, 350, 250, "Terminal", WINDOW_FLAG_DECORATED);
    if (terminal_window) {
        draw_string(20, 50, "Terminal Window", COLOR_WHITE);
        draw_string(20, 70, "Ready for input...", COLOR_WHITE);
        window_paint(terminal_window);
        printf("Terminal window created successfully\n");
    } else {
        handle_error("Could not create terminal window", ERROR_WARNING);
    }
    
    // Draw initial desktop
    printf("Rendering desktop...\n");
    desktop_paint(desktop);
    printf("Desktop rendered successfully\n");
    
    // Show final status
    printf("\n=== System Initialization Complete ===\n");
    printf("Total errors encountered: %d\n", get_error_count());
    if (get_error_count() > 0) {
        printf("System running with some limitations\n");
    } else {
        printf("All systems operational\n");
    }
    printf("=====================================\n");
    
    // Start shell (now in graphics mode)
    init_shell();
}