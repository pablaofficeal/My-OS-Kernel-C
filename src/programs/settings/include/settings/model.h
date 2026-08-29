#pragma once
#include <stdint.h>
#include <stdbool.h>
struct settings_model {
    int volume;
    int muted;
    int device;
};
void settings_model_init(struct settings_model *m);
void settings_model_load(struct settings_model *m);
void settings_model_save(const struct settings_model *m);
void settings_model_apply(const struct settings_model *m);
