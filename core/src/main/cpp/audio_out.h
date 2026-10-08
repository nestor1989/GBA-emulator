#ifndef AUDIO_OUT_H
#define AUDIO_OUT_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Opens and starts a low-latency output stream that pulls from emu_audio_read().
bool audio_out_start(void);
void audio_out_stop(void);

#ifdef __cplusplus
}
#endif

#endif
