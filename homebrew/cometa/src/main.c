// Cometa: a small horizontal space shooter bundled with Play Again.
// Original code, art and music. Plain C, no libraries.
#include "gba.h"
#include "assets.h"
#include "sound.h"

#ifndef HI_DEFAULT
#define HI_DEFAULT 10000
#endif

#define FX(v) ((s32) ((v) * 256))
#define PX(v) ((v) >> 8)
#define SCREEN_W 240
#define SCREEN_H 160
#define TOP 8              // the HUD row
#define BOTTOM 160

#define SB_HUD 28
#define SB_NEAR 29
#define SB_NEBULA 30
#define SB_FAR 31

#define MAX_SHOTS 24
#define MAX_ENEMIES 20
#define MAX_EBULLETS 40
#define MAX_EXPL 24
#define MAX_POWER 3
#define MAX_SPAWNERS 3

enum { ST_TITLE, ST_PLAY, ST_GAMEOVER };
enum { E_DRONE, E_DIVER, E_ROCK, E_HEAVY };
enum { P_LINE, P_SINE, P_SWOOP_DOWN, P_SWOOP_UP };
enum { F_SINE, F_DIVE, F_ROCKS, F_HEAVY, F_SWOOP, F_COUNT };

typedef struct { s32 x, y, vx, vy; u8 active; } Shot;
typedef struct { s32 x, y, vx, vy; u8 active; } EBullet;
typedef struct {
    s32 x, y, vx, vy;
    s16 hp, base_y, fire_t;
    u16 t;
    u8 type, pattern, flash, active, phase;
} Enemy;
typedef struct { s16 x, y; u8 frame, timer, big, active; } Expl;
typedef struct { s32 x, y; u16 t; u8 active; } Power;
typedef struct { u8 active, type, pattern, count, interval, timer; s16 y; } Spawner;
typedef struct {
    s32 x, y;
    s16 hp, max_hp;
    u16 t;
    u8 active, flash, dying, phase;
    u16 attack_t;
} Boss;

// ---- State --------------------------------------------------------------------
static u16 oam_shadow[512] __attribute__((aligned(4)));
static int oam_used;

static u16 keys;            // what the ship obeys (pad or autopilot)
static u16 pad, pad_prev, keys_hit;
static u32 frame;
static u32 rng = 0x2F6E2B1;

static int state, demo, paused;
static u32 state_t;
static u32 score, hi_score, next_life;
static int lives, weapon, stage, formation, formation_t, warning_t, clear_t, banner_t;
static s32 px, py;
static int invuln, dead_t, fire_cd, player_alive;
static u32 kills;
static int flash_level, fade_level;
static u32 scroll_far, scroll_neb, scroll_near;
static int comet_x, comet_y;

static Shot shots[MAX_SHOTS];
static EBullet ebullets[MAX_EBULLETS];
static Enemy enemies[MAX_ENEMIES];
static Expl expls[MAX_EXPL];
static Power powers[MAX_POWER];
static Spawner spawners[MAX_SPAWNERS];
static Boss boss;

extern void irq_handler(void);

// ---- Helpers ------------------------------------------------------------------
static u32 rand32(void) {
    rng ^= rng << 13;
    rng ^= rng >> 17;
    rng ^= rng << 5;
    return rng;
}
static int rand_range(int lo, int hi) { return lo + (int) (rand32() % (u32) (hi - lo + 1)); }

static u32 isqrt(u32 n) {
    u32 root = 0, bit = 1u << 30;
    while (bit > n) bit >>= 2;
    while (bit) {
        if (n >= root + bit) {
            n -= root + bit;
            root = (root >> 1) + bit;
        } else {
            root >>= 1;
        }
        bit >>= 2;
    }
    return root;
}

static int difficulty(void) { return stage - 1; }

// ---- Sprites --------------------------------------------------------------------
enum { SZ_8, SZ_16, SZ_32, SZ_64, SZ_32x16 };

static void sprite(int x, int y, int size, int tile, int pal, int flags) {
    if (oam_used >= 128 || x <= -64 || x >= SCREEN_W || y <= -64 || y >= SCREEN_H) return;
    u16 a0 = (u16) (y & 0xFF), a1 = (u16) (x & 0x1FF);
    switch (size) {
        case SZ_8: break;
        case SZ_16: a1 |= ATTR1_SIZE(1); break;
        case SZ_32: a1 |= ATTR1_SIZE(2); break;
        case SZ_64: a1 |= ATTR1_SIZE(3); break;
        case SZ_32x16: a0 |= ATTR0_WIDE; a1 |= ATTR1_SIZE(2); break;
    }
    a1 |= (u16) flags;
    u16* o = &oam_shadow[oam_used * 4];
    o[0] = a0;
    o[1] = a1;
    o[2] = (u16) (tile | ATTR2_PRIO(1) | ATTR2_PAL(pal));
    oam_used++;
}

static void oam_finish(void) {
    for (int i = oam_used; i < 128; i++) oam_shadow[i * 4] = ATTR0_HIDE;
}

// ---- Text (BG0) ------------------------------------------------------------------
static void put_text(int col, int row, const char* s, int bank) {
    volatile u16* map = SCREENBLOCK(SB_HUD) + row * 32 + col;
    for (; *s; s++, map++) {
        char c = *s;
        *map = c == ' ' ? 0 : (u16) (font_map[(u8) c - 32] | (bank << 12));
    }
}

