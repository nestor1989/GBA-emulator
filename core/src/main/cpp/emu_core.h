// Platform-independent emulation engine on top of mGBA.
//
// Threads:
//  - caller thread (Android main thread): load/unload, pause/resume, commands.
//  - emulation thread (owned here): runs frames, produces audio and video.
//  - audio thread (Oboe callback): consumes audio with emu_audio_read().
//  - GL thread (GLSurfaceView): consumes video with emu_video_latest().
#ifndef EMU_CORE_H
#define EMU_CORE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
	EMU_OK = 0,
	EMU_ERR_NOT_FOUND = -1,   // file missing or unreadable
	EMU_ERR_NOT_A_GAME = -2,  // no compatible ROM in the file or archive
	EMU_ERR_LOAD_FAILED = -3, // ROM recognized but the core could not start
	EMU_ERR_BAD_SAVE = -4,    // file is not a usable save for this game
};

// Fast-forward multipliers: 1 (normal), 2 (audio sped up), 4 and 8 (muted).
#define EMU_MAX_SPEED 8

// Keys, same bit layout as mGBA (GBA_KEY_* / GB_KEY_*).
enum {
	EMU_KEY_A = 1 << 0,
	EMU_KEY_B = 1 << 1,
	EMU_KEY_SELECT = 1 << 2,
	EMU_KEY_START = 1 << 3,
	EMU_KEY_RIGHT = 1 << 4,
	EMU_KEY_LEFT = 1 << 5,
	EMU_KEY_UP = 1 << 6,
	EMU_KEY_DOWN = 1 << 7,
	EMU_KEY_R = 1 << 8,
	EMU_KEY_L = 1 << 9,
};

// Largest frame any enabled core can produce (GB with SGB borders).
#define EMU_MAX_WIDTH 256
#define EMU_MAX_HEIGHT 224

enum { EMU_LOG_DEBUG = 0, EMU_LOG_INFO = 1, EMU_LOG_WARN = 2, EMU_LOG_ERROR = 3 };
typedef void (*EmuLogFn)(int level, const char* message);
void emu_set_log_fn(EmuLogFn fn);

// Checks that the file (plain ROM, .zip or .7z) contains a compatible game.
int emu_probe(const char* path);

// Loads a game. Saves go to <saveDir>/<CRC32>.sav and the manual state to
// <stateDir>/<CRC32>.ss1, so renaming or zipping the ROM keeps its saves.
int emu_load(const char* romPath, const char* saveDir, const char* stateDir);
bool emu_is_loaded(void);
// Stops the thread, writes the save to disk and frees the core.
void emu_unload(void);

// Starts the emulation thread in the paused state.
void emu_start(void);
// Pausing blocks until the current frame finishes and the thread is idle.
void emu_set_paused(bool paused);
void emu_set_keys(uint32_t keys);

bool emu_save_state(void);
bool emu_load_state(void);
bool emu_has_state(void);
// Forces the in-game save (.sav) to physical storage.
void emu_flush_save(void);

void emu_set_speed(unsigned speed);
unsigned emu_get_speed(void);

// Cheats (GameShark, Action Replay, CodeBreaker; type autodetected). Each
// entry is one cheat, its lines separated by '\n'. Replaces every active
// cheat. Returns -1 when all were accepted, else the index of the first one
// rejected (the rest still apply).
int emu_cheats_apply(const char* const* codes, size_t count);
bool emu_cheat_validate(const char* code);

// Battery saves from other emulators: raw .sav/.srm (VBA, VBA-M, My Boy!,
// mGBA, RetroArch/Lemuroid) and, for GBA, GameShark SP (.gsv) and SharkPort
// (.sps). The current save is first copied to <saveDir>/backup/<CRC32>.sav.
// The game restarts so it reads the new save.
int emu_import_save(const uint8_t* data, size_t size);
// malloc'd copy of the in-game save as a raw .sav, or NULL if there is none.
uint8_t* emu_export_save(size_t* size);
bool emu_has_save_backup(void);
// Swaps the current save with the backup (calling it twice undoes it).
bool emu_restore_save_backup(void);

// CRC32 of the ROM, 8 hex digits: identifies the game for saves and cheats.
void emu_get_game_id(char* out, size_t size);
void emu_get_title(char* out, size_t size);
void emu_get_video_size(unsigned* width, unsigned* height);
uint64_t emu_frame_count(void);
// Test helper: reads a byte from the emulated bus. Only while paused.
uint8_t emu_debug_read8(uint32_t address);

// Audio output side (Oboe callback thread).
void emu_audio_set_output_rate(unsigned rate);
void emu_audio_set_active(bool active);
// Fills `frames` stereo frames (interleaved int16). Missing samples are silence.
// Returns how many frames came from the emulator.
size_t emu_audio_read(int16_t* out, size_t frames);
size_t emu_audio_buffered_frames(void);

// Video output side (GL thread). Returns the most recent complete frame in
// RGBA8888 (R in the lowest byte). `fresh` is true when it differs from the
// previous call. The pointer stays valid until the next call. Returns NULL
// when no frame exists yet.
const uint32_t* emu_video_latest(unsigned* width, unsigned* height, unsigned* stride, bool* fresh);

#ifdef __cplusplus
}
#endif

#endif
