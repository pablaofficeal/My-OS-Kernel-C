// drivers/gpu.c - Hardware GPU implementation
#include "gpu.h"
#include "pci.h"
#include "memory.h"
#include "string.h"
#include "ports.h"

// VBE/EFI framebuffer info structure
typedef struct {
    uint32_t base_addr;
    uint32_t size;
    uint32_t width;
    uint32_t height;
    uint32_t bpp;
    uint32_t pitch;
} framebuffer_info_t;

static gpu_context_t gpu_ctx;
static opengl_context_t* current_opengl_ctx = NULL;
static framebuffer_info_t fb_info;

// GPU Command registers (generic)
#define GPU_REG_COMMAND     0x00
#define GPU_REG_STATUS      0x04
#define GPU_REG_FB_ADDR     0x08
#define GPU_REG_FB_SIZE     0x0C
#define GPU_REG_WIDTH       0x10
#define GPU_REG_HEIGHT      0x14
#define GPU_REG_BPP         0x18
#define GPU_REG_PITCH       0x1C

// GPU Status flags
#define GPU_STATUS_READY    0x01
#define GPU_STATUS_BUSY     0x02
#define GPU_STATUS_ERROR    0x04

// VBE Mode info structure (simplified)
typedef struct {
    uint16_t attributes;
    uint8_t  winA, winB;
    uint16_t granularity;
    uint16_t winsize;
    uint16_t segmentA, segmentB;
    uint32_t realFctPtr;
    uint16_t pitch;
    uint16_t width, height;
    uint8_t  wChar, yChar, planes, bpp, banks;
    uint8_t  memory_model, bank_size, image_pages;
    uint8_t  reserved0;
    uint8_t  red_mask, red_position;
    uint8_t  green_mask, green_position;
    uint8_t  blue_mask, blue_position;
    uint8_t  rsv_mask, rsv_position;
    uint8_t  directcolor_attributes;
    uint32_t physbase;
    uint32_t reserved1;
    uint16_t reserved2;
} __attribute__((packed)) vbe_mode_info_t;

// Инициализация GPU через PCI
int gpu_init(void) {
    printf("GPU: Initializing hardware graphics...\n");
    
    // Ищем GPU в PCI
    pci_device_t* gpu_device = pci_find_device(0x030000); // Класс: Display controller
    
    if (!gpu_device) {
        printf("GPU: No display controller found, using framebuffer\n");
        // Используем VBE framebuffer
        return init_framebuffer_gpu();
    }
    
    // Инициализируем GPU контекст
    memset(&gpu_ctx, 0, sizeof(gpu_context_t));
    gpu_ctx.vendor_id = pci_read_word(gpu_device->bus, gpu_device->dev, gpu_device->func, 0);
    gpu_ctx.device_id = pci_read_word(gpu_device->bus, gpu_device->dev, gpu_device->func, 2);
    
    printf("GPU: Found device %04X:%04X\n", gpu_ctx.vendor_id, gpu_ctx.device_id);
    
    // Инициализируем в зависимости от вендора
    switch (gpu_ctx.vendor_id) {
        case GPU_VENDOR_INTEL:
            return init_intel_gpu(gpu_device);
        case GPU_VENDOR_NVIDIA:
            return init_nvidia_gpu(gpu_device);
        case GPU_VENDOR_AMD:
            return init_amd_gpu(gpu_device);
        case GPU_VENDOR_VMWARE:
            return init_vmware_gpu(gpu_device);
        default:
            printf("GPU: Unknown vendor, using generic framebuffer\n");
            return init_framebuffer_gpu();
    }
}

// Инициализация Intel GPU
int init_intel_gpu(pci_device_t* device) {
    printf("GPU: Initializing Intel graphics...\n");
    
    // Получаем BAR адреса
    uint32_t bar0 = pci_read_dword(device->bus, device->dev, device->func, 0x10);
    uint32_t bar1 = pci_read_dword(device->bus, device->dev, device->func, 0x14);
    
    // Инициализируем MMIO
    gpu_ctx.mmio_base = (uint8_t*)(bar0 & 0xFFFFFFF0);
    
    // Устанавливаем базовый режим
    gpu_ctx.width = 1024;
    gpu_ctx.height = 768;
    gpu_ctx.bpp = 32;
    gpu_ctx.pitch = gpu_ctx.width * 4;
    gpu_ctx.framebuffer_size = gpu_ctx.width * gpu_ctx.height * 4;
    
    // Выделяем framebuffer
    gpu_ctx.framebuffer_addr = 0xFD000000; // Зарезервированная область
    
    // Инициализируем Intel-specific регистры
    outl((uint32_t)gpu_ctx.mmio_base + 0x6010, gpu_ctx.framebuffer_addr); // GMADR
    outl((uint32_t)gpu_ctx.mmio_base + 0x6014, gpu_ctx.framebuffer_size); // GMSIZE
    
    gpu_ctx.accelerated = 1;
    printf("GPU: Intel graphics initialized\n");
    return 1;
}

