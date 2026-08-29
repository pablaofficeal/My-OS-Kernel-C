#include "settings/model.h"
#include "../../../libc/include/purec.h"
#define SETTINGS_PATH "/config/settings.ini"
#define ENV_FALLBACK "settings.volume"
static int to_int(const char *s) {
    int v = 0;
    while (s && *s >= '0' && *s <= '9') { v = v * 10 + (*s - '0'); s++; }
    return v;
}
static void write_int(char *out, int v) {
    char tmp[12];
    int p = 11;
    tmp[p--] = '\0';
    if (v == 0) tmp[p--] = '0';
    else while (v > 0 && p >= 0) { tmp[p--] = '0' + v % 10; v /= 10; }
    int i = 0;
    for (int k = p + 1; tmp[k]; k++) out[i++] = tmp[k];
    out[i] = '\0';
}
void settings_model_init(struct settings_model *m) {
    m->volume = 75;
    m->muted = 0;
    m->device = 0;
}
void settings_model_save(const struct settings_model *m) {
    char buf[128];
    char vol[16];
    char dev[16];
    write_int(vol, m->volume);
    write_int(dev, m->device);
    buf[0] = '\0';
    pc_copy(buf, "volume=", sizeof(buf));
    uint32_t len = pc_strlen(buf);
    for (int i = 0; vol[i] && len + 1 < sizeof(buf); i++) buf[len++] = vol[i];
    buf[len++] = '\n'; buf[len] = '\0';
    char muted_line[16];
    muted_line[0] = 'm'; muted_line[1] = 'u'; muted_line[2] = 't'; muted_line[3] = 'e'; muted_line[4] = 'd'; muted_line[5] = '=';
    muted_line[6] = m->muted ? '1' : '0'; muted_line[7] = '\n'; muted_line[8] = '\0';
    for (int i = 0; muted_line[i] && len + 1 < sizeof(buf); i++) buf[len++] = muted_line[i];
    buf[len] = '\0';
    char devline[32] = "device=";
    uint32_t dl = pc_strlen(devline);
    for (int i = 0; dev[i]; i++) devline[dl++] = dev[i];
    devline[dl++] = '\n'; devline[dl] = '\0';
    for (int i = 0; devline[i] && len + 1 < sizeof(buf); i++) buf[len++] = devline[i];
    buf[len] = '\0';
    pc_directory_create("/config");
    int rc = pc_file_write(SETTINGS_PATH, buf, len);
    if (rc < 0) { pc_file_create(SETTINGS_PATH); pc_file_write(SETTINGS_PATH, buf, len); }
    pc_setenv(ENV_FALLBACK, vol);
    if (rc < 0) {
        char fb[32] = "fallback:";
        uint32_t fl = pc_strlen(fb);
        for (int i = 0; vol[i] && fl + 1 < sizeof(fb); i++) fb[fl++] = vol[i];
        fb[fl] = '\0';
        pc_setenv("settings.fallback", fb);
    }
}
void settings_model_load(struct settings_model *m) {
    int fd = pc_file_open(SETTINGS_PATH);
    if (fd >= 0) {
        char buf[256] = {0};
        int r = pc_file_read(fd, buf, sizeof(buf) - 1);
        pc_file_close(fd);
        if (r > 0) {
            buf[r] = '\0';
            char *p = buf;
            while (*p) {
                char *e = p;
                while (*e && *e != '\n') e++;
                char save = *e; *e = '\0';
                if (p[0] == 'v') { char *eq = p; while (*eq && *eq != '=') eq++; if (*eq == '=') m->volume = to_int(eq + 1); }
                else if (p[0] == 'm') { char *eq = p; while (*eq && *eq != '=') eq++; if (*eq == '=') m->muted = to_int(eq + 1) ? 1 : 0; }
                else if (p[0] == 'd') { char *eq = p; while (*eq && *eq != '=') eq++; if (*eq == '=') m->device = to_int(eq + 1); }
                if (save == '\0') break;
                p = e + 1;
            }
            return;
        }
    }
    char env[32] = {0};
    if (pc_getenv(ENV_FALLBACK, env, sizeof(env)) >= 0 && env[0]) m->volume = to_int(env);
}
void settings_model_apply(const struct settings_model *m) {
    pc_audio_set_volume((uint32_t)m->volume);
    pc_audio_set_muted(m->muted != 0);
    if (m->device) pc_audio_select_output((uint32_t)m->device);
}