static void put_number(int col, int row, u32 value, int digits, int bank) {
    char buf[12];
    buf[digits] = 0;
    for (int i = digits - 1; i >= 0; i--) {
        buf[i] = (char) ('0' + value % 10);
        value /= 10;
    }
    put_text(col, row, buf, bank);
}

static void clear_row(int row) {
    volatile u16* map = SCREENBLOCK(SB_HUD) + row * 32;
    for (int i = 0; i < 32; i++) map[i] = 0;
}

static void clear_text(void) {
    for (int r = 0; r < 20; r++) clear_row(r);
}

static void center_text(int row, const char* s, int bank) {
    int n = 0;
    while (s[n]) n++;
    put_text((30 - n) / 2, row, s, bank);
}

static void draw_logo(int col, int row) {
    volatile u16* map = SCREENBLOCK(SB_HUD);
    for (int y = 0; y < LOGO_H; y++)
        for (int x = 0; x < LOGO_W; x++)
            map[(row + y) * 32 + col + x] = (u16) ((TILE_LOGO + y * LOGO_W + x) | (6 << 12));
}

static void draw_hud(void) {
    put_text(0, 0, "SCORE", 0);
    put_number(6, 0, score, 7, 1);
    put_text(14, 0, "HI", 0);
    put_number(17, 0, score > hi_score ? score : hi_score, 7, 1);
    volatile u16* map = SCREENBLOCK(SB_HUD);
    for (int i = 0; i < 5; i++) {
        map[25 + i] = i < lives - 1 ? (u16) (TILE_LIFE | (7 << 12)) : 0;
    }
    if (demo) put_text(13, 2, "DEMO", 2);
}

static void draw_boss_bar(void) {
    volatile u16* map = SCREENBLOCK(SB_HUD) + 19 * 32;
    if (!boss.active || boss.dying) {
        for (int i = 0; i < 32; i++) map[i] = 0;
        return;
    }
    put_text(1, 19, "BOSS", 3);
    const int segs = 20;
    int filled = (boss.hp * segs + boss.max_hp - 1) / boss.max_hp;
    map[6] = TILE_BAR_LEFT | (7 << 12);
    for (int i = 0; i < segs; i++) map[7 + i] = (u16) ((i < filled ? TILE_BAR_FULL : TILE_BAR_EMPTY) | (7 << 12));
    map[7 + segs] = TILE_BAR_RIGHT | (7 << 12);
}

// ---- Save (SRAM) ---------------------------------------------------------------------
static void load_hi_score(void) {
    u8 b[9];
    for (int i = 0; i < 9; i++) b[i] = SRAM[i];
    u8 sum = 0;
    for (int i = 0; i < 8; i++) sum += b[i];
    if (b[0] == 'C' && b[1] == 'M' && b[2] == 'T' && b[3] == '1' && sum == b[8]) {
        hi_score = b[4] | (b[5] << 8) | (b[6] << 16) | ((u32) b[7] << 24);
    } else {
        hi_score = HI_DEFAULT;
    }
}

static void save_hi_score(void) {
    u8 b[9] = { 'C', 'M', 'T', '1', (u8) hi_score, (u8) (hi_score >> 8), (u8) (hi_score >> 16), (u8) (hi_score >> 24), 0 };
    for (int i = 0; i < 8; i++) b[8] += b[i];
    for (int i = 0; i < 9; i++) SRAM[i] = b[i];
}

// ---- Effects -------------------------------------------------------------------------
static void explode(int x, int y, int big) {
    for (int i = 0; i < MAX_EXPL; i++) {
        if (!expls[i].active) {
            expls[i] = (Expl) { (s16) x, (s16) y, 0, 0, (u8) big, 1 };
            return;
        }
    }
}

static void add_score(u32 points) {
    if (demo) return;
    score += points;
    if (score > 9999999) score = 9999999;
    if (score >= next_life) {
        next_life += 30000;
        if (lives < 6) {
            lives++;
            sfx_power();
        }
    }
}

static void fire_enemy_bullet(s32 x, s32 y, int angle_offset, int speed) {
    for (int i = 0; i < MAX_EBULLETS; i++) {
        if (ebullets[i].active) continue;
        s32 dx = (px + FX(18)) - x;
        s32 dy = (py + FX(8)) - y;
        if (!player_alive) dx = -FX(100), dy = 0;
        s32 ax = dx >> 4, ay = dy >> 4;
        u32 len = isqrt((u32) (ax * ax + ay * ay));
        if (len == 0) len = 1;
        s32 vx = (s32) (ax * speed / (s32) len);
        s32 vy = (s32) (ay * speed / (s32) len);
        if (angle_offset) {
            // rotate by a small angle: (vx, vy) + offset * (-vy, vx)
            s32 nvx = vx - (vy * angle_offset >> 8);
            s32 nvy = vy + (vx * angle_offset >> 8);
            vx = nvx;
            vy = nvy;
        }
        ebullets[i] = (EBullet) { x - FX(4), y - FX(4), vx, vy, 1 };
        return;
    }
}

static void fire_ring(s32 x, s32 y, int count, int speed, int phase) {
    for (int k = 0; k < count; k++) {
        int a = (phase + k * 256 / count) & 255;
        for (int i = 0; i < MAX_EBULLETS; i++) {
            if (ebullets[i].active) continue;
            s32 vx = sin_table[(a + 64) & 255] * speed >> 8;
            s32 vy = sin_table[a] * speed >> 8;
            ebullets[i] = (EBullet) { x - FX(4), y - FX(4), vx, vy, 1 };
            break;
        }
    }
}

