// Host test for the emulation engine (no Android needed).
//
//   cmake -S core/src/main/cpp -B build-host && cmake --build build-host
//   ./build-host/emu_host_test [game.zip ...] [!not-a-game.zip ...]
//
// It writes a tiny synthetic GBA ROM that stores 0x42 in SRAM and loops, then
// checks probing, loading, clock and audio pacing, video output, the .sav file,
// the manual save state, fast-forward, cheats and importing/exporting saves.
// Extra paths (e.g. a .zip or .7z) are probed and loaded too.
#include "emu_core.h"

#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

static int g_failures;

#define CHECK(cond, ...) \
	do { \
		if (cond) { \
			printf("  ok   "); \
		} else { \
			printf("  FAIL "); \
			++g_failures; \
		} \
		printf(__VA_ARGS__); \
		printf("\n"); \
	} while (0)

static void log_fn(int level, const char* message) {
	if (level >= EMU_LOG_WARN) {
		printf("  [log %d] %s\n", level, message);
	}
}

static void sleep_ms(unsigned ms) {
	struct timespec ts = { (time_t) (ms / 1000), (long) (ms % 1000) * 1000000L };
	nanosleep(&ts, NULL);
}

static void put32(unsigned char* p, unsigned v) {
	p[0] = v & 0xFF;
	p[1] = (v >> 8) & 0xFF;
	p[2] = (v >> 16) & 0xFF;
	p[3] = (v >> 24) & 0xFF;
}

static void write_synthetic_rom(const char* path) {
	unsigned char rom[0x400];
	memset(rom, 0, sizeof(rom));
	put32(&rom[0], 0xEA00002E); // b 0xC0
	memcpy(&rom[0xA0], "HOST TEST   ", 12);
	memcpy(&rom[0xAC], "ZHST", 4);
	rom[0xB2] = 0x96;
	put32(&rom[0xC0], 0xE3A0040E); // mov r0, #0x0E000000
	put32(&rom[0xC4], 0xE3A01042); // mov r1, #0x42
	put32(&rom[0xC8], 0xE5C01000); // strb r1, [r0]
	put32(&rom[0xCC], 0xEAFFFFFE); // b .
	memcpy(&rom[0x200], "SRAM_V113", 9);
	FILE* f = fopen(path, "wb");
	fwrite(rom, 1, sizeof(rom), f);
	fclose(f);
}

static atomic_bool g_consumerRun;

static unsigned long long now_ns(void) {
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (unsigned long long) ts.tv_sec * 1000000000ull + (unsigned long long) ts.tv_nsec;
}

// Pretends to be the Oboe callback: consumes exactly 48000 frames per second
// of wall time (like a real audio clock), in bursts every ~10 ms.
static void* audio_consumer(void* arg) {
	(void) arg;
	int16_t buffer[4800 * 2];
	unsigned long long start = now_ns();
	unsigned long long consumed = 0;
	while (atomic_load(&g_consumerRun)) {
		unsigned long long due = (now_ns() - start) * 48000ull / 1000000000ull;
		while (consumed < due) {
			unsigned long long chunk = due - consumed;
			if (chunk > 4800) {
				chunk = 4800;
			}
			emu_audio_read(buffer, (size_t) chunk);
			consumed += chunk;
		}
		sleep_ms(10);
	}
	return NULL;
}

static void exercise(const char* rom, const char* saves, const char* states, int expectReload) {
	printf("game: %s\n", rom);
	CHECK(emu_probe(rom) == EMU_OK, "probe recognizes the game");
	int result = emu_load(rom, saves, states);
	CHECK(result == EMU_OK, "load (result %d)", result);
	if (result != EMU_OK) {
		return;
	}
	unsigned w, h;
	emu_get_video_size(&w, &h);
	CHECK(w == 240 && h == 160, "video size %ux%u", w, h);

	emu_start();
	emu_set_paused(false);
	sleep_ms(1000);
	emu_set_paused(true);
	unsigned long long frames = (unsigned long long) emu_frame_count();
	CHECK(frames >= 54 && frames <= 66, "clock pacing: %llu frames in 1 s (expected ~60)", frames);

	unsigned vw, vh, stride;
	bool fresh;
	const uint32_t* pixels = emu_video_latest(&vw, &vh, &stride, &fresh);
	CHECK(pixels && vw == 240 && vh == 160 && stride >= 240, "video frame available %ux%u stride %u", vw, vh, stride);

	// Audio-driven pacing.
	emu_audio_set_output_rate(48000);
	emu_audio_set_active(true);
	atomic_store(&g_consumerRun, true);
	pthread_t consumer;
	pthread_create(&consumer, NULL, audio_consumer, NULL);
	unsigned long long before = (unsigned long long) emu_frame_count();
	emu_set_paused(false);
	sleep_ms(2000);
	emu_set_paused(true);
	unsigned long long during = (unsigned long long) emu_frame_count() - before;
	size_t buffered = emu_audio_buffered_frames();
	atomic_store(&g_consumerRun, false);
	pthread_join(consumer, NULL);
	emu_audio_set_active(false);
	CHECK(during >= 114 && during <= 126, "audio pacing: %llu frames in 2 s (expected ~120)", during);
	CHECK(buffered <= 48000 / 10, "audio queue bounded: %zu frames buffered", buffered);

	CHECK(emu_save_state(), "save state");
	CHECK(emu_has_state(), "state file exists");
	emu_set_paused(false);
	sleep_ms(200);
	CHECK(emu_load_state(), "load state while running");
	emu_set_paused(true);
	emu_flush_save();
	emu_unload();
	CHECK(!emu_is_loaded(), "unloaded");

	if (expectReload) {
		result = emu_load(rom, saves, states);
		CHECK(result == EMU_OK && emu_has_state(), "reload finds the state slot again");
		emu_unload();
	}
}

