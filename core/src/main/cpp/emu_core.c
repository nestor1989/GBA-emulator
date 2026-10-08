#include "emu_core.h"

#include <mgba/core/cheats.h>
#include <mgba/core/config.h>
#include <mgba/core/core.h>
#include <mgba/core/log.h>
#include <mgba/core/serialize.h>
#include <mgba-util/audio-buffer.h>
#include <mgba-util/audio-resampler.h>
#include <mgba-util/vfs.h>
#ifdef M_CORE_GBA
#include <mgba/internal/gba/sharkport.h>
#endif

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

_Static_assert(sizeof(mColor) == 4, "mGBA must be built with 32-bit color");

// Audio ring between the emulation thread (producer) and the audio callback
// (consumer). 8192 frames is ~170 ms at 48 kHz.
#define AUDIO_RING_FRAMES 8192u
// How much audio we keep queued. Emulation waits while the queue is above
// this, which locks the speed to the audio clock (no drift, no crackles).
#define AUDIO_TARGET_MS 50u
// If the audio callback has not run for this long, pace by the clock instead.
#define AUDIO_STALE_MS 250u
#define SLOT_FRESH 4u
// Above this multiplier fast-forward audio is muted (it would only be noise).
#define AUDIO_MAX_SPEED 2u
// Largest battery save of any cartridge (128 KB flash / GB MBC5) plus room for
// the RTC data some emulators append.
#define MAX_RAW_SAVE (0x20000u + 0x400u)
// SharkPort/GameShark SP files wrap the save with headers.
#define MAX_SAVE_FILE (0x40000u)

enum Command {
	CMD_NONE = 0,
	CMD_SAVE_STATE,
	CMD_LOAD_STATE,
	CMD_IMPORT_SAVE,
	CMD_EXPORT_SAVE,
	CMD_CHEATS_APPLY,
	CMD_CHEAT_VALIDATE,
};

// Inputs and outputs of a command executed on the emulation thread.
struct CommandArgs {
	const void* data;
	size_t size;
	const char* const* strings;
	size_t count;
	void* out;
	size_t outSize;
};

static EmuLogFn g_logFn;
static bool g_loggerInstalled;

static struct mCore* g_core;
static mColor g_renderBuf[EMU_MAX_WIDTH * EMU_MAX_HEIGHT];
static unsigned g_stride;
static uint64_t g_framePeriodNs = 16742706; // GBA: 280896 cycles at 16.78 MHz
static char g_savePath[PATH_MAX];
static char g_statePath[PATH_MAX];
static char g_backupPath[PATH_MAX];
static char g_title[32];
static char g_gameId[16];
static atomic_uint g_speed = 1;
static unsigned g_appliedSpeed = 1; // emulation thread only

// Emulation thread state, guarded by g_mutex.
static pthread_mutex_t g_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_cond = PTHREAD_COND_INITIALIZER;
static pthread_t g_thread;
static bool g_threadRunning;
static bool g_paused = true;
static bool g_stop;
static bool g_idle = true;
static enum Command g_cmd = CMD_NONE;
static struct CommandArgs* g_cmdArgs;
static unsigned g_cmdSeq;
static unsigned g_cmdDoneSeq;
static int g_cmdResult;

static atomic_uint g_keys;
static atomic_uint g_frameCount;

// Audio.
static struct mAudioResampler g_resampler;
static struct mAudioBuffer g_resampled;
static bool g_audioReady;
static int16_t g_ring[AUDIO_RING_FRAMES * 2];
static atomic_size_t g_ringWrite;
static atomic_size_t g_ringRead;
static atomic_uint g_outRate = 48000;
static atomic_bool g_audioActive;
static atomic_uint g_lastReadMs;

// Video triple buffer: the producer writes g_back, the consumer reads g_front,
// g_mid holds the third slot plus a "fresh" flag.
static uint32_t g_slots[3][EMU_MAX_WIDTH * EMU_MAX_HEIGHT];
static unsigned g_slotW[3];
static unsigned g_slotH[3];
static unsigned g_slotStride[3];
static atomic_uint g_mid = 1;
static unsigned g_back = 0;  // producer only
static unsigned g_front = 2; // consumer only

// ---------------------------------------------------------------------------
// Utilities
// ---------------------------------------------------------------------------

static uint64_t now_ns(void) {
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (uint64_t) ts.tv_sec * 1000000000ull + (uint64_t) ts.tv_nsec;
}

static uint32_t now_ms(void) {
	return (uint32_t) (now_ns() / 1000000ull);
}