// ---- Spawning -----------------------------------------------------------------------
static Enemy* new_enemy(int type) {
    for (int i = 0; i < MAX_ENEMIES; i++) {
        if (!enemies[i].active) {
            Enemy* e = &enemies[i];
            *e = (Enemy) { 0 };
            e->active = 1;
            e->type = (u8) type;
            e->fire_t = (s16) rand_range(60, 200);
            return e;
        }
    }
    return 0;
}

static void spawn_one(Spawner* s, int index) {
    int d = difficulty();
    Enemy* e = new_enemy(s->type);
    if (!e) return;
    e->x = FX(244);
    e->pattern = s->pattern;
    switch (s->type) {
        case E_DRONE:
            e->hp = 1;
            e->vx = -FX(1.25) - d * 20;
            e->base_y = s->y;
            e->y = FX(s->y);
            if (s->pattern == P_SWOOP_DOWN) {
                e->y = FX(TOP + 4);
                e->vy = FX(0.9);
                e->vx = -FX(1.6) - d * 20;
            } else if (s->pattern == P_SWOOP_UP) {
                e->y = FX(BOTTOM - 20);
                e->vy = -FX(0.9);
                e->vx = -FX(1.6) - d * 20;
            }
            e->phase = (u8) (index * 22);
            break;
        case E_DIVER:
            e->hp = 1;
            e->vx = -FX(1.1) - d * 16;
            e->y = FX(s->y + (index & 1 ? 18 : -18) * (index / 2 + 0));
            e->phase = (u8) rand_range(140, 190);
            break;
        case E_ROCK:
            e->hp = 3;
            e->vx = -FX(0.5) - rand_range(0, 230) - d * 16;
            e->vy = rand_range(-60, 60);
            e->y = FX(rand_range(TOP + 4, BOTTOM - 20));
            break;
        case E_HEAVY:
            e->hp = (s16) (12 + d * 4);
            e->vx = -FX(0.5);
            e->y = FX(s->y);
            e->base_y = s->y;
            e->fire_t = 70;
            break;
    }
}

static void start_formation(int kind) {
    Spawner* s = 0;
    for (int i = 0; i < MAX_SPAWNERS; i++) {
        if (!spawners[i].active) {
            s = &spawners[i];
            break;
        }
    }
    if (!s) return;
    *s = (Spawner) { 0 };
    s->active = 1;
    switch (kind) {
        case F_SINE:
            s->type = E_DRONE;
            s->pattern = P_SINE;
            s->count = 5;
            s->interval = 14;
            s->y = (s16) rand_range(40, 120);
            break;
        case F_DIVE:
            s->type = E_DIVER;
            s->count = 4;
            s->interval = 18;
            s->y = (s16) rand_range(60, 100);
            break;
        case F_ROCKS:
            s->type = E_ROCK;
            s->count = 6;
            s->interval = 22;
            break;
        case F_HEAVY:
            s->type = E_HEAVY;
            s->count = 1;
            s->interval = 1;
            s->y = (s16) (rand32() & 1 ? 44 : 104);
            break;
        case F_SWOOP:
            s->type = E_DRONE;
            s->pattern = rand32() & 1 ? P_SWOOP_DOWN : P_SWOOP_UP;
            s->count = 6;
            s->interval = 12;
            break;
    }
}

static const u8 stage_plan[10] = { F_SINE, F_SWOOP, F_DIVE, F_ROCKS, F_SINE, F_HEAVY, F_SWOOP, F_DIVE, F_HEAVY, F_SINE };

static void update_spawners(void) {
    for (int i = 0; i < MAX_SPAWNERS; i++) {
        Spawner* s = &spawners[i];
        if (!s->active) continue;
        if (s->timer) {
            s->timer--;
            continue;
        }
        spawn_one(s, s->count);
        s->timer = s->interval;
        if (--s->count == 0) s->active = 0;
    }
}

static int enemies_alive(void) {
    for (int i = 0; i < MAX_ENEMIES; i++)
        if (enemies[i].active) return 1;
    for (int i = 0; i < MAX_SPAWNERS; i++)
        if (spawners[i].active) return 1;
    return 0;
}

// ---- Boss ------------------------------------------------------------------------------
static void boss_start(void) {
    boss = (Boss) { 0 };
    boss.active = 1;
    boss.max_hp = boss.hp = (s16) (110 + difficulty() * 40);
    boss.x = FX(250);
    boss.y = FX(48);
    music_play(SONG_BOSS);
}

