// Headless runner for QA and store screenshots: plays a ROM in mGBA with a
// scripted pad and saves frames as PPM.
//
//   capture <rom> <script> <outdir>
//
// Script lines:
//   run <frames> [keys]   keys: A B S(start) s(select) U D L R l r, e.g. "run 60 AR"
//   shot <name>           writes <outdir>/<name>.ppm
//   save                  prints the save data size and first bytes
//   record <name> / stop  writes the audio in between to <outdir>/<name>.wav
//   scan <frames> <every> runs and saves a shot every <every> frames, printing
//                         how many sprites of each kind are on screen (Cometa tiles)
#include <mgba/core/core.h>
#include <mgba/core/log.h>
#include <mgba-util/vfs.h>
#include <mgba-util/audio-buffer.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void quiet(struct mLogger* logger, int category, enum mLogLevel level, const char* format, va_list args) {
    (void) logger; (void) category;
    if (level & (mLOG_FATAL | mLOG_ERROR)) {
        vfprintf(stderr, format, args);
        fputc('\n', stderr);
    }
}
static struct mLogger logger = { .log = quiet, .filter = NULL };

static void write_ppm(const char* path, const uint32_t* buffer, unsigned w, unsigned h) {
    FILE* out = fopen(path, "wb");
    fprintf(out, "P6\n%u %u\n255\n", w, h);
    for (unsigned i = 0; i < w * h; i++) {
        uint32_t c = buffer[i];
        unsigned char rgb[3] = { (unsigned char) (c & 0xFF), (unsigned char) ((c >> 8) & 0xFF), (unsigned char) ((c >> 16) & 0xFF) };
        fwrite(rgb, 1, 3, out);
    }
    fclose(out);
}

// Counts visible sprites by tile range (see homebrew/cometa/src/assets.h).
static void sprite_stats(struct mCore* core, char* out, size_t size) {
    int bullets = 0, expl = 0, enemies = 0, boss = 0, shots = 0, power = 0;
    for (int i = 0; i < 128; i++) {
        uint32_t a0 = core->rawRead16(core, 0x07000000 + i * 8, -1);
        uint32_t a2 = core->rawRead16(core, 0x07000000 + i * 8 + 4, -1);
        if ((a0 & 0x0300) == 0x0200) continue;
        unsigned tile = a2 & 0x3FF;
        if (tile == 16) shots++;
        else if (tile >= 17 && tile < 19) bullets++;
        else if (tile >= 19 && tile < 43) enemies++;
        else if (tile >= 43 && tile < 51) power++;
        else if (tile >= 55 && tile < 71) enemies++;
        else if (tile >= 71 && tile < 191) expl++;
        else if (tile >= 191) boss++;
    }
    snprintf(out, size, "shots=%d bullets=%d enemies=%d expl=%d power=%d boss=%d", shots, bullets, enemies, expl, power, boss);
}

static uint32_t parse_keys(const char* s) {
    uint32_t k = 0;
    for (; s && *s; s++) {
        switch (*s) {
            case 'A': k |= 1; break;
            case 'B': k |= 2; break;
            case 's': k |= 4; break;
            case 'S': k |= 8; break;
            case 'R': k |= 16; break;
            case 'L': k |= 32; break;
            case 'U': k |= 64; break;
            case 'D': k |= 128; break;
            case 'r': k |= 256; break;
            case 'l': k |= 512; break;
        }
    }
    return k;
}