static void sleep_ns(uint64_t ns) {
	struct timespec ts = { (time_t) (ns / 1000000000ull), (long) (ns % 1000000000ull) };
	while (nanosleep(&ts, &ts) == -1 && errno == EINTR) {
	}
}

static void emu_log(int level, const char* format, ...) {
	if (!g_logFn) {
		return;
	}
	char message[512];
	va_list args;
	va_start(args, format);
	vsnprintf(message, sizeof(message), format, args);
	va_end(args);
	g_logFn(level, message);
}

static void mgba_log(struct mLogger* logger, int category, enum mLogLevel level, const char* format, va_list args) {
	(void) logger;
	if (!g_logFn) {
		return;
	}
	int ours;
	if (level & (mLOG_FATAL | mLOG_ERROR)) {
		ours = EMU_LOG_ERROR;
	} else if (level & mLOG_WARN) {
		ours = EMU_LOG_WARN;
	} else if (level & mLOG_INFO) {
		ours = EMU_LOG_INFO;
	} else {
		return; // debug, stubs and game errors are too noisy
	}
	char message[512];
	int prefix = snprintf(message, sizeof(message), "%s: ", mLogCategoryName(category));
	if (prefix < 0 || (size_t) prefix >= sizeof(message)) {
		prefix = 0;
	}
	vsnprintf(message + prefix, sizeof(message) - (size_t) prefix, format, args);
	g_logFn(ours, message);
}

static struct mLogger g_logger = { .log = mgba_log, .filter = NULL };

static void install_logger(void) {
	if (!g_loggerInstalled) {
		mLogSetDefaultLogger(&g_logger);
		g_loggerInstalled = true;
	}
}

void emu_set_log_fn(EmuLogFn fn) {
	g_logFn = fn;
	install_logger();
}

static void destroy_core(struct mCore* core) {
	mCoreConfigDeinit(&core->config);
	core->deinit(core);
}

// ---------------------------------------------------------------------------
// Audio ring (single producer, single consumer)
// ---------------------------------------------------------------------------

static size_t ring_level(void) {
	return atomic_load_explicit(&g_ringWrite, memory_order_acquire) -
	       atomic_load_explicit(&g_ringRead, memory_order_acquire);
}

static void ring_write(const int16_t* samples, size_t frames) {
	size_t w = atomic_load_explicit(&g_ringWrite, memory_order_relaxed);
	size_t r = atomic_load_explicit(&g_ringRead, memory_order_acquire);
	size_t space = AUDIO_RING_FRAMES - (w - r);
	if (frames > space) {
		frames = space; // queue full: drop the newest samples
	}
	for (size_t done = 0; done < frames;) {
		size_t pos = (w + done) & (AUDIO_RING_FRAMES - 1);
		size_t chunk = AUDIO_RING_FRAMES - pos;
		if (chunk > frames - done) {
			chunk = frames - done;
		}
		memcpy(&g_ring[pos * 2], &samples[done * 2], chunk * 2 * sizeof(int16_t));
		done += chunk;
	}
	atomic_store_explicit(&g_ringWrite, w + frames, memory_order_release);
}

size_t emu_audio_read(int16_t* out, size_t frames) {
	atomic_store_explicit(&g_lastReadMs, now_ms(), memory_order_relaxed);
	size_t r = atomic_load_explicit(&g_ringRead, memory_order_relaxed);
	size_t w = atomic_load_explicit(&g_ringWrite, memory_order_acquire);
	size_t available = w - r;
	size_t n = frames < available ? frames : available;
	for (size_t done = 0; done < n;) {
		size_t pos = (r + done) & (AUDIO_RING_FRAMES - 1);
		size_t chunk = AUDIO_RING_FRAMES - pos;
		if (chunk > n - done) {
			chunk = n - done;
		}
		memcpy(&out[done * 2], &g_ring[pos * 2], chunk * 2 * sizeof(int16_t));
		done += chunk;
	}
	atomic_store_explicit(&g_ringRead, r + n, memory_order_release);
	if (n < frames) {
		memset(&out[n * 2], 0, (frames - n) * 2 * sizeof(int16_t));
	}
	return n;
}

size_t emu_audio_buffered_frames(void) {
	return ring_level();
}

void emu_audio_set_output_rate(unsigned rate) {
	if (rate) {
		atomic_store(&g_outRate, rate);
	}
}

void emu_audio_set_active(bool active) {
	atomic_store(&g_audioActive, active);
}