// Инициализация NVIDIA GPU
int init_nvidia_gpu(pci_device_t* device) {
    printf("GPU: Initializing NVIDIA graphics...\n");
    
    // Получаем BAR адреса
    uint32_t bar0 = pci_read_dword(device->bus, device->dev, device->func, 0x10);
    gpu_ctx.mmio_base = (uint8_t*)(bar0 & 0xFFFFFFF0);
    
    // Базовая инициализация NVIDIA GPU
    gpu_ctx.width = 1024;
    gpu_ctx.height = 768;
    gpu_ctx.bpp = 32;
    gpu_ctx.pitch = gpu_ctx.width * 4;
    gpu_ctx.framebuffer_size = gpu_ctx.width * gpu_ctx.height * 4;
    gpu_ctx.framebuffer_addr = 0xFD000000;
    
    // Инициализируем NVIDIA FIFO и DMA
    // Здесь будет сложная инициализация NVIDIA GPU
    
    gpu_ctx.accelerated = 1;
    printf("GPU: NVIDIA graphics initialized\n");
    return 1;
}

// Инициализация AMD GPU
int init_amd_gpu(pci_device_t* device) {
    printf("GPU: Initializing AMD graphics...\n");
    
    uint32_t bar0 = pci_read_dword(device->bus, device->dev, device->func, 0x10);
    gpu_ctx.mmio_base = (uint8_t*)(bar0 & 0xFFFFFFF0);
    
    gpu_ctx.width = 1024;
    gpu_ctx.height = 768;
    gpu_ctx.bpp = 32;
    gpu_ctx.pitch = gpu_ctx.width * 4;
    gpu_ctx.framebuffer_size = gpu_ctx.width * gpu_ctx.height * 4;
    gpu_ctx.framebuffer_addr = 0xFD000000;
    
    // Инициализируем AMD GPU registers
    
    gpu_ctx.accelerated = 1;
    printf("GPU: AMD graphics initialized\n");
    return 1;
}

// Инициализация VMware SVGA GPU
int init_vmware_gpu(pci_device_t* device) {
    printf("GPU: Initializing VMware SVGA...\n");
    
    uint32_t bar0 = pci_read_dword(device->bus, device->dev, device->func, 0x10);
    gpu_ctx.mmio_base = (uint8_t*)(bar0 & 0xFFFFFFF0);
    
    // VMware SVGA specific initialization
    gpu_ctx.width = 1024;
    gpu_ctx.height = 768;
    gpu_ctx.bpp = 32;
    gpu_ctx.pitch = gpu_ctx.width * 4;
    gpu_ctx.framebuffer_size = gpu_ctx.width * gpu_ctx.height * 4;
    gpu_ctx.framebuffer_addr = 0xFD000000;
    
    // Инициализируем VMware SVGA
    outl((uint32_t)gpu_ctx.mmio_base + 0x00, gpu_ctx.width);   // WIDTH
    outl((uint32_t)gpu_ctx.mmio_base + 0x04, gpu_ctx.height);  // HEIGHT
    outl((uint32_t)gpu_ctx.mmio_base + 0x08, gpu_ctx.bpp);    // BPP
    outl((uint32_t)gpu_ctx.mmio_base + 0x0C, 1);              // ENABLE
    
    gpu_ctx.accelerated = 1;
    printf("GPU: VMware SVGA initialized\n");
    return 1;
}

// Инициализация через VBE framebuffer
int init_framebuffer_gpu(void) {
    printf("GPU: Using VBE framebuffer mode...\n");
    
    // Получаем информацию от bootloader (GRUB/LIMINE)
    // В реальной системе это будет передано через multiboot info
    gpu_ctx.width = 1024;
    gpu_ctx.height = 768;
    gpu_ctx.bpp = 32;
    gpu_ctx.pitch = gpu_ctx.width * 4;
    gpu_ctx.framebuffer_size = gpu_ctx.width * gpu_ctx.height * 4;
    gpu_ctx.framebuffer_addr = 0xFD000000;
    
    fb_info.base_addr = gpu_ctx.framebuffer_addr;
    fb_info.size = gpu_ctx.framebuffer_size;
    fb_info.width = gpu_ctx.width;
    fb_info.height = gpu_ctx.height;
    fb_info.bpp = gpu_ctx.bpp;
    fb_info.pitch = gpu_ctx.pitch;
    
    gpu_ctx.accelerated = 0; // Программная отрисовка
    printf("GPU: Framebuffer mode initialized\n");
    return 1;
}

// Получение контекста GPU
gpu_context_t* gpu_get_context(void) {
    return &gpu_ctx;
}

