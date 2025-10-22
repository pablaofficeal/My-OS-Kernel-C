#ifndef OPENGL_H
#define OPENGL_H

#include "gpu.h"

// OpenGL constants
#define GL_FALSE                          0
#define GL_TRUE                           1

// Primitive types
#define GL_POINTS                         0x0000
#define GL_LINES                          0x0001
#define GL_LINE_LOOP                      0x0002
#define GL_LINE_STRIP                     0x0003
#define GL_TRIANGLES                      0x0004
#define GL_TRIANGLE_STRIP                 0x0005
#define GL_TRIANGLE_FAN                   0x0006
#define GL_QUADS                          0x0007
#define GL_QUAD_STRIP                     0x0008
#define GL_POLYGON                        0x0009

// Matrix modes
#define GL_MODELVIEW                      0x1700
#define GL_PROJECTION                     0x1701
#define GL_TEXTURE                        0x1702

// Buffer bits
#define GL_COLOR_BUFFER_BIT               0x00004000
#define GL_DEPTH_BUFFER_BIT               0x00000100
#define GL_STENCIL_BUFFER_BIT             0x00000400

// Capabilities
#define GL_DEPTH_TEST                     0x0B71
#define GL_BLEND                          0x0BE2
#define GL_CULL_FACE                      0x0B44

// Texture targets
#define GL_TEXTURE_2D                     0x0DE1
#define GL_TEXTURE_1D                     0x0DE0

// Texture parameters
#define GL_TEXTURE_MIN_FILTER             0x2801
#define GL_TEXTURE_MAG_FILTER             0x2800
#define GL_TEXTURE_WRAP_S                 0x2802
#define GL_TEXTURE_WRAP_T                 0x2803

#define GL_NEAREST                        0x2600
#define GL_LINEAR                         0x2601
#define GL_REPEAT                         0x2901
#define GL_CLAMP                          0x2900

// Shader types
#define GL_VERTEX_SHADER                  0x8B31
#define GL_FRAGMENT_SHADER                0x8B30

// OpenGL context structure
typedef struct opengl_context {
    gpu_context_t* gpu;
    uint32_t current_texture;
    
    // Viewport
    int viewport_x, viewport_y;
    int viewport_width, viewport_height;
    
    // Clear color
    uint32_t clear_color;
    
    // Vertex buffer
    float* vertex_buffer;
    uint32_t vertex_count;
    uint32_t max_vertices;
    
    // Current state
    uint32_t primitive_mode;
    float current_color[4];
    float current_texcoord[2];
} opengl_context_t;

// Context management
opengl_context_t* opengl_create_context(gpu_context_t* gpu);
void opengl_destroy_context(opengl_context_t* ctx);

// Basic OpenGL functions
void opengl_clear(opengl_context_t* ctx, uint32_t mask);
void opengl_viewport(opengl_context_t* ctx, int x, int y, int width, int height);
void opengl_clear_color(opengl_context_t* ctx, float r, float g, float b, float a);

// Primitive drawing
void opengl_begin(opengl_context_t* ctx, uint32_t mode);
void opengl_end(opengl_context_t* ctx);
void opengl_vertex2f(opengl_context_t* ctx, float x, float y);
void opengl_vertex3f(opengl_context_t* ctx, float x, float y, float z);
void opengl_color3f(opengl_context_t* ctx, float r, float g, float b);
void opengl_color4f(opengl_context_t* ctx, float r, float g, float b, float a);

// Matrix operations
void opengl_matrix_mode(opengl_context_t* ctx, uint32_t mode);
void opengl_load_identity(opengl_context_t* ctx);
void opengl_translatef(opengl_context_t* ctx, float x, float y, float z);
void opengl_scalef(opengl_context_t* ctx, float x, float y, float z);
void opengl_rotatef(opengl_context_t* ctx, float angle, float x, float y, float z);

// Texture management
uint32_t opengl_gen_texture(opengl_context_t* ctx);
void opengl_bind_texture(opengl_context_t* ctx, uint32_t texture);
void opengl_tex_image2d(opengl_context_t* ctx, int width, int height, const void* data);
void opengl_tex_parameteri(opengl_context_t* ctx, uint32_t param, int value);

// Shader support
int opengl_supports_shaders(gpu_context_t* gpu);
uint32_t opengl_create_shader(opengl_context_t* ctx, const char* source, int type);
uint32_t opengl_create_program(opengl_context_t* ctx, uint32_t vertex_shader, uint32_t fragment_shader);

// Helper macros for easier usage
#define glClear(mask) opengl_clear(gl_state.current_ctx, mask)
#define glViewport(x, y, w, h) opengl_viewport(gl_state.current_ctx, x, y, w, h)
#define glClearColor(r, g, b, a) opengl_clear_color(gl_state.current_ctx, r, g, b, a)
#define glBegin(mode) opengl_begin(gl_state.current_ctx, mode)
#define glEnd() opengl_end(gl_state.current_ctx)
#define glVertex2f(x, y) opengl_vertex2f(gl_state.current_ctx, x, y)
#define glVertex3f(x, y, z) opengl_vertex3f(gl_state.current_ctx, x, y, z)
#define glColor3f(r, g, b) opengl_color3f(gl_state.current_ctx, r, g, b)
#define glColor4f(r, g, b, a) opengl_color4f(gl_state.current_ctx, r, g, b, a)
#define glMatrixMode(mode) opengl_matrix_mode(gl_state.current_ctx, mode)
#define glLoadIdentity() opengl_load_identity(gl_state.current_ctx)
#define glTranslatef(x, y, z) opengl_translatef(gl_state.current_ctx, x, y, z)
#define glScalef(x, y, z) opengl_scalef(gl_state.current_ctx, x, y, z)
#define glRotatef(angle, x, y, z) opengl_rotatef(gl_state.current_ctx, angle, x, y, z)
#define glGenTextures(count, textures) *(textures) = opengl_gen_texture(gl_state.current_ctx)
#define glBindTexture(target, texture) opengl_bind_texture(gl_state.current_ctx, texture)
#define glTexImage2D(target, level, internalformat, width, height, border, format, type, pixels) \
    opengl_tex_image2d(gl_state.current_ctx, width, height, pixels)
#define glTexParameteri(target, pname, param) opengl_tex_parameteri(gl_state.current_ctx, pname, param)

// Global OpenGL state for macro access
extern struct {
    opengl_context_t* current_ctx;
    uint32_t matrix_mode;
    float modelview_matrix[16];
    float projection_matrix[16];
    float texture_matrix[16];
    float* current_matrix;
    uint32_t active_texture;
    uint32_t blend_enabled;
    uint32_t depth_test_enabled;
    uint32_t cull_face_enabled;
} gl_state;

#endif // OPENGL_H