static void boss_update(void) {
    if (!boss.active) return;
    boss.t++;
    if (boss.flash) boss.flash--;
    if (boss.dying) {
        if ((boss.t & 7) == 0) {
            explode(PX(boss.x) + rand_range(4, 60), PX(boss.y) + rand_range(4, 60), rand32() & 1);
            sfx_explode();
        }
        if (--boss.dying == 0) {
            explode(PX(boss.x) + 32, PX(boss.y) + 32, 1);
            explode(PX(boss.x) + 16, PX(boss.y) + 20, 1);
            explode(PX(boss.x) + 44, PX(boss.y) + 40, 1);
            sfx_big_explode();
            flash_level = 16;
            boss.active = 0;
            add_score(5000 * (u32) stage);
            clear_t = 240;
            music_play(SONG_CLEAR);
        }
        return;
    }
    // enter, then hover
    if (PX(boss.x) > 166) {
        boss.x -= FX(0.6);
    } else {
        boss.y = FX(48) + sin_table[(boss.t * 2) & 255] * 38;
    }
    int hp_part = boss.hp * 3 / boss.max_hp; // 2: calm, 1: angry, 0: furious
    int d = difficulty();
    boss.attack_t++;
    s32 cx = boss.x + FX(26), cy = boss.y + FX(32);
    if (PX(boss.x) <= 180) {
        int period = hp_part >= 2 ? 56 : hp_part == 1 ? 44 : 34;
        period -= d * 4;
        if (period < 22) period = 22;
        if (boss.attack_t % period == 0) {
            int speed = FX(1.5) + d * 24;
            if (hp_part >= 2) {
                fire_enemy_bullet(cx, cy, 0, speed);
                fire_enemy_bullet(cx, cy, 40, speed);
                fire_enemy_bullet(cx, cy, -40, speed);
            } else {
                fire_ring(cx, cy, hp_part == 1 ? 10 : 14, FX(1.2) + d * 16, (int) (boss.t * 5));
                fire_enemy_bullet(cx, cy, 0, speed + FX(0.4));
            }
        }
        if (hp_part == 0 && boss.attack_t % 150 == 75) {
            Spawner s = { 1, E_DRONE, P_SINE, 1, 1, 0, (s16) rand_range(30, 130) };
            spawn_one(&s, rand_range(0, 5));
        }
    }
}

// ---- Player ---------------------------------------------------------------------------
static void player_reset(void) {
    px = FX(24);
    py = FX(72);
    invuln = 120;
    player_alive = 1;
}

static void kill_player(void) {
    if (invuln || !player_alive) return;
    explode(PX(px) + 16, PX(py) + 8, 1);
    sfx_death();
    flash_level = 10;
    player_alive = 0;
    dead_t = 90;
    if (weapon > 1) weapon--;
}

static u16 autopilot(void) {
    u16 k = KEY_A;
    int cx = PX(px) + 18, cy = PX(py) + 8;
    int target_y = cy, threat = 0;
    int best = 999;
    for (int i = 0; i < MAX_POWER; i++) {
        if (powers[i].active) {
            target_y = PX(powers[i].y) + 6;
            best = 0;
        }
    }
    for (int i = 0; i < MAX_ENEMIES && best; i++) {
        Enemy* e = &enemies[i];
        if (!e->active || PX(e->x) < cx) continue;
        int dist = PX(e->x) - cx;
        if (dist < best) {
            best = dist;
            target_y = PX(e->y) + (e->type == E_HEAVY ? 16 : 8);
        }
    }
    if (boss.active && best > 120) target_y = PX(boss.y) + 32;
    for (int i = 0; i < MAX_EBULLETS; i++) {
        EBullet* b = &ebullets[i];
        if (!b->active) continue;
        int bx = PX(b->x) + 4, by = PX(b->y) + 4;
        int dx = bx - cx, dy = by - cy;
        if (dx > -10 && dx < 56 && dy > -18 && dy < 18) {
            threat = dy >= 0 ? -1 : 1;
            break;
        }
    }
    for (int i = 0; i < MAX_ENEMIES; i++) {
        Enemy* e = &enemies[i];
        if (!e->active) continue;
        int dx = PX(e->x) - cx, dy = PX(e->y) + 8 - cy;
        if (dx > -12 && dx < 30 && dy > -18 && dy < 18) threat = dy >= 0 ? -1 : 1;
    }
    if (threat < 0 || (!threat && target_y < cy - 3)) k |= KEY_UP;
    else if (threat > 0 || target_y > cy + 3) k |= KEY_DOWN;
    if (cx < 50) k |= KEY_RIGHT;
    else if (cx > 90 || threat) k |= KEY_LEFT;
    return k;
}

static void player_update(void) {
    if (!player_alive) {
        if (--dead_t == 0) {
            if (lives > 1 || demo) {
                if (!demo) lives--;
                player_reset();
            } else {
                lives = 0;
                state = ST_GAMEOVER;
                state_t = 0;
                music_play(SONG_GAMEOVER);
                clear_text();
                center_text(8, "GAME OVER", 3);
                if (score > hi_score) {
                    hi_score = score;
                    save_hi_score();
                    center_text(11, "NEW RECORD!", 1);
                }
                draw_hud();
            }
        }
        return;
    }
    if (invuln) invuln--;
    u16 k = keys;
    s32 speed = FX(1.6);
    s32 vx = 0, vy = 0;
    if (k & KEY_LEFT) vx = -speed;
    if (k & KEY_RIGHT) vx = speed;
    if (k & KEY_UP) vy = -speed;
    if (k & KEY_DOWN) vy = speed;
    if (vx && vy) {
        vx = vx * 181 >> 8;
        vy = vy * 181 >> 8;
    }
    px += vx;
    py += vy;
    if (px < FX(-4)) px = FX(-4);
    if (px > FX(SCREEN_W - 32)) px = FX(SCREEN_W - 32);
    if (py < FX(TOP)) py = FX(TOP);
    if (py > FX(BOTTOM - 16)) py = FX(BOTTOM - 16);

    if (fire_cd) fire_cd--;
    if ((k & (KEY_A | KEY_B)) && fire_cd == 0) {
        fire_cd = 7;
        int made = 0;
        for (int i = 0; i < MAX_SHOTS && made < weapon; i++) {
            if (shots[i].active) continue;
            s32 sy = py + FX(4);
            s32 svy = 0;
            if (weapon == 2) sy += made ? FX(3) : -FX(3);
            if (weapon == 3) svy = made == 0 ? 0 : made == 1 ? -FX(0.7) : FX(0.7);
            shots[i] = (Shot) { px + FX(22), sy, FX(5), svy, 1 };
            made++;
        }
        sfx_shot();
    }
}

