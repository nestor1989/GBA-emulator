// PSG-only sound: lead on channel 2, bass on the wave channel (3), effects on
// channels 1 and 4. Original music written for Cometa.
#include "gba.h"
#include "assets.h"
#include "sound.h"

#define REST 0

typedef struct {
    const u8* lead;     // pairs: midi note, length in steps (0,0 ends)
    const u8* bass;     // one root per bar, 0 ends
    const s8* pattern;  // 8 offsets per bar for the bass
    u8 step_frames;
    u8 loop;
    u16 lead_env;       // SOUND2CNT_L value
} Song;

// ---- Main theme (A minor, 16 bars) -------------------------------------------
static const u8 main_lead[] = {
    69, 2, 72, 2, 76, 2, 74, 1, 72, 1,
    69, 4, 65, 2, 67, 1, 69, 1,
    67, 2, 64, 2, 67, 2, 72, 2,
    71, 4, 74, 2, 71, 2,
    69, 2, 72, 2, 76, 2, 81, 2,
    79, 2, 77, 2, 76, 2, 72, 2,
    74, 2, 76, 2, 72, 2, 67, 2,
    71, 2, 74, 2, 76, 4,
    72, 2, 69, 2, 72, 2, 77, 2,
    74, 2, 71, 2, 74, 2, 79, 2,
    76, 3, 74, 1, 72, 2, 71, 2,
    69, 6, REST, 2,
    81, 2, 79, 2, 77, 2, 76, 2,
    74, 2, 76, 2, 77, 2, 74, 2,
    76, 2, 71, 2, 68, 2, 71, 2,
    69, 4, 76, 2, 72, 2,
    0, 0,
};
static const u8 main_bass[] = { 45, 41, 48, 43, 45, 41, 48, 43, 41, 43, 45, 45, 41, 43, 40, 45, 0 };
static const s8 main_pattern[8] = { 0, 12, 0, 12, 0, 12, 7, 12 };

// ---- Boss (driving arpeggios) ---------------------------------------------------
static const u8 boss_lead[] = {
    69, 1, 72, 1, 76, 1, 72, 1, 69, 1, 72, 1, 76, 1, 81, 1,
    69, 1, 72, 1, 76, 1, 72, 1, 69, 1, 72, 1, 76, 1, 79, 1,
    65, 1, 69, 1, 72, 1, 69, 1, 65, 1, 69, 1, 72, 1, 77, 1,
    67, 1, 71, 1, 74, 1, 71, 1, 67, 1, 71, 1, 74, 1, 79, 1,
    69, 1, 72, 1, 76, 1, 72, 1, 69, 1, 72, 1, 76, 1, 81, 1,
    69, 1, 72, 1, 76, 1, 72, 1, 69, 1, 72, 1, 76, 1, 84, 1,
    64, 1, 68, 1, 71, 1, 68, 1, 64, 1, 68, 1, 71, 1, 76, 1,
    64, 1, 68, 1, 71, 1, 74, 1, 76, 2, REST, 2,
    0, 0,
};
static const u8 boss_bass[] = { 45, 45, 41, 43, 45, 45, 40, 40, 0 };
static const s8 boss_pattern[8] = { 0, 0, 12, 0, 0, 12, 0, 10 };

// ---- Jingles ---------------------------------------------------------------------
static const u8 over_lead[] = { 76, 2, 74, 2, 72, 2, 71, 2, 69, 8, 0, 0 };
static const u8 over_bass[] = { 45, 45, 0 };
static const s8 over_pattern[8] = { 0, -1, -1, -1, 0, -1, -1, -1 };
static const u8 clear_lead[] = { 69, 1, 72, 1, 76, 1, 81, 5, 79, 1, 81, 7, 0, 0 };
static const u8 clear_bass[] = { 45, 45, 0 };
static const s8 clear_pattern[8] = { 0, 12, 0, 12, 0, 12, 0, 12 };

static const Song songs[] = {
    { main_lead, main_bass, main_pattern, 13, 1, 0x9440 },
    { boss_lead, boss_bass, boss_pattern, 9, 1, 0x8240 },
    { over_lead, over_bass, over_pattern, 12, 0, 0x9480 },
    { clear_lead, clear_bass, clear_pattern, 8, 0, 0x9440 },
};

static const Song* song;
static u16 lead_pos, bass_bar, lead_left, bass_step, step_timer;
static u8 playing, muted;

// Effects on channel 1 can be a short note sequence (power-up arpeggio).
static const u8* sfx1_seq;
static u8 sfx1_timer;

