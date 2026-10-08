// Minimal hardware definitions for Cometa. No external libraries.
#ifndef GBA_H
#define GBA_H

#include <stdint.h>

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef int8_t s8;
typedef int16_t s16;
typedef int32_t s32;

#define REG16(a) (*(volatile u16*) (a))
#define REG32(a) (*(volatile u32*) (a))

#define REG_DISPCNT   REG16(0x04000000)
#define REG_DISPSTAT  REG16(0x04000004)
#define REG_VCOUNT    REG16(0x04000006)
#define REG_BGCNT(n)  REG16(0x04000008 + 2 * (n))
#define REG_BGHOFS(n) REG16(0x04000010 + 4 * (n))
#define REG_BGVOFS(n) REG16(0x04000012 + 4 * (n))
#define REG_BLDCNT    REG16(0x04000050)
#define REG_BLDALPHA  REG16(0x04000052)
#define REG_BLDY      REG16(0x04000054)

#define REG_SOUND1CNT_L REG16(0x04000060)
#define REG_SOUND1CNT_H REG16(0x04000062)
#define REG_SOUND1CNT_X REG16(0x04000064)
#define REG_SOUND2CNT_L REG16(0x04000068)
#define REG_SOUND2CNT_H REG16(0x0400006C)
#define REG_SOUND3CNT_L REG16(0x04000070)
#define REG_SOUND3CNT_H REG16(0x04000072)
#define REG_SOUND3CNT_X REG16(0x04000074)
#define REG_SOUND4CNT_L REG16(0x04000078)
#define REG_SOUND4CNT_H REG16(0x0400007C)
#define REG_SOUNDCNT_L  REG16(0x04000080)
#define REG_SOUNDCNT_H  REG16(0x04000082)
#define REG_SOUNDCNT_X  REG16(0x04000084)
#define WAVE_RAM        ((volatile u32*) 0x04000090)

#define REG_DMA3SAD   REG32(0x040000D4)
#define REG_DMA3DAD   REG32(0x040000D8)
#define REG_DMA3CNT   REG32(0x040000DC)

#define REG_KEYINPUT  REG16(0x04000130)
#define REG_IE        REG16(0x04000200)
#define REG_IF        REG16(0x04000202)
#define REG_WAITCNT   REG16(0x04000204)
#define REG_IME       REG16(0x04000208)

#define BIOS_IRQ_HANDLER (*(void (**)(void)) 0x03007FFC)

#define PAL_BG   ((volatile u16*) 0x05000000)
#define PAL_OBJ  ((volatile u16*) 0x05000200)
#define VRAM     ((volatile u16*) 0x06000000)
#define OAM      ((volatile u16*) 0x07000000)
#define SRAM     ((volatile u8*) 0x0E000000)

#define CHARBLOCK(n)  ((volatile u16*) (0x06000000 + (n) * 0x4000))
#define SCREENBLOCK(n) ((volatile u16*) (0x06000000 + (n) * 0x800))
#define OBJ_TILES     ((volatile u16*) 0x06010000)

// DISPCNT
#define DCNT_MODE0  0x0000
#define DCNT_OBJ_1D 0x0040
#define DCNT_BG0    0x0100
#define DCNT_BG1    0x0200
#define DCNT_BG2    0x0400
#define DCNT_BG3    0x0800
#define DCNT_OBJ    0x1000

// Keys (REG_KEYINPUT is active low)
#define KEY_A      0x0001
#define KEY_B      0x0002
#define KEY_SELECT 0x0004
#define KEY_START  0x0008
#define KEY_RIGHT  0x0010
#define KEY_LEFT   0x0020
#define KEY_UP     0x0040
#define KEY_DOWN   0x0080
#define KEY_R      0x0100
#define KEY_L      0x0200
#define KEY_ANY    0x03FF

// OAM attributes
#define ATTR0_HIDE   0x0200
#define ATTR0_BLEND  0x0400
#define ATTR0_SQUARE 0x0000
#define ATTR0_WIDE   0x4000
#define ATTR0_TALL   0x8000
#define ATTR1_HFLIP  0x1000
#define ATTR1_VFLIP  0x2000
#define ATTR1_SIZE(n) ((n) << 14)
#define ATTR2_PRIO(n) ((n) << 10)
#define ATTR2_PAL(n)  ((n) << 12)

static inline void vblank_wait(void) {
    __asm__ volatile("swi 0x05" ::: "r0", "r1", "r2", "r3", "memory");
}

static inline void dma3_copy32(volatile void* dst, const void* src, u32 words) {
    REG_DMA3SAD = (u32) src;
    REG_DMA3DAD = (u32) dst;
    REG_DMA3CNT = words | 0x84000000; // enable, 32-bit
}

static inline void dma3_fill32(volatile void* dst, u32 value, u32 words) {
    static volatile u32 source;
    source = value;
    REG_DMA3SAD = (u32) &source;
    REG_DMA3DAD = (u32) dst;
    REG_DMA3CNT = words | 0x85000000; // enable, 32-bit, fixed source
}

#endif