// ---- Collisions -------------------------------------------------------------------------
static int overlap(int ax, int ay, int aw, int ah, int bx, int by, int bw, int bh) {
    return ax < bx + bw && bx < ax + aw && ay < by + bh && by < ay + ah;
}

static void enemy_box(const Enemy* e, int* x, int* y, int* w, int* h) {
    int ex = PX(e->x), ey = PX(e->y);
    switch (e->type) {
        case E_HEAVY: *x = ex + 3; *y = ey + 6; *w = 27; *h = 21; break;
        case E_ROCK: *x = ex + 3; *y = ey + 3; *w = 10; *h = 10; break;
        default: *x = ex + 2; *y = ey + 3; *w = 12; *h = 10; break;
    }
}

static void drop_power(s32 x, s32 y) {
    for (int i = 0; i < MAX_POWER; i++) {
        if (!powers[i].active) {
            powers[i] = (Power) { x, y, 0, 1 };
            return;
        }
    }
}

static void destroy_enemy(Enemy* e) {
    static const u16 points[] = { 100, 150, 50, 500 };
    e->active = 0;
    int big = e->type == E_HEAVY;
    explode(PX(e->x) + (big ? 16 : 8), PX(e->y) + (big ? 16 : 8), big);
    if (big) sfx_big_explode(); else sfx_explode();
    add_score(points[e->type]);
    kills++;
    if (big || kills % 14 == 0) drop_power(e->x + (big ? FX(8) : 0), e->y + (big ? FX(8) : 0));
}

static void collisions(void) {
    // player shots vs enemies and boss
    for (int i = 0; i < MAX_SHOTS; i++) {
        Shot* s = &shots[i];
        if (!s->active) continue;
        int sx = PX(s->x) + 2, sy = PX(s->y) + 2;
        for (int j = 0; j < MAX_ENEMIES; j++) {
            Enemy* e = &enemies[j];
            if (!e->active) continue;
            int x, y, w, h;
            enemy_box(e, &x, &y, &w, &h);
            if (overlap(sx, sy, 6, 4, x, y, w, h)) {
                s->active = 0;
                if (--e->hp <= 0) {
                    destroy_enemy(e);
                } else {
                    e->flash = 3;
                    sfx_hit();
                }
                break;
            }
        }
        if (s->active && boss.active && !boss.dying &&
            overlap(sx, sy, 6, 4, PX(boss.x) + 8, PX(boss.y) + 8, 52, 48)) {
            s->active = 0;
            boss.flash = 3;
            if (--boss.hp <= 0) {
                boss.dying = 100;
                for (int k = 0; k < MAX_EBULLETS; k++) ebullets[k].active = 0;
            } else if ((boss.hp & 3) == 0) {
                sfx_hit();
            }
            if (!demo) score += 10;
        }
    }
    if (!player_alive) return;
    int hx = PX(px) + 14, hy = PX(py) + 6;
    // player vs enemy bullets
    for (int i = 0; i < MAX_EBULLETS; i++) {
        EBullet* b = &ebullets[i];
        if (b->active && overlap(hx, hy, 8, 4, PX(b->x) + 2, PX(b->y) + 2, 4, 4)) {
            b->active = 0;
            kill_player();
            return;
        }
    }
    // player vs enemies
    for (int j = 0; j < MAX_ENEMIES; j++) {
        Enemy* e = &enemies[j];
        if (!e->active) continue;
        int x, y, w, h;
        enemy_box(e, &x, &y, &w, &h);
        if (overlap(hx - 4, hy - 1, 14, 6, x, y, w, h)) {
            if (e->type != E_HEAVY) destroy_enemy(e);
            kill_player();
            return;
        }
    }
    if (boss.active && !boss.dying && overlap(hx, hy, 8, 4, PX(boss.x) + 10, PX(boss.y) + 12, 48, 40)) {
        kill_player();
    }
    // power-ups
    for (int i = 0; i < MAX_POWER; i++) {
        Power* p = &powers[i];
        if (p->active && overlap(PX(px) + 4, PX(py) + 2, 26, 12, PX(p->x) + 2, PX(p->y) + 2, 12, 10)) {
            p->active = 0;
            sfx_power();
            if (weapon < 3) weapon++;
            else add_score(1000);
        }
    }
}

