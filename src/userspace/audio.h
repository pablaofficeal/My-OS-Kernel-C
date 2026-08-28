#pragma once

#include "../kernel/syscall.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

bool userspace_audio_get_status(struct audio_status *status);
uint8_t userspace_audio_get_volume(void);
bool userspace_audio_is_muted(void);
void userspace_audio_set_volume(uint8_t volume);
void userspace_audio_set_muted(bool muted);
void userspace_audio_toggle_mute(void);
void userspace_audio_adjust_volume(int8_t delta);
void userspace_audio_play_test_sound(void);

#ifdef __cplusplus
}
#endif
