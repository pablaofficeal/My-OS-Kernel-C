#include "audio.h"
#include "audio_hda.h"
#include "../kernel/system_info.h"

#define PIT_CHANNEL2_PORT 0x42
#define PIT_COMMAND_PORT  0x43
#define SPEAKER_PORT      0x61
#define PIT_FREQUENCY     1193180U
#define AUDIO_DEFAULT_VOLUME 65

struct audio_master_bus {
    uint8_t volume;
    bool muted;
    uint32_t active_backend;
    uint32_t available_backends;
    bool pcm_ready;
    bool test_active;
    uint8_t test_step;
    uint64_t next_step_ms;
};

static struct audio_master_bus master_bus;

static const uint16_t test_frequencies[] = {
    880,
    1175,
    1568
};

static const uint16_t test_durations_ms[] = {
    90,
    90,
    120
};

static inline void outb(uint16_t port, uint8_t value) {
    __asm__ volatile("outb %0,%1"::"a"(value),"Nd"(port));
}

static inline uint8_t inb(uint16_t port) {
    uint8_t value;
    __asm__ volatile("inb %1,%0":"=a"(value):"Nd"(port));
    return value;
}

static uint8_t clamp_volume(uint8_t volume) {
    return volume > 100 ? 100 : volume;
}

static void pc_speaker_off(void) {
    uint8_t state = inb(SPEAKER_PORT);
    outb(SPEAKER_PORT, state & (uint8_t)~0x03);
}

static void pc_speaker_tone(uint32_t frequency_hz) {
    if (frequency_hz == 0 || master_bus.muted || master_bus.volume == 0) {
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

static void stop_test_sound(void) {
    master_bus.test_active = false;
    master_bus.test_step = 0;
    master_bus.next_step_ms = 0;
    pc_speaker_off();
}

static void start_pc_speaker_test(void) {
    master_bus.test_active = true;
    master_bus.test_step = 0;
    master_bus.next_step_ms = system_info_uptime_ms() + test_durations_ms[0];
    pc_speaker_tone(test_frequencies[0]);
}

void audio_init(void) {
    master_bus.volume = AUDIO_DEFAULT_VOLUME;
    master_bus.muted = false;
    master_bus.active_backend = AUDIO_BACKEND_PC_SPEAKER;
    master_bus.available_backends = AUDIO_BACKEND_PC_SPEAKER;
    master_bus.pcm_ready = false;
    master_bus.test_active = false;
    master_bus.test_step = 0;
    master_bus.next_step_ms = 0;
    pc_speaker_off();
    hda_init();
    if (hda_is_present()) {
        master_bus.available_backends |= AUDIO_BACKEND_HDA;
        master_bus.active_backend = AUDIO_BACKEND_HDA;
        master_bus.pcm_ready = hda_pcm_output_ready();
    }
}

void audio_get_status(struct audio_status *status) {
    if (!status) {
        return;
    }

    audio_update();
    status->volume = master_bus.volume;
    status->muted = master_bus.muted ? 1 : 0;
    status->backend = master_bus.active_backend;
    status->available_backends = master_bus.available_backends;
    status->pcm_ready = master_bus.pcm_ready ? 1 : 0;
    status->test_active = master_bus.test_active ? 1 : 0;
}

uint8_t audio_get_volume(void) {
    return master_bus.volume;
}

bool audio_is_muted(void) {
    return master_bus.muted;
}

void audio_set_volume(uint8_t volume) {
    master_bus.volume = clamp_volume(volume);
    if (master_bus.volume > 0) {
        master_bus.muted = false;
    }
    if (master_bus.muted || master_bus.volume == 0) {
        stop_test_sound();
    }
}

void audio_set_muted(bool muted) {
    master_bus.muted = muted;
    if (master_bus.muted) {
        stop_test_sound();
    }
}

void audio_adjust_volume(int8_t delta) {
    int16_t next_volume = (int16_t)master_bus.volume + delta;

    if (next_volume < 0) {
        next_volume = 0;
    }
    if (next_volume > 100) {
        next_volume = 100;
    }

    audio_set_volume((uint8_t)next_volume);
}

void audio_play_test_sound(void) {
    if (master_bus.muted || master_bus.volume == 0) {
        return;
    }

    if (master_bus.pcm_ready) {
        return;
    }

    start_pc_speaker_test();
}

void audio_update(void) {
    if (!master_bus.test_active) {
        return;
    }
    if (master_bus.muted || master_bus.volume == 0) {
        stop_test_sound();
        return;
    }

    uint64_t now = system_info_uptime_ms();
    if (now < master_bus.next_step_ms) {
        return;
    }

    master_bus.test_step++;
    if (master_bus.test_step >= sizeof(test_frequencies) / sizeof(test_frequencies[0])) {
        stop_test_sound();
        return;
    }

    master_bus.next_step_ms = now + test_durations_ms[master_bus.test_step];
    pc_speaker_tone(test_frequencies[master_bus.test_step]);
}