static void features(const char* rom, const char* saves, const char* states) {
	printf("turbo, cheats, save import/export\n");
	CHECK(emu_load(rom, saves, states) == EMU_OK, "load");
	char id[16];
	emu_get_game_id(id, sizeof(id));
	CHECK(strlen(id) == 8, "game id %s", id);

	// Fast-forward with clock pacing (no audio output).
	emu_start();
	unsigned speeds[] = { 4, 8 };
	for (int i = 0; i < 2; ++i) {
		emu_set_speed(speeds[i]);
		unsigned long long before = (unsigned long long) emu_frame_count();
		emu_set_paused(false);
		sleep_ms(1000);
		emu_set_paused(true);
		unsigned long long frames = (unsigned long long) emu_frame_count() - before;
		unsigned expected = 60 * speeds[i];
		CHECK(frames >= expected * 9 / 10 && frames <= expected * 11 / 10, "turbo x%u: %llu frames in 1 s (expected ~%u)", speeds[i], frames, expected);
	}
	// x2 keeps the sound and is paced by the audio clock.
	emu_set_speed(2);
	emu_audio_set_output_rate(48000);
	emu_audio_set_active(true);
	atomic_store(&g_consumerRun, true);
	pthread_t consumer;
	pthread_create(&consumer, NULL, audio_consumer, NULL);
	unsigned long long before = (unsigned long long) emu_frame_count();
	emu_set_paused(false);
	sleep_ms(1000);
	emu_set_paused(true);
	unsigned long long frames = (unsigned long long) emu_frame_count() - before;
	atomic_store(&g_consumerRun, false);
	pthread_join(consumer, NULL);
	emu_audio_set_active(false);
	CHECK(frames >= 108 && frames <= 132, "turbo x2 with audio: %llu frames in 1 s (expected ~120)", frames);
	emu_set_speed(1);

	// Cheats: VBA raw and CodeBreaker formats, autodetected.
	CHECK(emu_cheat_validate("02000000:5A"), "VBA-style code accepted");
	CHECK(emu_cheat_validate("32000001 00A5"), "CodeBreaker code accepted");
	CHECK(!emu_cheat_validate("hola mundo"), "garbage rejected");
	CHECK(!emu_cheat_validate("  \n \n"), "empty rejected");
	const char* cheats[] = { "02000000:5A", "basura", "32000001 00A5\n" };
	int rejected = emu_cheats_apply(cheats, 3);
	CHECK(rejected == 1, "apply reports the bad one (index %d)", rejected);
	emu_set_paused(false);
	sleep_ms(200);
	emu_set_paused(true);
	CHECK(emu_debug_read8(0x02000000) == 0x5A && emu_debug_read8(0x02000001) == 0xA5,
	      "cheats write RAM (%02X %02X)", emu_debug_read8(0x02000000), emu_debug_read8(0x02000001));
	CHECK(emu_cheats_apply(NULL, 0) == -1, "cheats cleared");

	// Saves from other emulators.
	size_t size = 0;
	uint8_t* exported = emu_export_save(&size);
	CHECK(exported && size == 32768 && exported[0] == 0x42, "export: %zu bytes, first byte %02X", size, exported ? exported[0] : 0);
	free(exported);

	uint8_t* sav = malloc(0x20000);
	memset(sav, 0x00, 0x20000);
	sav[0] = 0x99;
	sav[1] = 0x77;
	CHECK(emu_import_save(sav, 32768) == EMU_OK, "import raw 32 KB .sav");
	exported = emu_export_save(&size);
	CHECK(exported && size == 32768 && exported[0] == 0x99 && exported[1] == 0x77, "imported bytes are in the save");
	free(exported);

	sav[1] = 0x55;
	CHECK(emu_import_save(sav, 0x20000) == EMU_OK, "import 128 KB .srm (RetroArch-style padding)");
	exported = emu_export_save(&size);
	CHECK(exported && size == 32768 && exported[1] == 0x55, "trimmed to the cartridge size");
	free(exported);

	sav[1] = 0x33;
	CHECK(emu_import_save(sav, 512) == EMU_OK, "import short file");
	exported = emu_export_save(&size);
	CHECK(exported && size == 32768 && exported[1] == 0x33 && exported[600] == 0xFF, "padded with 0xFF");
	free(exported);

	CHECK(emu_has_save_backup(), "backup exists");
	CHECK(emu_restore_save_backup(), "restore backup");
	exported = emu_export_save(&size);
	CHECK(exported && exported[1] == 0x55, "previous save is back (%02X)", exported ? exported[1] : 0);
	free(exported);
	CHECK(emu_restore_save_backup(), "restore again (undo)");
	exported = emu_export_save(&size);
	CHECK(exported && exported[1] == 0x33, "undo works (%02X)", exported ? exported[1] : 0);
	free(exported);

	uint8_t rom_like[0x400];
	memset(rom_like, 0, sizeof(rom_like));
	rom_like[3] = 0xEA;
	rom_like[0xB2] = 0x96;
	CHECK(emu_import_save(rom_like, sizeof(rom_like)) == EMU_ERR_BAD_SAVE, "a ROM picked by mistake is rejected");
	uint8_t* huge = calloc(1, 300 * 1024);
	CHECK(emu_import_save(huge, 300 * 1024) == EMU_ERR_BAD_SAVE, "a 300 KB file is rejected");
	free(huge);
	free(sav);

	// The imported save survives a reload.
	emu_unload();
	CHECK(emu_load(rom, saves, states) == EMU_OK, "reload");
	exported = emu_export_save(&size);
	CHECK(exported && exported[1] == 0x33, "save persisted on disk (%02X)", exported ? exported[1] : 0);
	free(exported);
	emu_unload();
}