static void wave_init(void) {
    REG_SOUND3CNT_L = 0;             // stop; bank 0 plays, so the CPU writes bank 1
    // Sawtooth: 32 four-bit samples, two per byte, high nibble first.
    for (int word = 0; word < 4; word++) {
        u32 value = 0;
        for (int b = 0; b < 4; b++) {
            int i = (word * 4 + b) * 2;
            u32 hi = 15 - i / 2;
            u32 lo = 15 - (i + 1) / 2;
            value |= ((hi << 4) | lo) << (8 * b);
        }
        WAVE_RAM[word] = value;
    }
    REG_SOUND3CNT_L = 0x40;          // play bank 1
}

void snd_init(void) {
    REG_SOUNDCNT_X = 0x80;
    REG_SOUNDCNT_L = 0xFF77;
    REG_SOUNDCNT_H = 0x0002;
    wave_init();
}

void snd_mute(int value) {
    muted = (u8) value;
    REG_SOUNDCNT_L = value ? 0x0000 : 0xFF77;
}

void music_play(int index) {
    song = &songs[index];
    lead_pos = 0;
    bass_bar = 0;
    lead_left = 0;
    bass_step = 0;
    step_timer = 0;
    playing = 1;
}

void music_stop(void) {
    playing = 0;
    REG_SOUND2CNT_L = 0;
    REG_SOUND2CNT_H = 0x8000;
    REG_SOUND3CNT_H = 0;
}

static void music_step(void) {
    // Lead
    if (lead_left == 0) {
        u8 note = song->lead[lead_pos];
        u8 len = song->lead[lead_pos + 1];
        if (len == 0) {
            if (!song->loop) {
                playing = 0;
                REG_SOUND3CNT_H = 0;
                return;
            }
            lead_pos = 0;
            bass_bar = 0;
            bass_step = 0;
            note = song->lead[0];
            len = song->lead[1];
        }
        lead_pos += 2;
        lead_left = len;
        if (note != REST) {
            REG_SOUND2CNT_L = song->lead_env;
            REG_SOUND2CNT_H = note_square[note] | 0x8000;
        } else {
            REG_SOUND2CNT_L = 0;
            REG_SOUND2CNT_H = 0x8000;
        }
    }
    lead_left--;

    // Bass
    u8 root = song->bass[bass_bar];
    if (root == 0) {
        bass_bar = 0;
        root = song->bass[0];
    }
    s8 offset = song->pattern[bass_step];
    if (offset >= 0) {
        // Legato: muting the wave channel between notes would click (DC jump).
        REG_SOUND3CNT_L = 0xC0;
        REG_SOUND3CNT_H = 0x2000;    // 100% volume
        REG_SOUND3CNT_X = note_wave[root + offset] | 0x8000;
    }
    if (++bass_step == 8) {
        bass_step = 0;
        bass_bar++;
    }
}

void snd_update(void) {
    if (playing && !muted) {
        if (step_timer == 0) {
            music_step();
            step_timer = song->step_frames;
        }
        step_timer--;
    }
    if (sfx1_seq) {
        if (sfx1_timer == 0) {
            if (*sfx1_seq == 0) {
                sfx1_seq = 0;
            } else {
                REG_SOUND1CNT_L = 0x0008;
                REG_SOUND1CNT_H = 0xA180;
                REG_SOUND1CNT_X = note_square[*sfx1_seq] | 0x8000;
                sfx1_seq++;
                sfx1_timer = 3;
            }
        }
        if (sfx1_timer) sfx1_timer--;
    }
}

// ---- Effects ------------------------------------------------------------------------
void sfx_shot(void) {
    if (sfx1_seq) return;
    REG_SOUND1CNT_L = 0x001C;               // sweep down
    REG_SOUND1CNT_H = 0x5180;               // vol 5, quick decay, 50% duty
    REG_SOUND1CNT_X = note_square[84] | 0x8000;
}

void sfx_hit(void) {
    REG_SOUND4CNT_L = 0x6100;
    REG_SOUND4CNT_H = 0x8011;
}

void sfx_explode(void) {
    REG_SOUND4CNT_L = 0xB200;
    REG_SOUND4CNT_H = 0x8043;
}

void sfx_big_explode(void) {
    REG_SOUND4CNT_L = 0xF500;
    REG_SOUND4CNT_H = 0x8064;
}

void sfx_death(void) {
    REG_SOUND4CNT_L = 0xF600;
    REG_SOUND4CNT_H = 0x8075;
    sfx1_seq = 0;
    REG_SOUND1CNT_L = 0x007A;
    REG_SOUND1CNT_H = 0xF380;
    REG_SOUND1CNT_X = note_square[72] | 0x8000;
}

static const u8 power_seq[] = { 84, 88, 91, 96, 0 };
static const u8 select_seq[] = { 81, 88, 0 };
static const u8 warning_seq[] = { 69, 63, 69, 63, 69, 63, 0 };

void sfx_power(void) { sfx1_seq = power_seq; sfx1_timer = 0; }
void sfx_select(void) { sfx1_seq = select_seq; sfx1_timer = 0; }
void sfx_warning(void) { sfx1_seq = warning_seq; sfx1_timer = 0; }
