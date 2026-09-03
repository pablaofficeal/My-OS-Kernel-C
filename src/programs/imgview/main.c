#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>
#include "../../libgui/include/puregui.h"
#include "../../libgui/include/pguiw.h"
#include "../../libfs/include/purefs.h"
#include "../../libc/include/purec.h"

#define MAX_IMAGE_WIDTH  2048
#define MAX_IMAGE_HEIGHT 2048
#define MAX_IMAGE_FILE_SIZE (8 * 1024 * 1024)

struct image_data {
    uint32_t width;
    uint32_t height;
    uint32_t bpp;
    bool loaded;
    char path[128];
    uint32_t *pixels; // Allocated in 32-bit 0x00RRGGBB format
};

static struct image_data g_image;

static uint16_t read_u16_le(const uint8_t *p) {
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static uint32_t read_u32_le(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static int32_t read_i32_le(const uint8_t *p) {
    return (int32_t)read_u32_le(p);
}

static bool parse_bmp(const uint8_t *data, uint32_t size, struct image_data *img) {
    if (size < 54) return false;
    uint16_t magic = read_u16_le(data);
    if (magic != 0x4D42) return false; // 'BM'

    uint32_t data_offset = read_u32_le(data + 10);
    int32_t width = read_i32_le(data + 18);
    int32_t height = read_i32_le(data + 22);
    uint16_t bpp = read_u16_le(data + 28);
    uint32_t compression = read_u32_le(data + 30);

    if (width <= 0 || width > MAX_IMAGE_WIDTH) return false;
    bool top_down = false;
    if (height < 0) {
        top_down = true;
        height = -height;
    }
    if (height <= 0 || height > MAX_IMAGE_HEIGHT) return false;
    if (bpp != 24 && bpp != 32 && bpp != 8) return false;
    if (compression != 0 && compression != 3) return false; // BI_RGB or BI_BITFIELDS
    if (data_offset >= size) return false;

    uint32_t total_pixels = (uint32_t)width * (uint32_t)height;
    img->pixels = (uint32_t *)pc_heap_grow(total_pixels * sizeof(uint32_t));
    if (!img->pixels) return false;

    img->width = (uint32_t)width;
    img->height = (uint32_t)height;
    img->bpp = (uint32_t)bpp;

    uint32_t row_stride = 0;
    if (bpp == 24) row_stride = (width * 3 + 3) & ~3U;
    else if (bpp == 32) row_stride = width * 4;
    else if (bpp == 8) row_stride = (width + 3) & ~3U;

    const uint8_t *palette = data + 54;

    for (int32_t r = 0; r < height; r++) {
        int32_t y = top_down ? r : (height - 1 - r);
        const uint8_t *row = data + data_offset + r * row_stride;
        if ((uint32_t)(data_offset + r * row_stride) >= size) break;

        for (int32_t x = 0; x < width; x++) {
            uint32_t color = 0;
            if (bpp == 24) {
                uint8_t b = row[x * 3 + 0];
                uint8_t g = row[x * 3 + 1];
                uint8_t r_val = row[x * 3 + 2];
                color = ((uint32_t)r_val << 16) | ((uint32_t)g << 8) | (uint32_t)b;
            } else if (bpp == 32) {
                uint8_t b = row[x * 4 + 0];
                uint8_t g = row[x * 4 + 1];
                uint8_t r_val = row[x * 4 + 2];
                color = ((uint32_t)r_val << 16) | ((uint32_t)g << 8) | (uint32_t)b;
            } else if (bpp == 8) {
                uint8_t idx = row[x];
                uint8_t b = palette[idx * 4 + 0];
                uint8_t g = palette[idx * 4 + 1];
                uint8_t r_val = palette[idx * 4 + 2];
                color = ((uint32_t)r_val << 16) | ((uint32_t)g << 8) | (uint32_t)b;
            }
            img->pixels[y * width + x] = color;
        }
    }

    img->loaded = true;
    return true;
}

static bool parse_ppm(const uint8_t *data, uint32_t size, struct image_data *img) {
    if (size < 15 || data[0] != 'P' || data[1] != '6') return false;
    uint32_t idx = 2;
    while (idx < size && (data[idx] == ' ' || data[idx] == '\n' || data[idx] == '\r' || data[idx] == '\t')) idx++;
    if (idx >= size) return false;
    if (data[idx] == '#') {
        while (idx < size && data[idx] != '\n') idx++;
        idx++;
    }
    uint32_t w = 0;
    while (idx < size && data[idx] >= '0' && data[idx] <= '9') {
        w = w * 10 + (data[idx] - '0');
        idx++;
    }
    while (idx < size && (data[idx] == ' ' || data[idx] == '\n' || data[idx] == '\r' || data[idx] == '\t')) idx++;
    uint32_t h = 0;
    while (idx < size && data[idx] >= '0' && data[idx] <= '9') {
        h = h * 10 + (data[idx] - '0');
        idx++;
    }
    while (idx < size && (data[idx] == ' ' || data[idx] == '\n' || data[idx] == '\r' || data[idx] == '\t')) idx++;
    while (idx < size && data[idx] >= '0' && data[idx] <= '9') idx++; // maxval (255)
    if (idx < size && (data[idx] == ' ' || data[idx] == '\n' || data[idx] == '\r' || data[idx] == '\t')) idx++;

    if (w == 0 || w > MAX_IMAGE_WIDTH || h == 0 || h > MAX_IMAGE_HEIGHT) return false;

    img->pixels = (uint32_t *)pc_heap_grow(w * h * sizeof(uint32_t));
    if (!img->pixels) return false;

    img->width = w;
    img->height = h;
    img->bpp = 24;

    for (uint32_t y = 0; y < h; y++) {
        for (uint32_t x = 0; x < w; x++) {
            if (idx + 3 > size) break;
            uint8_t r = data[idx++];
            uint8_t g = data[idx++];
            uint8_t b = data[idx++];
            img->pixels[y * w + x] = ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
        }
    }

    img->loaded = true;
    return true;
}

static bool load_image_file(const char *path, struct image_data *img) {
    if (!path || !img) return false;
    pc_copy(img->path, path, sizeof(img->path));
    img->loaded = false;
    img->pixels = NULL;

    int32_t fd = pc_file_open(path);
    if (fd < 0) return false;

    uint8_t *buf = (uint8_t *)pc_heap_grow(MAX_IMAGE_FILE_SIZE);
    if (!buf) {
        (void)pc_file_close(fd);
        return false;
    }

    int32_t bytes_read = pc_file_read(fd, buf, MAX_IMAGE_FILE_SIZE);
    (void)pc_file_close(fd);

    if (bytes_read <= 0) return false;

    if (parse_bmp(buf, (uint32_t)bytes_read, img)) return true;
    if (parse_ppm(buf, (uint32_t)bytes_read, img)) return true;

    return false;
}

static void draw_image_view(struct pg_window *window, const struct image_data *img) {
    struct pg_rect client = pg_window_client(window);
    pg_window_clear(window, 0x001B2028); // Dark studio background

    if (!img->loaded || !img->pixels) {
        const char *basename = img->path;
        for (const char *p = img->path; *p; p++) {
            if (*p == '/') basename = p + 1;
        }

        // Check if it's likely a compressed format
        uint32_t nlen = pc_strlen(basename);
        bool is_png = nlen >= 4 && (pc_strcmp(basename + nlen - 4, ".png") == 0 ||
                                     pc_strcmp(basename + nlen - 4, ".PNG") == 0);
        bool is_jpg = nlen >= 4 && (pc_strcmp(basename + nlen - 4, ".jpg") == 0 ||
                                     pc_strcmp(basename + nlen - 4, ".JPG") == 0);

        pg_window_rect(window, (struct pg_rect){0, 0, client.width, client.height}, 0x001B2028);
        pg_window_rect(window, (struct pg_rect){0, 0, client.width, 48}, 0x00242B35);
        pg_window_text(window, 8, 14, "Image Viewer", 0x00E0E0E0);

        if (is_png || is_jpg) {
            pg_window_text(window, 20, 72, "Cannot display compressed image.", 0x00FF6B6B);
            if (is_png)
                pg_window_text(window, 20, 96, "PNG format uses zlib compression - not supported.", 0x00CCAA44);
            else
                pg_window_text(window, 20, 96, "JPEG format uses lossy compression - not supported.", 0x00CCAA44);
            pg_window_text(window, 20, 120, "Convert your image to BMP (24-bit) using:", 0x00AAAAAA);
            pg_window_text(window, 20, 144, "  convert image.png image.bmp", 0x0066CCFF);
            pg_window_text(window, 20, 168, "Supported formats: BMP 24-bit, BMP 32-bit, PPM P6", 0x00888888);
        } else {
            pg_window_text(window, 20, 72, "Cannot load image file.", 0x00FF6B6B);
            pg_window_text(window, 20, 96, "Supported: BMP (24-bit, 32-bit, 8-bit), PPM (P6 binary)", 0x00CCCCCC);
            pg_window_text(window, 20, 120, "File:", 0x00888888);
            pg_window_text(window, 68, 120, img->path, 0x00AAAAAA);
        }
        return;
    }

    // Top status bar
    char info[128];
    pc_copy(info, "File: ", sizeof(info));
    uint32_t len = pc_strlen(info);
    pc_copy(info + len, img->path, sizeof(info) - len);

    pg_window_rect(window, (struct pg_rect){0, 0, client.width, 24}, 0x00242B35);
    pg_window_text(window, 8, 4, info, 0x00E0E0E0);

    // Render Image Centered inside client area
    uint32_t avail_w = client.width;
    uint32_t avail_h = client.height > 24 ? client.height - 24 : 0;
    uint32_t start_x = (avail_w > img->width) ? (avail_w - img->width) / 2 : 0;
    uint32_t start_y = 24 + ((avail_h > img->height) ? (avail_h - img->height) / 2 : 0);

    uint32_t draw_w = img->width < avail_w ? img->width : avail_w;
    uint32_t draw_h = img->height < avail_h ? img->height : avail_h;

    // Fast Span-based rendering
    for (uint32_t y = 0; y < draw_h; y++) {
        uint32_t py = start_y + y;
        uint32_t x = 0;
        while (x < draw_w) {
            uint32_t color = img->pixels[y * img->width + x];
            uint32_t span = 1;
            while (x + span < draw_w && img->pixels[y * img->width + x + span] == color) {
                span++;
            }
            pg_window_rect(window, (struct pg_rect){start_x + x, py, span, 1}, color);
            x += span;
        }
    }
}

int main(void) {
    char cmdline[128];
    char path[128] = "/src/demo/screenshot.bmp";

    int32_t cmdlen = pc_get_command_line(cmdline, sizeof(cmdline));
    if (cmdlen > 0) {
        // command_line is passed directly as the file path argument
        // strip leading spaces
        uint32_t start = 0;
        while (cmdline[start] == ' ' || cmdline[start] == '\t') start++;
        // strip trailing whitespace/newlines
        uint32_t end = (uint32_t)cmdlen;
        while (end > start && (cmdline[end-1] == ' ' || cmdline[end-1] == '\n' ||
                                cmdline[end-1] == '\r' || cmdline[end-1] == '\t')) end--;
        if (end > start) {
            cmdline[end] = '\0';
            pc_copy(path, cmdline + start, sizeof(path));
        }
    }

    (void)load_image_file(path, &g_image);

    struct pc_display_info display;
    if (!pc_display_get_info(&display) || !display.available) return 1;

    uint32_t win_w = g_image.loaded ? g_image.width + 16 : 640;
    uint32_t win_h = g_image.loaded ? g_image.height + 40 : 400;

    if (win_w < 400) win_w = 400;
    if (win_h < 300) win_h = 300;
    if (win_w > display.width - 40) win_w = display.width - 40;
    if (win_h > display.height - 60) win_h = display.height - 60;

    struct pg_window window;
    char title[64];
    pc_copy(title, "Image Viewer - ", sizeof(title));
    uint32_t tlen = pc_strlen(title);
    // Show only filename, not full path in title
    const char *basename = path;
    for (const char *p = path; *p; p++) {
        if (*p == '/') basename = p + 1;
    }
    pc_copy(title + tlen, basename, sizeof(title) - tlen);

    if (!pg_window_center(&window, title, win_w, win_h)) return 1;

    struct pg_event event = {.type = PG_EVENT_NONE};
    pg_window_begin(&window);
    draw_image_view(&window, &g_image);
    pg_window_end(&window);

    while (pg_window_is_open(&window)) {
        if (!pg_window_poll_event(&window, &event)) {
            pc_sleep(16);
            continue;
        }
        if (event.type == PG_EVENT_CLOSE) break;
        if (event.type == PG_EVENT_KEY && (event.key == 27 || event.key == 'q' || event.key == 'Q')) break;

        if (event.type == PG_EVENT_REPAINT) {
            pg_window_begin(&window);
            draw_image_view(&window, &g_image);
            pg_window_end(&window);
        }
    }

    pg_window_close(&window);
    return 0;
}