// ---- Updates ------------------------------------------------------------------------------
static void update_enemies(void) {
    int d = difficulty();
    for (int i = 0; i < MAX_ENEMIES; i++) {
        Enemy* e = &enemies[i];
        if (!e->active) continue;
        e->t++;
        if (e->flash) e->flash--;
        switch (e->type) {
            case E_DRONE:
                if (e->pattern == P_SINE) {
                    e->y = FX(e->base_y) + sin_table[(e->t * 3 + e->phase) & 255] * 26;
                } else {
                    e->vy -= e->pattern == P_SWOOP_DOWN ? 4 : -4;
                    if (e->vy > FX(1.2)) e->vy = FX(1.2);
                    if (e->vy < -FX(1.2)) e->vy = -FX(1.2);
                    e->y += e->vy;
                }
                e->x += e->vx;
                break;
            case E_DIVER:
                if (!e->phase || PX(e->x) > e->phase) {
                    e->x += e->vx;
                } else {
                    if (e->vy == 0) {
                        s32 dy = (py + FX(8)) - (e->y + FX(8));
                        e->vy = dy / 48;
                        if (e->vy > FX(1.6)) e->vy = FX(1.6);
                        if (e->vy < -FX(1.6)) e->vy = -FX(1.6);
                        if (e->vy == 0) e->vy = 1;
                        e->vx = -FX(2.6) - d * 24;
                    }
                    e->x += e->vx;
                    e->y += e->vy;
                }
                break;
            case E_ROCK:
                e->x += e->vx;
                e->y += e->vy;
                if (e->y < FX(TOP) || e->y > FX(BOTTOM - 16)) e->vy = -e->vy;
                break;
            case E_HEAVY:
                if (PX(e->x) > 176) e->x += e->vx;
                else e->x -= FX(0.12);
                e->y = FX(e->base_y) + sin_table[(e->t * 2) & 255] * 10;
                break;
        }
        // shooting
        if (PX(e->x) < 236 && PX(e->x) > 40 && e->type != E_ROCK) {
            if (--e->fire_t <= 0) {
                int speed = FX(1.3) + d * 26;
                s32 cx = e->x + FX(e->type == E_HEAVY ? 4 : 6), cy = e->y + FX(e->type == E_HEAVY ? 16 : 8);
                if (e->type == E_HEAVY) {
                    fire_enemy_bullet(cx, cy, 0, speed);
                    fire_enemy_bullet(cx, cy, 36, speed);
                    fire_enemy_bullet(cx, cy, -36, speed);
                    e->fire_t = (s16) (84 - d * 8 > 40 ? 84 - d * 8 : 40);
                } else {
                    if (!demo || (rand32() & 1)) fire_enemy_bullet(cx, cy, 0, speed);
                    int base = 240 - d * 30;
                    if (base < 90) base = 90;
                    e->fire_t = (s16) rand_range(base, base + 140);
                }
            }
        }
        int x = PX(e->x), y = PX(e->y);
        if (x < -40 || y < -40 || y > SCREEN_H + 24) e->active = 0;
    }
}

static void update_bullets(void) {
    for (int i = 0; i < MAX_SHOTS; i++) {
        Shot* s = &shots[i];
        if (!s->active) continue;
        s->x += s->vx;
        s->y += s->vy;
        if (PX(s->x) > SCREEN_W || PX(s->y) < TOP - 8 || PX(s->y) > SCREEN_H) s->active = 0;
    }
    for (int i = 0; i < MAX_EBULLETS; i++) {
        EBullet* b = &ebullets[i];
        if (!b->active) continue;
        b->x += b->vx;
        b->y += b->vy;
        int x = PX(b->x), y = PX(b->y);
        if (x < -8 || x > SCREEN_W || y < TOP - 8 || y > SCREEN_H) b->active = 0;
    }
    for (int i = 0; i < MAX_POWER; i++) {
        Power* p = &powers[i];
        if (!p->active) continue;
        p->t++;
        p->x -= FX(0.45);
        p->y += sin_table[(p->t * 4) & 255] >> 2;
        if (PX(p->x) < -16) p->active = 0;
    }
    for (int i = 0; i < MAX_EXPL; i++) {
        Expl* e = &expls[i];
        if (!e->active) continue;
        if (++e->timer >= (e->big ? 5 : 4)) {
            e->timer = 0;
            if (++e->frame >= (e->big ? SPR_BIGEXPL_FRAMES : SPR_EXPL_FRAMES)) e->active = 0;
        }
    }
}

static void update_stage(void) {
    if (banner_t) {
        if (banner_t == 120) {
            put_text(11, 9, "STAGE", 0);
            put_number(17, 9, (u32) stage, 1, 1);
        }
        if (--banner_t == 0) clear_row(9);
    }
    if (warning_t) {
        if (--warning_t == 0) {
            clear_row(9);
            boss_start();
        } else if ((warning_t & 31) == 0) {
            sfx_warning();
        }
        if (warning_t) {
            if (warning_t & 16) center_text(9, "WARNING", 3);
            else clear_row(9);
        }
        return;
    }
    if (clear_t) {
        if (clear_t == 230) {
            center_text(8, "STAGE CLEAR", 1);
            put_text(9, 10, "BONUS", 0);
            put_number(15, 10, 5000 * (u32) stage, 6, 1);
        }
        if (--clear_t == 0) {
            clear_row(8);
            clear_row(10);
            stage++;
            formation = 0;
            formation_t = 90;
            banner_t = 120;
            dma3_copy32(PAL_BG + 5 * 16, nebula_palettes + ((stage - 1) % 3) * 16, 8);
            music_play(SONG_MAIN);
        }
        return;
    }
    if (boss.active) return;
    if (formation_t) {
        formation_t--;
        return;
    }
    if (formation < 10) {
        start_formation(stage_plan[formation]);
        if (stage >= 2 && (formation == 4 || formation == 8)) start_formation(F_ROCKS);
        formation++;
        int gap = 190 - difficulty() * 15;
        formation_t = gap < 110 ? 110 : gap;
    } else if (!enemies_alive()) {
        warning_t = 150;
        music_stop();
    }
}

// ---- Drawing --------------------------------------------------------------------------------
static int enemy_pal(void) { return stage % 3 == 2 ? 2 : stage % 3 == 0 ? 3 : 0; }

