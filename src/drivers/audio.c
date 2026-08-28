#include "audio.h"
#include "../kernel/scheduler.h"

#define PIT_CHANNEL2_PORT 0x42
#define PIT_COMMAND_PORT  0x43
#define SPEAKER_PORT      0x61
#define PIT_FREQUENCY     1193180U
#define AUDIO_DEFAULT_VOLUME 65

static uint8_t audio_volume = AUDIO_DEFAULT_VOLUME;
static bool audio_muted;

static inline void outb(uint16_t port, uint8_t value) {
    __asm__ volatile("outb %0,%1"::"a"(value),"Nd"(port));
}

static inline uint8_t inb(uint16_t port) {
    uint8_t value;
    __asm__ volatile("inb %1,%0":"=a"(value):"Nd"(port));
    return value;
}

static void audio_wait(uint32_t milliseconds) {
    for (volatile uint64_t wait = 0; wait < (uint64_t)milliseconds * 850000ULL; wait++) {
        __asm__ volatile("pause");
    }
    scheduler_yield();
}

static uint8_t clamp_volume(uint8_t volume) {
    return volume > 100 ? 100 : volume;
}

static void pc_speaker_off(void) {
    uint8_t state = inb(SPEAKER_PORT);
    outb(SPEAKER_PORT, state & (uint8_t)~0x03);
}

static void pc_speaker_tone(uint32_t frequency_hz) {
    if (frequency_hz == 0 || audio_muted || audio_volume == 0) {
        pc_speaker_off();
        return;
    }

    uint32_t divisor = PIT_FREQUENCY / frequency_hz;
    if (divisor == 0) {
        divisor = 1;
    }

    outb(PIT_COMMAND_PORT, 0xB6);
    outb(PIT_CHANNEL2_PORT, (uint8_t)(divisor & 0xFF));
    outb(PIT_CHANNEL2_PORT, (uint8_t)((divisor >> 8) & 0xFF));

    uint8_t state = inb(SPEAKER_PORT);
    outb(SPEAKER_PORT, state | 0x03);
}

void audio_init(void) {
    audio_volume = AUDIO_DEFAULT_VOLUME;
    audio_muted = false;
    pc_speaker_off();
}

void audio_get_status(struct audio_status *status) {
    if (!status) {
        return;
    }

    status->volume = audio_volume;
    status->muted = audio_muted ? 1 : 0;
    status->backend = AUDIO_BACKEND_PC_SPEAKER;
}

uint8_t audio_get_volume(void) {
    return audio_volume;
}

bool audio_is_muted(void) {
    return audio_muted;
}

void audio_set_volume(uint8_t volume) {
    audio_volume = clamp_volume(volume);
    if (audio_volume > 0) {
        audio_muted = false;
    }
}

void audio_set_muted(bool muted) {
    audio_muted = muted;
    if (audio_muted) {
        pc_speaker_off();
    }
}

void audio_adjust_volume(int8_t delta) {
    int16_t next_volume = (int16_t)audio_volume + delta;

    if (next_volume < 0) {
        next_volume = 0;
    }
    if (next_volume > 100) {
        next_volume = 100;
    }

    audio_set_volume((uint8_t)next_volume);
}

void audio_play_test_sound(void) {
    if (audio_muted || audio_volume == 0) {
        return;
    }

    pc_speaker_tone(880);
    audio_wait(90);
    pc_speaker_tone(1175);
    audio_wait(90);
    pc_speaker_tone(1568);
    audio_wait(120);
    pc_speaker_off();
}