static bool audio_driving(void) {
	if (!atomic_load(&g_audioActive)) {
		return false;
	}
	uint32_t last = atomic_load_explicit(&g_lastReadMs, memory_order_relaxed);
	return last && (uint32_t) (now_ms() - last) < AUDIO_STALE_MS;
}

static void pump_audio(void) {
	if (!g_audioReady) {
		return;
	}
	struct mAudioBuffer* source = g_core->getAudioBuffer(g_core);
	unsigned sourceRate = g_core->audioSampleRate(g_core);
	unsigned outRate = atomic_load(&g_outRate);
	if (!source || source->channels != 2 || !sourceRate || !outRate) {
		return;
	}
	// Fast-forward x2 plays the audio sped up (the source "runs" twice as
	// fast); higher speeds are muted.
	unsigned speed = atomic_load(&g_speed);
	mAudioResamplerSetSource(&g_resampler, source, (double) sourceRate * speed, true);
	mAudioResamplerSetDestination(&g_resampler, &g_resampled, outRate);
	// Without an output stream (or when muted) the samples are still drained,
	// so the core's buffer never overflows, but dropped: queuing them would
	// replay stale audio later.
	bool keep = atomic_load(&g_audioActive) && speed <= AUDIO_MAX_SPEED;
	int16_t chunk[512 * 2];
	for (int guard = 0; guard < 64; ++guard) {
		mAudioResamplerProcess(&g_resampler);
		size_t got = mAudioBufferRead(&g_resampled, chunk, 512);
		if (!got) {
			break;
		}
		if (keep) {
			ring_write(chunk, got);
		}
	}
}

// ---------------------------------------------------------------------------
// Video
// ---------------------------------------------------------------------------

static void publish_video(void) {
	unsigned width = 0;
	unsigned height = 0;
	g_core->currentVideoSize(g_core, &width, &height);
	if (width > g_stride) {
		width = g_stride;
	}
	if (height > EMU_MAX_HEIGHT) {
		height = EMU_MAX_HEIGHT;
	}
	unsigned slot = g_back;
	memcpy(g_slots[slot], g_renderBuf, (size_t) g_stride * height * sizeof(uint32_t));
	g_slotW[slot] = width;
	g_slotH[slot] = height;
	g_slotStride[slot] = g_stride;
	unsigned previous = atomic_exchange(&g_mid, slot | SLOT_FRESH);
	g_back = previous & 3u;
}

const uint32_t* emu_video_latest(unsigned* width, unsigned* height, unsigned* stride, bool* fresh) {
	bool isFresh = false;
	if (atomic_load(&g_mid) & SLOT_FRESH) {
		unsigned previous = atomic_exchange(&g_mid, g_front);
		g_front = previous & 3u;
		isFresh = true;
	}
	unsigned slot = g_front;
	if (fresh) {
		*fresh = isFresh;
	}
	*width = g_slotW[slot];
	*height = g_slotH[slot];
	*stride = g_slotStride[slot];
	if (!*width || !*height) {
		return NULL;
	}
	return g_slots[slot];
}

// ---------------------------------------------------------------------------
// Saves
// ---------------------------------------------------------------------------

static bool save_state_now(void) {
	char tmp[PATH_MAX + 8];
	snprintf(tmp, sizeof(tmp), "%s.tmp", g_statePath);
	struct VFile* vf = VFileOpen(tmp, O_CREAT | O_TRUNC | O_RDWR);
	if (!vf) {
		emu_log(EMU_LOG_ERROR, "cannot create %s: %s", tmp, strerror(errno));
		return false;
	}
	bool ok = mCoreSaveStateNamed(g_core, vf, SAVESTATE_SAVEDATA | SAVESTATE_RTC | SAVESTATE_METADATA);
	if (ok) {
		ok = vf->sync(vf, NULL, 0);
	}
	vf->close(vf);
	// Write-then-rename: a crash mid-write never destroys the previous state.
	if (ok && rename(tmp, g_statePath) != 0) {
		emu_log(EMU_LOG_ERROR, "cannot rename state: %s", strerror(errno));
		ok = false;
	}
	if (!ok) {
		unlink(tmp);
	}
	return ok;
}

static bool load_state_now(void) {
	struct VFile* vf = VFileOpen(g_statePath, O_RDONLY);
	if (!vf) {
		return false;
	}
	bool ok = mCoreLoadStateNamed(g_core, vf, SAVESTATE_RTC);
	vf->close(vf);
	return ok;
}