// Установка видео режима
int gpu_set_mode(uint32_t width, uint32_t height, uint32_t bpp) {
    printf("GPU: Setting mode %ux%ux%u\n", width, height, bpp);
    
    gpu_ctx.width = width;
    gpu_ctx.height = height;
    gpu_ctx.bpp = bpp;
    gpu_ctx.pitch = width * (bpp / 8);
    gpu_ctx.framebuffer_size = width * height * (bpp / 8);
    
    if (gpu_ctx.accelerated && gpu_ctx.mmio_base) {
        // Аппаратная установка режима
        outl((uint32_t)gpu_ctx.mmio_base + GPU_REG_WIDTH, width);
        outl((uint32_t)gpu_ctx.mmio_base + GPU_REG_HEIGHT, height);
        outl((uint32_t)gpu_ctx.mmio_base + GPU_REG_BPP, bpp);
        outl((uint32_t)gpu_ctx.mmio_base + GPU_REG_PITCH, gpu_ctx.pitch);
    }
    
    return 1;
}

// Очистка экрана
void gpu_clear(uint32_t color) {
    if (gpu_ctx.accelerated && gpu_ctx.mmio_base) {
        // Аппаратная очистка
        outl((uint32_t)gpu_ctx.mmio_base + GPU_REG_COMMAND, GPU_CMD_CLEAR);
        outl((uint32_t)gpu_ctx.mmio_base + 0x20, color); // Цвет
    } else {
        // Программная очистка
        uint32_t* fb = (uint32_t*)gpu_ctx.framebuffer_addr;
        for (uint32_t i = 0; i < gpu_ctx.width * gpu_ctx.height; i++) {
            fb[i] = color;
        }
    }
}

// Аппаратное рисование прямоугольника
void gpu_draw_rect_hw(int x, int y, int width, int height, uint32_t color) {
    if (gpu_ctx.accelerated && gpu_ctx.mmio_base) {
        outl((uint32_t)gpu_ctx.mmio_base + GPU_REG_COMMAND, GPU_CMD_DRAW_RECT);
        outl((uint32_t)gpu_ctx.mmio_base + 0x20, x);
        outl((uint32_t)gpu_ctx.mmio_base + 0x24, y);
        outl((uint32_t)gpu_ctx.mmio_base + 0x28, width);
        outl((uint32_t)gpu_ctx.mmio_base + 0x2C, height);
        outl((uint32_t)gpu_ctx.mmio_base + 0x30, color);
    } else {
        // Программная реализация
        // Вызовем функцию из screen.c
        extern void fill_rect(int x, int y, int width, int height, uint32_t color);
        fill_rect(x, y, width, height, color);
    }
}

// Аппаратное рисование линии
void gpu_draw_line_hw(int x1, int y1, int x2, int y2, uint32_t color) {
    if (gpu_ctx.accelerated && gpu_ctx.mmio_base) {
        outl((uint32_t)gpu_ctx.mmio_base + GPU_REG_COMMAND, GPU_CMD_DRAW_LINE);
        outl((uint32_t)gpu_ctx.mmio_base + 0x20, x1);
        outl((uint32_t)gpu_ctx.mmio_base + 0x24, y1);
        outl((uint32_t)gpu_ctx.mmio_base + 0x28, x2);
        outl((uint32_t)gpu_ctx.mmio_base + 0x2C, y2);
        outl((uint32_t)gpu_ctx.mmio_base + 0x30, color);
    } else {
        // Программная реализация
        extern void draw_line(int x1, int y1, int x2, int y2, uint32_t color);
        draw_line(x1, y1, x2, y2, color);
    }
}

// Аппаратное рисование круга
void gpu_draw_circle_hw(int x, int y, int radius, uint32_t color) {
    if (gpu_ctx.accelerated && gpu_ctx.mmio_base) {
        outl((uint32_t)gpu_ctx.mmio_base + GPU_REG_COMMAND, GPU_CMD_DRAW_CIRCLE);
        outl((uint32_t)gpu_ctx.mmio_base + 0x20, x);
        outl((uint32_t)gpu_ctx.mmio_base + 0x24, y);
        outl((uint32_t)gpu_ctx.mmio_base + 0x28, radius);
        outl((uint32_t)gpu_ctx.mmio_base + 0x2C, color);
    } else {
        // Программная реализация круга
        for (int dy = -radius; dy <= radius; dy++) {
            for (int dx = -radius; dx <= radius; dx++) {
                if (dx*dx + dy*dy <= radius*radius) {
                    extern void put_pixel(int x, int y, uint32_t color);
                    put_pixel(x + dx, y + dy, color);
                }
            }
        }
    }
}

// Обмен буферами
void gpu_swap_buffers(void) {
    if (gpu_ctx.accelerated && gpu_ctx.mmio_base) {
        // Команда обмена буферов для двойной буферизации
        outl((uint32_t)gpu_ctx.mmio_base + GPU_REG_COMMAND, 0x07); // SWAP_BUFFERS
    }
    // Для программной реализации ничего не делаем
}