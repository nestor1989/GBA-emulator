#ifndef SOUND_H
#define SOUND_H

enum { SONG_MAIN, SONG_BOSS, SONG_GAMEOVER, SONG_CLEAR };

void snd_init(void);
void snd_update(void);      // once per frame
void snd_mute(int muted);   // pause menu

void music_play(int song);
void music_stop(void);

void sfx_shot(void);
void sfx_hit(void);
void sfx_explode(void);
void sfx_big_explode(void);
void sfx_death(void);
void sfx_power(void);
void sfx_select(void);
void sfx_warning(void);

#endif