static bool write_file_atomic(const char* path, const void* data, size_t size) {
	char tmp[PATH_MAX + 8];
	snprintf(tmp, sizeof(tmp), "%s.tmp", path);
	struct VFile* vf = VFileOpen(tmp, O_CREAT | O_TRUNC | O_RDWR);
	if (!vf) {
		return false;
	}
	bool ok = vf->write(vf, data, size) == (ssize_t) size && vf->sync(vf, NULL, 0);
	vf->close(vf);
	if (ok && rename(tmp, path) != 0) {
		ok = false;
	}
	if (!ok) {
		unlink(tmp);
	}
	return ok;
}

static void* read_file(const char* path, size_t* size) {
	struct VFile* vf = VFileOpen(path, O_RDONLY);
	if (!vf) {
		return NULL;
	}
	ssize_t length = vf->size(vf);
	void* data = NULL;
	if (length > 0 && (size_t) length <= MAX_SAVE_FILE) {
		data = malloc((size_t) length);
		if (data && vf->read(vf, data, (size_t) length) != length) {
			free(data);
			data = NULL;
		}
	}
	vf->close(vf);
	*size = data ? (size_t) length : 0;
	return data;
}

static bool looks_like_rom(const uint8_t* data, size_t size) {
	return size > 0xB2 && data[3] == 0xEA && data[0xB2] == 0x96;
}

// Raw battery saves (.sav, .srm) are the same bytes in every emulator. The
// cartridge's save size wins: longer files (RetroArch pads to 128 KB, some
// emulators append RTC data) are cut, shorter ones are padded with 0xFF.
static bool restore_raw_save(const uint8_t* data, size_t size) {
	if (size > MAX_RAW_SAVE || looks_like_rom(data, size)) {
		return false;
	}
	if (g_core->platform(g_core) != mPLATFORM_GBA) {
		// Game Boy: write the whole file, RTC trailer included.
		return g_core->savedataRestore(g_core, data, size, true);
	}
	void* current = NULL;
	size_t expected = g_core->savedataClone(g_core, &current);
	free(current);
	if (!expected) {
		// Save type not known yet (the game has not touched its save chip):
		// mGBA works it out from the data when the game first accesses it.
		return g_core->savedataRestore(g_core, data, size, true);
	}
	uint8_t* buffer = malloc(expected);
	if (!buffer) {
		return false;
	}
	memset(buffer, 0xFF, expected);
	memcpy(buffer, data, size < expected ? size : expected);
	bool ok = g_core->savedataRestore(g_core, buffer, expected, true);
	free(buffer);
	return ok;
}

static void backup_current_save(void) {
	void* current = NULL;
	size_t size = g_core->savedataClone(g_core, &current);
	if (size && current) {
		if (!write_file_atomic(g_backupPath, current, size)) {
			emu_log(EMU_LOG_WARN, "cannot write save backup %s", g_backupPath);
		}
	}
	free(current);
}

static int import_save_now(const uint8_t* data, size_t size) {
	if (!data || !size || size > MAX_SAVE_FILE) {
		return EMU_ERR_BAD_SAVE;
	}
	bool ok = false;
	bool handled = false;
#ifdef M_CORE_GBA
	if (g_core->platform(g_core) == mPLATFORM_GBA) {
		struct VFile* vf = VFileFromConstMemory(data, size);
		if (vf) {
			// GameShark SP / SharkPort files carry the cartridge title and are
			// rejected when they belong to another game.
			if (GBASavedataSharkPortPayloadSize(vf) > 0) {
				backup_current_save();
				ok = GBASavedataImportSharkPort(g_core->board, vf, false);
				handled = true;
			} else if (GBASavedataGSVPayloadSize(vf) > 0) {
				backup_current_save();
				ok = GBASavedataImportGSV(g_core->board, vf, false);
				handled = true;
			}
			vf->close(vf);
		}
	}
#endif
	if (!handled) {
		if (size > MAX_RAW_SAVE || looks_like_rom(data, size)) {
			return EMU_ERR_BAD_SAVE;
		}
		backup_current_save();
		ok = restore_raw_save(data, size);
	}
	if (!ok) {
		return EMU_ERR_BAD_SAVE;
	}
	// Games read their save at boot.
	g_core->reset(g_core);
	return EMU_OK;
}

static int export_save_now(struct CommandArgs* args) {
	void* data = NULL;
	size_t size = g_core->savedataClone(g_core, &data);
	if (!size || !data) {
		free(data);
		return EMU_ERR_BAD_SAVE;
	}
	args->out = data;
	args->outSize = size;
	return EMU_OK;
}