int main(int argc, char** argv) {
	emu_set_log_fn(log_fn);
	char dir[] = "/tmp/emu_host_test_XXXXXX";
	if (!mkdtemp(dir)) {
		perror("mkdtemp");
		return 1;
	}
	char rom[600], saves[600], states[600], junk[600];
	snprintf(rom, sizeof(rom), "%s/Host Test.gba", dir);
	snprintf(saves, sizeof(saves), "%s/saves", dir);
	snprintf(states, sizeof(states), "%s/states", dir);
	snprintf(junk, sizeof(junk), "%s/readme.txt", dir);
	mkdir(saves, 0755);
	mkdir(states, 0755);
	write_synthetic_rom(rom);
	FILE* f = fopen(junk, "w");
	fputs("not a game\n", f);
	fclose(f);

	printf("probe\n");
	CHECK(emu_probe("/nonexistent/file.gba") == EMU_ERR_NOT_FOUND, "missing file -> NOT_FOUND");
	CHECK(emu_probe(junk) == EMU_ERR_NOT_A_GAME, "text file -> NOT_A_GAME");
	CHECK(emu_load(junk, saves, states) == EMU_ERR_NOT_A_GAME, "loading a text file fails cleanly");

	exercise(rom, saves, states, 1);

	// The in-game SRAM write must be in the .sav (CRC name is unknown here,
	// so check every .sav in the folder).
	char cmd[700];
	snprintf(cmd, sizeof(cmd), "for f in '%s'/*.sav; do od -An -tx1 -N1 \"$f\"; done | grep -q 42", saves);
	CHECK(system(cmd) == 0, ".sav contains the byte the game wrote");

	features(rom, saves, states);

	// Extra paths: "file" must load, "!file" must be rejected (e.g. a .zip without a ROM).
	for (int i = 1; i < argc; ++i) {
		if (argv[i][0] == '!') {
			printf("rejected: %s\n", argv[i] + 1);
			CHECK(emu_probe(argv[i] + 1) == EMU_ERR_NOT_A_GAME, "probe rejects it");
			CHECK(emu_load(argv[i] + 1, saves, states) == EMU_ERR_NOT_A_GAME, "load rejects it");
		} else {
			exercise(argv[i], saves, states, 0);
		}
	}

	printf(g_failures ? "\n%d check(s) FAILED\n" : "\nall checks passed\n", g_failures);
	return g_failures ? 1 : 0;
}
