// drivers/gpu.h - Hardware GPU interface
#ifndef GPU_H
#define GPU_H

#include <stdint.h>

// GPU Device IDs
#define GPU_VENDOR_INTEL    0x8086
#define GPU_VENDOR_NVIDIA   0x10DE
#define GPU_VENDOR_AMD      0x1002
#define GPU_VENDOR_VMWARE   0x15AD

// GPU Command types
#define GPU_CMD_CLEAR       0x01
#define GPU_CMD_DRAW_RECT   0x02
#define GPU_CMD_DRAW_LINE   0x03
#define GPU_CMD_DRAW_CIRCLE 0x04
#define GPU_CMD_BLIT        0x05
#define GPU_CMD_SET_MODE    0x06

// GPU Memory types
#define GPU_MEMORY_FB       0x01  // Framebuffer
#define GPU_MEMORY_TEX      0x02  // Texture memory
#define GPU_MEMORY_VB       0x03  // Vertex buffer

// OpenGL-like constants
#define GL_COLOR_BUFFER_BIT   0x00004000
#define GL_TRIANGLES          0x0004
#define GL_QUADS              0x0007
#define GL_TEXTURE_2D         0x0DE1
#define GL_RGBA               0x1908
#define GL_UNSIGNED_BYTE      0x1401
#define GL_LINEAR             0x2601
#define GL_NEAREST            0x2600

// GPU Context structure
typedef struct {
    uint32_t vendor_id;
    uint32_t device_id;
    uint32_t framebuffer_addr;
    uint32_t framebuffer_size;
    uint32_t width;
    uint32_t height;
    uint32_t bpp;
    uint32_t pitch;
    uint8_t* mmio_base;
    uint32_t irq_num;
    int accelerated;
} gpu_context_t;

// OpenGL Context for hardware acceleration
typedef struct {
    gpu_context_t* gpu;
    uint32_t current_texture;
    uint32_t viewport_x;
    uint32_t viewport_y;
    uint32_t viewport_width;
    uint32_t viewport_height;
    uint32_t clear_color;
    float* vertex_buffer;
    uint32_t vertex_count;
    uint32_t max_vertices;
} opengl_context_t;

// GPU initialization and control
int gpu_init(void);
gpu_context_t* gpu_get_context(void);
int gpu_set_mode(uint32_t width, uint32_t height, uint32_t bpp);
void gpu_clear(uint32_t color);
void gpu_swap_buffers(void);

// Hardware-accelerated drawing
void gpu_draw_rect_hw(int x, int y, int width, int height, uint32_t color);
void gpu_draw_line_hw(int x1, int y1, int x2, int y2, uint32_t color);
void gpu_draw_circle_hw(int x, int y, int radius, uint32_t color);
void gpu_fill_rect_hw(int x, int y, int width, int height, uint32_t color);
void gpu_blit(uint32_t src_addr, int src_x, int src_y, int dst_x, int dst_y, int width, int height);

// OpenGL-like API for 3D acceleration
opengl_context_t* opengl_create_context(gpu_context_t* gpu);
void opengl_destroy_context(opengl_context_t* ctx);
void opengl_clear(opengl_context_t* ctx, uint32_t mask);
void opengl_viewport(opengl_context_t* ctx, int x, int y, int width, int height);
void opengl_clear_color(opengl_context_t* ctx, float r, float g, float b, float a);
void opengl_begin(opengl_context_t* ctx, uint32_t mode);
void opengl_end(opengl_context_t* ctx);
void opengl_vertex2f(opengl_context_t* ctx, float x, float y);
void opengl_vertex3f(opengl_context_t* ctx, float x, float y, float z);
void opengl_color3f(opengl_context_t* ctx, float r, float g, float b);
void opengl_color4f(opengl_context_t* ctx, float r, float g, float b, float a);

// Texture management
uint32_t opengl_gen_texture(opengl_context_t* ctx);
void opengl_bind_texture(opengl_context_t* ctx, uint32_t texture);
void opengl_tex_image2d(opengl_context_t* ctx, int width, int height, const void* data);
void opengl_tex_parameteri(opengl_context_t* ctx, uint32_t param, int value);

// Matrix operations (для 3D трансформаций)
void opengl_matrix_mode(opengl_context_t* ctx, uint32_t mode);
void opengl_load_identity(opengl_context_t* ctx);
void opengl_translatef(opengl_context_t* ctx, float x, float y, float z);
void opengl_scalef(opengl_context_t* ctx, float x, float y, float z);
void opengl_rotatef(opengl_context_t* ctx, float angle, float x, float y, float z);

// Shader support (если GPU поддерживает)
int opengl_supports_shaders(gpu_context_t* gpu);
uint32_t opengl_create_shader(opengl_context_t* ctx, const char* source, int type);
uint32_t opengl_create_program(opengl_context_t* ctx, uint32_t vertex_shader, uint32_t fragment_shader);

#endif