// Adds the lines of one cheat (separated by newlines) to a set.
static bool fill_cheat(struct mCheatSet* set, const char* code) {
	bool any = false;
	const char* p = code;
	while (p && *p) {
		const char* end = strchr(p, '\n');
		size_t length = end ? (size_t) (end - p) : strlen(p);
		char line[256];
		if (length >= sizeof(line)) {
			return false;
		}
		memcpy(line, p, length);
		line[length] = '\0';
		// Trim spaces, tabs and CR.
		char* start = line;
		while (*start == ' ' || *start == '\t' || *start == '\r') {
			++start;
		}
		size_t trimmed = strlen(start);
		while (trimmed && (start[trimmed - 1] == ' ' || start[trimmed - 1] == '\t' || start[trimmed - 1] == '\r')) {
			start[--trimmed] = '\0';
		}
		if (trimmed) {
			// Type 0 is "autodetect" for both GBA and GB cheats.
			if (!mCheatAddLine(set, start, 0)) {
				return false;
			}
			any = true;
		}
		p = end ? end + 1 : NULL;
	}
	return any;
}

static void clear_cheats(struct mCheatDevice* device) {
	while (mCheatSetsSize(&device->cheats)) {
		struct mCheatSet* set = *mCheatSetsGetPointer(&device->cheats, 0);
		mCheatRemoveSet(device, set); // also undoes ROM patches
		mCheatSetDeinit(set);
	}
}

static int apply_cheats_now(const char* const* codes, size_t count) {
	struct mCheatDevice* device = g_core->cheatDevice ? g_core->cheatDevice(g_core) : NULL;
	if (!device) {
		return count ? 0 : -1;
	}
	clear_cheats(device);
	int firstRejected = -1;
	for (size_t i = 0; i < count; ++i) {
		struct mCheatSet* set = device->createSet(device, NULL);
		if (!set) {
			continue;
		}
		if (codes[i] && fill_cheat(set, codes[i])) {
			set->enabled = true;
			mCheatAddSet(device, set);
		} else {
			mCheatSetDeinit(set);
			if (firstRejected < 0) {
				firstRejected = (int) i;
			}
		}
	}
	return firstRejected;
}

static int validate_cheat_now(const char* code) {
	struct mCheatDevice* device = g_core->cheatDevice ? g_core->cheatDevice(g_core) : NULL;
	if (!device || !code) {
		return 0;
	}
	struct mCheatSet* set = device->createSet(device, NULL);
	if (!set) {
		return 0;
	}
	bool ok = fill_cheat(set, code);
	mCheatSetDeinit(set);
	return ok ? 1 : 0;
}

static int execute(enum Command cmd, struct CommandArgs* args) {
	switch (cmd) {
	case CMD_SAVE_STATE:
		return save_state_now() ? 1 : 0;
	case CMD_LOAD_STATE:
		return load_state_now() ? 1 : 0;
	case CMD_IMPORT_SAVE:
		return import_save_now(args->data, args->size);
	case CMD_EXPORT_SAVE:
		return export_save_now(args);
	case CMD_CHEATS_APPLY:
		return apply_cheats_now(args->strings, args->count);
	case CMD_CHEAT_VALIDATE:
		return validate_cheat_now(args->data);
	case CMD_NONE:
		break;
	}
	return 0;
}

// ---------------------------------------------------------------------------
// Emulation thread
// ---------------------------------------------------------------------------

static void apply_speed(void) {
	unsigned speed = atomic_load(&g_speed);
	if (speed == g_appliedSpeed) {
		return;
	}
	// When fast-forwarding, only draw one frame out of `speed`: the screen
	// still updates ~60 times per second and the CPU time goes to emulation.
	mCoreConfigSetIntValue(&g_core->config, "frameskip", (int) speed - 1);
	g_core->reloadConfigOption(g_core, "frameskip", NULL);
	g_appliedSpeed = speed;
}

static void run_frame(void) {
	apply_speed();
	g_core->setKeys(g_core, atomic_load_explicit(&g_keys, memory_order_relaxed));
	g_core->runFrame(g_core);
	publish_video();
	pump_audio();
	atomic_fetch_add_explicit(&g_frameCount, 1, memory_order_relaxed);
}