static void draw_sprites(void) {
    oam_used = 0;
    // enemy bullets first: they must stay visible above everything
    for (int i = 0; i < MAX_EBULLETS; i++) {
        EBullet* b = &ebullets[i];
        if (b->active) sprite(PX(b->x), PX(b->y), SZ_8, SPR_EBULLET + ((frame >> 3) & 1), 0, 0);
    }
    if (player_alive && !(invuln && (frame & 4))) {
        sprite(PX(px), PX(py), SZ_32x16, SPR_PLAYER + ((frame >> 2) & 1) * SPR_PLAYER_TILES, 0, 0);
    }
    for (int i = 0; i < MAX_POWER; i++) {
        Power* p = &powers[i];
        if (p->active) sprite(PX(p->x), PX(p->y), SZ_16, SPR_POWER + ((p->t >> 3) & 1) * SPR_POWER_TILES, 0, 0);
    }
    for (int i = 0; i < MAX_EXPL; i++) {
        Expl* e = &expls[i];
        if (!e->active) continue;
        if (e->big) sprite(e->x - 16, e->y - 16, SZ_32, SPR_BIGEXPL + e->frame * SPR_BIGEXPL_TILES, 0, 0);
        else sprite(e->x - 8, e->y - 8, SZ_16, SPR_EXPL + e->frame * SPR_EXPL_TILES, 0, 0);
    }
    for (int i = 0; i < MAX_SHOTS; i++) {
        Shot* s = &shots[i];
        if (s->active) sprite(PX(s->x), PX(s->y), SZ_8, SPR_SHOT, 0, 0);
    }
    int epal = enemy_pal();
    for (int i = 0; i < MAX_ENEMIES; i++) {
        Enemy* e = &enemies[i];
        if (!e->active) continue;
        int pal = e->flash ? 1 : epal;
        int x = PX(e->x), y = PX(e->y);
        switch (e->type) {
            case E_DRONE:
                sprite(x, y, SZ_16, SPR_DRONE + ((e->t >> 3) & 1) * SPR_DRONE_TILES, pal, 0);
                break;
            case E_DIVER:
                sprite(x, y, SZ_16, SPR_DIVER + ((e->t >> 2) & 1) * SPR_DIVER_TILES, pal, 0);
                break;
            case E_ROCK: {
                int f = (e->t >> 4) & 3;
                int flags = (f & 2) ? (ATTR1_HFLIP | ATTR1_VFLIP) : 0;
                sprite(x, y, SZ_16, SPR_ROCK + (f & 1) * SPR_ROCK_TILES, e->flash ? 1 : 0, flags);
                break;
            }
            case E_HEAVY:
                sprite(x, y, SZ_32, SPR_HEAVY, pal, 0);
                break;
        }
    }
    if (boss.active) {
        int bx = PX(boss.x), by = PX(boss.y);
        if (!(boss.dying && (frame & 2))) sprite(bx, by, SZ_64, SPR_BOSS, boss.flash ? 1 : epal, 0);
    }
    if (state == ST_TITLE) sprite(comet_x, comet_y, SZ_16, SPR_COMET, 0, 0);
    oam_finish();
}

// ---- Game flow --------------------------------------------------------------------------------
static void reset_world(void) {
    for (int i = 0; i < MAX_SHOTS; i++) shots[i].active = 0;
    for (int i = 0; i < MAX_EBULLETS; i++) ebullets[i].active = 0;
    for (int i = 0; i < MAX_ENEMIES; i++) enemies[i].active = 0;
    for (int i = 0; i < MAX_EXPL; i++) expls[i].active = 0;
    for (int i = 0; i < MAX_POWER; i++) powers[i].active = 0;
    for (int i = 0; i < MAX_SPAWNERS; i++) spawners[i].active = 0;
    boss.active = 0;
}

static void go_title(void) {
    state = ST_TITLE;
    state_t = 0;
    demo = 0;
    paused = 0;
    reset_world();
    player_alive = 0;
    clear_text();
    draw_logo((30 - LOGO_W) / 2, 4);
    center_text(10, "PRESS START", 0);
    put_text(9, 13, "HI", 0);
    put_number(12, 13, hi_score, 7, 1);
    center_text(16, "A FIRE   START PAUSE", 2);
    center_text(18, "2026 PLAY AGAIN", 0);
    dma3_copy32(PAL_BG + 5 * 16, nebula_palettes, 8);
    comet_x = -20;
    comet_y = 30;
    music_play(SONG_MAIN);
}

static void start_game(int is_demo) {
    state = ST_PLAY;
    state_t = 0;
    demo = is_demo;
    paused = 0;
    reset_world();
    clear_text();
    score = 0;
    next_life = 30000;
    lives = 3;
    weapon = is_demo ? 2 : 1;
    stage = 1;
    kills = 0;
    formation = 0;
    formation_t = 60;
    warning_t = 0;
    clear_t = 0;
    banner_t = is_demo ? 0 : 120;
    player_reset();
    dma3_copy32(PAL_BG + 5 * 16, nebula_palettes, 8);
    draw_hud();
    if (!is_demo) sfx_select();
    music_play(SONG_MAIN);
}