int main(int argc, char** argv) {
    if (argc < 4) {
        fprintf(stderr, "usage: capture <rom> <script> <outdir>\n");
        return 2;
    }
    mLogSetDefaultLogger(&logger);
    struct mCore* core = mCoreFind(argv[1]);
    if (!core || !core->init(core)) {
        fprintf(stderr, "not a game\n");
        return 1;
    }
    mCoreInitConfig(core, NULL);
    unsigned w, h;
    core->baseVideoSize(core, &w, &h);
    uint32_t* buffer = calloc(w * h, 4);
    core->setVideoBuffer(core, (void*) buffer, w);
    if (!mCoreLoadFile(core, argv[1])) {
        fprintf(stderr, "load failed\n");
        return 1;
    }
    struct VFile* save = VFileMemChunk(NULL, 0);
    core->loadSave(core, save);
    core->reset(core);

    FILE* script = fopen(argv[2], "r");
    if (!script) {
        perror(argv[2]);
        return 1;
    }
    char line[256];
    unsigned long total = 0;
    FILE* wav = NULL;
    unsigned long wav_frames = 0;
    unsigned rate = core->audioSampleRate(core);
    while (fgets(line, sizeof(line), script)) {
        char cmd[32] = { 0 }, arg[64] = { 0 }, keys[32] = { 0 };
        int n = sscanf(line, "%31s %63s %31s", cmd, arg, keys);
        if (n < 1 || cmd[0] == '#') continue;
        if (!strcmp(cmd, "run")) {
            int frames = atoi(arg);
            core->setKeys(core, n >= 3 ? parse_keys(keys) : 0);
            for (int i = 0; i < frames; i++) {
                core->runFrame(core);
                struct mAudioBuffer* audio = core->getAudioBuffer(core);
                if (audio && wav) {
                    int16_t samples[4096];
                    size_t got;
                    while ((got = mAudioBufferRead(audio, samples, 2048)) > 0) {
                        fwrite(samples, 4, got, wav);
                        wav_frames += got;
                    }
                } else if (audio) {
                    mAudioBufferClear(audio);
                }
            }
            total += frames;
        } else if (!strcmp(cmd, "shot")) {
            char path[512];
            snprintf(path, sizeof(path), "%s/%s.ppm", argv[3], arg);
            write_ppm(path, buffer, w, h);
            printf("shot %s at frame %lu\n", arg, total);
        } else if (!strcmp(cmd, "scan")) {
            int frames = atoi(arg), every = atoi(keys);
            core->setKeys(core, 0);
            for (int i = 1; i <= frames; i++) {
                core->runFrame(core);
                struct mAudioBuffer* audio = core->getAudioBuffer(core);
                if (audio) mAudioBufferClear(audio);
                total++;
                if (i % every == 0) {
                    char path[512], stats[160];
                    snprintf(path, sizeof(path), "%s/f%05lu.ppm", argv[3], total);
                    write_ppm(path, buffer, w, h);
                    sprite_stats(core, stats, sizeof(stats));
                    printf("f%05lu %s\n", total, stats);
                }
            }
        } else if (!strcmp(cmd, "record")) {
            char path[512];
            snprintf(path, sizeof(path), "%s/%s.wav", argv[3], arg);
            wav = fopen(path, "wb");
            fseek(wav, 44, SEEK_SET);
            wav_frames = 0;
        } else if (!strcmp(cmd, "stop") && wav) {
            uint32_t data = (uint32_t) (wav_frames * 4);
            uint32_t header[11] = { 0x46464952, 36 + data, 0x45564157, 0x20746d66, 16, 0x00020001, rate, rate * 4,
                                    0x00100004, 0x61746164, data };
            fseek(wav, 0, SEEK_SET);
            fwrite(header, 4, 11, wav);
            fclose(wav);
            wav = NULL;
            printf("audio: %lu frames at %u Hz\n", wav_frames, rate);
        } else if (!strcmp(cmd, "save")) {
            size_t size = save->size(save);
            unsigned char head[16] = { 0 };
            save->seek(save, 0, SEEK_SET);
            save->read(save, head, size < 16 ? size : 16);
            printf("save: %zu bytes:", size);
            for (int i = 0; i < 9; i++) printf(" %02X", head[i]);
            printf("\n");
        }
    }
    core->deinit(core);
    return 0;
}