static void pace(uint64_t* next) {
	unsigned speed = atomic_load(&g_speed);
	*next += g_framePeriodNs / speed;
	if (speed <= AUDIO_MAX_SPEED && audio_driving()) {
		size_t target = (size_t) atomic_load(&g_outRate) * AUDIO_TARGET_MS / 1000u;
		for (int i = 0; i < 100 && ring_level() > target; ++i) {
			sleep_ns(1000000);
		}
		*next = now_ns();
		return;
	}
	uint64_t now = now_ns();
	if (*next + 100000000ull < now) {
		*next = now; // far behind (slow device, debugger): never fast-forward to catch up
		return;
	}
	if (*next > now) {
		sleep_ns(*next - now);
	}
}

static void* thread_main(void* arg) {
	(void) arg;
#if defined(__APPLE__)
	pthread_setname_np("emu-core");
#else
	pthread_setname_np(pthread_self(), "emu-core");
	// Same priority Android gives to display threads. Fails silently if not allowed.
	setpriority(PRIO_PROCESS, 0, -8);
#endif
	uint64_t next = now_ns();
	pthread_mutex_lock(&g_mutex);
	for (;;) {
		while (!g_stop && (g_paused || g_cmd != CMD_NONE)) {
			if (g_cmd != CMD_NONE) {
				g_cmdResult = execute(g_cmd, g_cmdArgs);
				g_cmd = CMD_NONE;
				g_cmdArgs = NULL;
				g_cmdDoneSeq = g_cmdSeq;
				pthread_cond_broadcast(&g_cond);
				continue;
			}
			g_idle = true;
			pthread_cond_broadcast(&g_cond);
			pthread_cond_wait(&g_cond, &g_mutex);
			next = now_ns();
		}
		if (g_stop) {
			break;
		}
		g_idle = false;
		pthread_mutex_unlock(&g_mutex);

		run_frame();
		pace(&next);

		pthread_mutex_lock(&g_mutex);
	}
	g_idle = true;
	pthread_cond_broadcast(&g_cond);
	pthread_mutex_unlock(&g_mutex);
	return NULL;
}

static void stop_thread(void) {
	pthread_mutex_lock(&g_mutex);
	if (!g_threadRunning) {
		pthread_mutex_unlock(&g_mutex);
		return;
	}
	g_stop = true;
	pthread_cond_broadcast(&g_cond);
	pthread_mutex_unlock(&g_mutex);

	pthread_join(g_thread, NULL);

	pthread_mutex_lock(&g_mutex);
	g_threadRunning = false;
	g_stop = false;
	g_paused = true;
	g_idle = true;
	g_cmd = CMD_NONE;
	g_cmdArgs = NULL;
	pthread_cond_broadcast(&g_cond);
	pthread_mutex_unlock(&g_mutex);
}