static void title_update(void) {
    state_t++;
    if ((state_t & 31) == 0) clear_row(10);
    else if ((state_t & 31) == 12) center_text(10, "PRESS START", 0);
    comet_x += 2;
    if ((state_t & 3) == 0) comet_y += (comet_x & 64) ? 1 : -1;
    if (comet_x > 300) {
        comet_x = -20;
        comet_y = rand_range(24, 120);
    }
#ifdef AUTOPILOT
    if (state_t == 30) {
        rng ^= 0x1234567;
        start_game(0);
        return;
    }
#endif
    if (keys_hit & (KEY_START | KEY_A)) {
        rng ^= frame * 2654435761u;
        start_game(0);
    } else if (state_t > 600) {
        start_game(1);
    }
}

static void play_update(void) {
    state_t++;
    if (demo) {
        if ((keys_hit & KEY_ANY) || state_t > 1500) {
            go_title();
            return;
        }
        keys = autopilot();
    } else {
#ifdef AUTOPILOT
        keys = autopilot();
        if (lives < 2) lives = 3;
#endif
        if (keys_hit & KEY_START) {
            paused = !paused;
            snd_mute(paused);
            if (paused) center_text(9, "PAUSE", 0);
            else clear_row(9);
        }
        if (paused) return;
    }
    update_stage();
    update_spawners();
    player_update();
    if (state != ST_PLAY) return;
    update_enemies();
    boss_update();
    update_bullets();
    collisions();
    if ((frame & 3) == 0 || state_t < 3) draw_hud();
    draw_boss_bar();
}

static void gameover_update(void) {
    state_t++;
    update_enemies();
    boss_update();
    update_bullets();
    if (state_t > 90 && (keys_hit & (KEY_START | KEY_A))) go_title();
    if (state_t > 600) go_title();
}

// ---- Video setup -------------------------------------------------------------------------------
static void video_init(void) {
    REG_DISPCNT = 0x0080; // forced blank while loading
    dma3_copy32(PAL_BG, bg_palette, sizeof(bg_palette) / 4);
    dma3_copy32(PAL_OBJ, obj_palette, sizeof(obj_palette) / 4);
    dma3_copy32(CHARBLOCK(0), bg0_tiles, sizeof(bg0_tiles) / 4);
    dma3_copy32(CHARBLOCK(1), star_tiles, sizeof(star_tiles) / 4);
    dma3_copy32(CHARBLOCK(2), nebula_tiles, sizeof(nebula_tiles) / 4);
    dma3_copy32(OBJ_TILES, obj_tiles, sizeof(obj_tiles) / 4);
    dma3_fill32(SCREENBLOCK(SB_HUD), 0, 512);
    dma3_copy32(SCREENBLOCK(SB_NEAR), near_map, 512);
    dma3_copy32(SCREENBLOCK(SB_NEBULA), nebula_map, 512);
    dma3_copy32(SCREENBLOCK(SB_FAR), far_map, 512);
    // BG0 HUD (prio 0), BG1 near stars (2), BG2 nebula (3), BG3 far stars (3)
    REG_BGCNT(0) = 0 | (0 << 2) | (SB_HUD << 8);
    REG_BGCNT(1) = 2 | (1 << 2) | (SB_NEAR << 8);
    REG_BGCNT(2) = 3 | (2 << 2) | (SB_NEBULA << 8);
    REG_BGCNT(3) = 3 | (1 << 2) | (SB_FAR << 8);
    for (int i = 0; i < 128; i++) oam_shadow[i * 4] = ATTR0_HIDE;
    dma3_copy32(OAM, oam_shadow, 256);
    REG_DISPCNT = DCNT_MODE0 | DCNT_OBJ_1D | DCNT_BG0 | DCNT_BG1 | DCNT_BG2 | DCNT_BG3 | DCNT_OBJ;
}

static const u16 twinkle[4] = { 0x7FFF, 0x7BAE, 0x3B3F, 0x62D2 };

static void vblank_work(void) {
    dma3_copy32(OAM, oam_shadow, 256);
    REG_BGHOFS(1) = (u16) (scroll_near >> 8);
    REG_BGHOFS(2) = (u16) (scroll_neb >> 8);
    REG_BGHOFS(3) = (u16) (scroll_far >> 8);
    if ((frame & 7) == 0) PAL_BG[4 * 16 + 5] = twinkle[(frame >> 3) & 3];
    int level = flash_level > fade_level ? flash_level : fade_level;
    if (fade_level) {
        REG_BLDCNT = 0x00FF;           // darken everything
        REG_BLDY = (u16) fade_level;
    } else if (level) {
        REG_BLDCNT = 0x00BF;           // brighten everything
        REG_BLDY = (u16) level;
    } else {
        REG_BLDCNT = 0;
    }
}

int main(void) {
    REG_WAITCNT = 0x4317;
    video_init();
    snd_init();
    BIOS_IRQ_HANDLER = irq_handler;
    REG_DISPSTAT = 0x0008;
    REG_IE = 0x0001;
    REG_IME = 1;
    load_hi_score();
    fade_level = 16;
    go_title();

    while (1) {
        vblank_wait();
        vblank_work();
        snd_update();
        frame++;
        pad_prev = pad;
        pad = (u16) (~REG_KEYINPUT & KEY_ANY);
        keys_hit = pad & (u16) ~pad_prev;
        keys = pad;

        if (fade_level) fade_level--;
        if (flash_level) flash_level--;
        if (!paused) {
            scroll_far += 64;
            scroll_neb += 32;
            scroll_near += 192;
        }
        switch (state) {
            case ST_TITLE: title_update(); break;
            case ST_PLAY: play_update(); break;
            case ST_GAMEOVER: gameover_update(); break;
        }
        draw_sprites();
    }
}