// Runs a command on the emulation thread (between two frames) and waits for
// it. Returns `failure` when no game is loaded or the thread stopped.
static int run_command(enum Command cmd, struct CommandArgs* args, int failure) {
	struct CommandArgs empty;
	if (!args) {
		memset(&empty, 0, sizeof(empty));
		args = &empty;
	}
	pthread_mutex_lock(&g_mutex);
	if (!g_core) {
		pthread_mutex_unlock(&g_mutex);
		return failure;
	}
	if (!g_threadRunning) {
		int result = execute(cmd, args);
		pthread_mutex_unlock(&g_mutex);
		return result;
	}
	while (g_cmd != CMD_NONE) {
		pthread_cond_wait(&g_cond, &g_mutex);
	}
	g_cmd = cmd;
	g_cmdArgs = args;
	unsigned seq = ++g_cmdSeq;
	pthread_cond_broadcast(&g_cond);
	while (g_cmdDoneSeq != seq && g_threadRunning) {
		pthread_cond_wait(&g_cond, &g_mutex);
	}
	int result = g_cmdDoneSeq == seq ? g_cmdResult : failure;
	pthread_mutex_unlock(&g_mutex);
	return result;
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

int emu_probe(const char* path) {
	install_logger();
	if (!path || access(path, R_OK) != 0) {
		return EMU_ERR_NOT_FOUND;
	}
	// mCoreFind looks inside .zip/.7z archives and recognizes ROMs by their
	// header, not by the file extension.
	struct mCore* core = mCoreFind(path);
	if (!core) {
		return EMU_ERR_NOT_A_GAME;
	}
	if (core->init(core)) {
		core->deinit(core);
	} else {
		free(core);
	}
	return EMU_OK;
}

int emu_load(const char* romPath, const char* saveDir, const char* stateDir) {
	install_logger();
	emu_unload();
	if (!romPath || !saveDir || !stateDir || access(romPath, R_OK) != 0) {
		return EMU_ERR_NOT_FOUND;
	}
	struct mCore* core = mCoreFind(romPath);
	if (!core) {
		return EMU_ERR_NOT_A_GAME;
	}
	if (!core->init(core)) {
		free(core);
		return EMU_ERR_LOAD_FAILED;
	}
	mCoreInitConfig(core, NULL);

	unsigned width = 0;
	unsigned height = 0;
	core->baseVideoSize(core, &width, &height);
	if (!width || !height || width > EMU_MAX_WIDTH || height > EMU_MAX_HEIGHT) {
		emu_log(EMU_LOG_ERROR, "unexpected video size %ux%u", width, height);
		destroy_core(core);
		return EMU_ERR_LOAD_FAILED;
	}
	g_stride = width;
	memset(g_renderBuf, 0, sizeof(g_renderBuf));
	core->setVideoBuffer(core, g_renderBuf, width);

	if (!mCoreLoadFile(core, romPath)) {
		destroy_core(core);
		return EMU_ERR_NOT_A_GAME;
	}

	uint32_t crc = 0;
	core->checksum(core, &crc, mCHECKSUM_CRC32);
	snprintf(g_gameId, sizeof(g_gameId), "%08X", crc);
	snprintf(g_savePath, sizeof(g_savePath), "%s/%08X.sav", saveDir, crc);
	snprintf(g_statePath, sizeof(g_statePath), "%s/%08X.ss1", stateDir, crc);
	char backupDir[PATH_MAX];
	snprintf(backupDir, sizeof(backupDir), "%s/backup", saveDir);
	mkdir(backupDir, 0700);
	snprintf(g_backupPath, sizeof(g_backupPath), "%s/%08X.sav", backupDir, crc);

	// mGBA maps the .sav file (MAP_SHARED): every in-game save lands in the
	// page cache immediately and survives the process being killed.
	struct VFile* save = VFileOpen(g_savePath, O_CREAT | O_RDWR);
	if (!save) {
		emu_log(EMU_LOG_WARN, "cannot open %s: %s", g_savePath, strerror(errno));
	} else if (!core->loadSave(core, save)) {
		save->close(save);
		emu_log(EMU_LOG_WARN, "core rejected save file %s", g_savePath);
	}

	struct mGameInfo info;
	memset(&info, 0, sizeof(info));
	core->getGameInfo(core, &info);
	snprintf(g_title, sizeof(g_title), "%s", info.title);
	for (size_t len = strlen(g_title); len && (g_title[len - 1] == ' ' || g_title[len - 1] == '\0'); --len) {
		g_title[len - 1] = '\0';
	}

	int32_t cycles = core->frameCycles(core);
	int32_t frequency = core->frequency(core);
	if (cycles > 0 && frequency > 0) {
		g_framePeriodNs = (uint64_t) cycles * 1000000000ull / (uint64_t) frequency;
	}

	core->reset(core);

	mAudioBufferInit(&g_resampled, 4096, 2);
	mAudioResamplerInit(&g_resampler, mINTERPOLATOR_SINC);
	g_audioReady = true;

	for (int i = 0; i < 3; ++i) {
		g_slotW[i] = 0;
		g_slotH[i] = 0;
	}
	atomic_store(&g_keys, 0);
	atomic_store(&g_frameCount, 0);
	atomic_store(&g_speed, 1);
	g_appliedSpeed = 1;
	g_core = core;
	emu_log(EMU_LOG_INFO, "loaded '%s' crc=%08X video=%ux%u", g_title, crc, width, height);
	return EMU_OK;
}

bool emu_is_loaded(void) {
	return g_core != NULL;
}

void emu_unload(void) {
	stop_thread();
	if (!g_core) {
		return;
	}
	g_core->unloadROM(g_core); // flushes and unmaps the save file
	destroy_core(g_core);
	g_core = NULL;
	if (g_audioReady) {
		mAudioResamplerDeinit(&g_resampler);
		mAudioBufferDeinit(&g_resampled);
		g_audioReady = false;
	}
	g_title[0] = '\0';
	g_gameId[0] = '\0';
	g_savePath[0] = '\0';
	g_statePath[0] = '\0';
	g_backupPath[0] = '\0';
}

void emu_start(void) {
	pthread_mutex_lock(&g_mutex);
	if (g_threadRunning || !g_core) {
		pthread_mutex_unlock(&g_mutex);
		return;
	}
	g_stop = false;
	g_paused = true;
	g_idle = false;
	g_cmd = CMD_NONE;
	if (pthread_create(&g_thread, NULL, thread_main, NULL) == 0) {
		g_threadRunning = true;
	} else {
		g_idle = true;
		emu_log(EMU_LOG_ERROR, "cannot start emulation thread");
	}
	pthread_mutex_unlock(&g_mutex);
}

void emu_set_paused(bool paused) {
	pthread_mutex_lock(&g_mutex);
	g_paused = paused;
	pthread_cond_broadcast(&g_cond);
	if (paused) {
		while (g_threadRunning && !g_idle) {
			pthread_cond_wait(&g_cond, &g_mutex);
		}
	}
	pthread_mutex_unlock(&g_mutex);
}

void emu_set_keys(uint32_t keys) {
	atomic_store_explicit(&g_keys, keys, memory_order_relaxed);
}

bool emu_save_state(void) {
	return run_command(CMD_SAVE_STATE, NULL, 0) == 1;
}

bool emu_load_state(void) {
	return run_command(CMD_LOAD_STATE, NULL, 0) == 1;
}

void emu_set_speed(unsigned speed) {
	if (speed < 1) {
		speed = 1;
	}
	if (speed > EMU_MAX_SPEED) {
		speed = EMU_MAX_SPEED;
	}
	atomic_store(&g_speed, speed);
}

unsigned emu_get_speed(void) {
	return atomic_load(&g_speed);
}

int emu_cheats_apply(const char* const* codes, size_t count) {
	struct CommandArgs args;
	memset(&args, 0, sizeof(args));
	args.strings = codes;
	args.count = count;
	return run_command(CMD_CHEATS_APPLY, &args, count ? 0 : -1);
}

bool emu_cheat_validate(const char* code) {
	struct CommandArgs args;
	memset(&args, 0, sizeof(args));
	args.data = code;
	return run_command(CMD_CHEAT_VALIDATE, &args, 0) == 1;
}

int emu_import_save(const uint8_t* data, size_t size) {
	struct CommandArgs args;
	memset(&args, 0, sizeof(args));
	args.data = data;
	args.size = size;
	int result = run_command(CMD_IMPORT_SAVE, &args, EMU_ERR_LOAD_FAILED);
	if (result == EMU_OK) {
		emu_flush_save();
	}
	return result;
}

uint8_t* emu_export_save(size_t* size) {
	struct CommandArgs args;
	memset(&args, 0, sizeof(args));
	int result = run_command(CMD_EXPORT_SAVE, &args, EMU_ERR_LOAD_FAILED);
	if (result != EMU_OK) {
		*size = 0;
		return NULL;
	}
	*size = args.outSize;
	return args.out;
}

bool emu_has_save_backup(void) {
	return g_core && g_backupPath[0] && access(g_backupPath, F_OK) == 0;
}

bool emu_restore_save_backup(void) {
	if (!emu_has_save_backup()) {
		return false;
	}
	size_t size = 0;
	void* data = read_file(g_backupPath, &size);
	if (!data) {
		return false;
	}
	// Importing backs up the current save first, so restoring twice undoes
	// the restore.
	int result = emu_import_save(data, size);
	free(data);
	return result == EMU_OK;
}

void emu_get_game_id(char* out, size_t size) {
	if (out && size) {
		snprintf(out, size, "%s", g_gameId);
	}
}

uint8_t emu_debug_read8(uint32_t address) {
	return g_core ? (uint8_t) g_core->busRead8(g_core, address) : 0;
}

bool emu_has_state(void) {
	return g_core && g_statePath[0] && access(g_statePath, F_OK) == 0;
}

void emu_flush_save(void) {
	if (!g_core || !g_savePath[0]) {
		return;
	}
	// fsync on any descriptor of the file also writes back the pages that
	// mGBA dirtied through its shared mapping.
	int fd = open(g_savePath, O_RDONLY);
	if (fd >= 0) {
		fsync(fd);
		close(fd);
	}
}

void emu_get_title(char* out, size_t size) {
	if (!out || !size) {
		return;
	}
	snprintf(out, size, "%s", g_title);
}

void emu_get_video_size(unsigned* width, unsigned* height) {
	*width = 0;
	*height = 0;
	if (g_core) {
		g_core->currentVideoSize(g_core, width, height);
	}
}

uint64_t emu_frame_count(void) {
	return atomic_load_explicit(&g_frameCount, memory_order_relaxed);
}
