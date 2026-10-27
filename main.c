#include <pspctrl.h>
#include <pspdisplay.h>
#include <pspgu.h>
#include <pspuser.h>
#include <pspkernel.h>
#include <string.h>

PSP_MODULE_INFO("Dash Homebrew", 0, 1, 0);
PSP_MAIN_THREAD_ATTR(THREAD_ATTR_USER);

#define BUF_W 512
#define SCREEN_W 480
#define SCREEN_H 272
#define GROUND_Y 226.0f
#define PLAYER_X 92.0f
#define PLAYER_SIZE 24.0f
#define LEVEL_LENGTH 4800.0f

static unsigned int display_list[0x40000] __attribute__((aligned(64)));
static int running = 1;
static int game_over = 0;
static int won = 0;
static float world_x = 0.0f;
static float player_y = GROUND_Y - PLAYER_SIZE;
static float player_vy = 0.0f;
static int grounded = 1;
static int jump_was_down = 0;
static int square_was_down = 0;
static int show_hitboxes = 0;
static unsigned int frames = 0;

// Level data: each item is a distance from the start, in PSP screen pixels.
// Add or move entries to make your own obstacle pattern.
static const float spikes[] = {
    430, 650, 680, 930, 1190, 1220, 1450, 1730, 1760,
    2030, 2290, 2320, 2570, 2850, 2880, 3160, 3420,
    3450, 3710, 3980, 4010, 4280, 4510
};
#define SPIKE_COUNT (sizeof(spikes) / sizeof(spikes[0]))

static int exit_callback(int arg1, int arg2, void *common) {
    (void)arg1; (void)arg2; (void)common;
    running = 0;
    return 0;
}

static int callback_thread(SceSize args, void *argp) {
    (void)args; (void)argp;
    int cbid = sceKernelCreateCallback("Exit Callback", exit_callback, NULL);
    if (cbid >= 0) sceKernelRegisterExitCallback(cbid);
    sceKernelSleepThreadCB();
    return 0;
}

static void setup_callbacks(void) {
    int thid = sceKernelCreateThread("update_thread", callback_thread, 0x11, 0xFA0, 0, 0);
    if (thid >= 0) sceKernelStartThread(thid, 0, 0);
}

typedef struct { short x, y, z; } Vertex;

static void draw_rect(float x, float y, float w, float h, unsigned int color) {
    Vertex *v = (Vertex *)sceGuGetMemory(2 * sizeof(Vertex));
    v[0].x = (short)x;     v[0].y = (short)y;     v[0].z = 0;
    v[1].x = (short)(x+w); v[1].y = (short)(y+h); v[1].z = 0;
    sceGuColor(color);
    sceGuDrawArray(GU_SPRITES, GU_VERTEX_16BIT | GU_TRANSFORM_2D, 2, 0, v);
}

static void draw_spike(float x, float base_y, float w, float h, unsigned int color) {
    Vertex *v = (Vertex *)sceGuGetMemory(3 * sizeof(Vertex));
    v[0].x = (short)x;       v[0].y = (short)base_y;    v[0].z = 0;
    v[1].x = (short)(x+w/2); v[1].y = (short)(base_y-h);v[1].z = 0;
    v[2].x = (short)(x+w);   v[2].y = (short)base_y;    v[2].z = 0;
    sceGuColor(color);
    sceGuDrawArray(GU_TRIANGLES, GU_VERTEX_16BIT | GU_TRANSFORM_2D, 3, 0, v);
}

static void init_gu(void) {
    sceGuInit();
    sceGuStart(GU_DIRECT, display_list);
    sceGuDrawBuffer(GU_PSM_8888, (void *)0, BUF_W);
    sceGuDispBuffer(SCREEN_W, SCREEN_H, (void *)0x88000, BUF_W);
    sceGuOffset(2048 - SCREEN_W / 2, 2048 - SCREEN_H / 2);
    sceGuViewport(2048, 2048, SCREEN_W, SCREEN_H);
    sceGuEnable(GU_SCISSOR_TEST);
    sceGuScissor(0, 0, SCREEN_W, SCREEN_H);
    sceGuDisable(GU_DEPTH_TEST);
    sceGuFinish();
    sceGuSync(0, 0);
    sceGuDisplay(GU_TRUE);
}

static void reset_game(void) {
    game_over = 0;
    won = 0;
    world_x = 0.0f;
    player_y = GROUND_Y - PLAYER_SIZE;
    player_vy = 0.0f;
    grounded = 1;
    frames = 0;
}

static void draw_frame(void) {
    sceGuStart(GU_DIRECT, display_list);
    sceGuClearColor(0xFF191820);
    sceGuClear(GU_COLOR_BUFFER_BIT);

    // Minimal parallax-like sky accents and floor, inspired by the clean readability of the 3DS demake.
    draw_rect(0, 0, SCREEN_W, GROUND_Y, 0xFF211A30);
    draw_rect(0, GROUND_Y, SCREEN_W, SCREEN_H - GROUND_Y, 0xFF29243A);
    draw_rect(0, GROUND_Y, SCREEN_W, 3, 0xFFFFB84A);
    for (int i = 0; i < 8; ++i) {
        float x = (float)((i * 83 - ((int)world_x % 83) + 480) % 480);
        draw_rect(x, 44 + (i % 3) * 28, 3, 3, 0xFF55486E);
    }

    // Fixed obstacle set keeps the prototype light and predictable on PSP hardware.
    for (unsigned int i = 0; i < SPIKE_COUNT; ++i) {
        float sx = PLAYER_X + (spikes[i] - world_x);
        if (sx > -32 && sx < SCREEN_W + 32) draw_spike(sx, GROUND_Y, 30, 29, 0xFF55D9F5);
    }

    if (!game_over && !won) {
        if (show_hitboxes) draw_rect(PLAYER_X + 3, player_y + 3, PLAYER_SIZE - 6, PLAYER_SIZE - 5, 0x5533FF33);
        draw_rect(PLAYER_X, player_y, PLAYER_SIZE, PLAYER_SIZE, 0xFF55E7A5);
        draw_rect(PLAYER_X + 6, player_y + 6, 5, 5, 0xFFFFFFFF);
        draw_rect(PLAYER_X + 15, player_y + 6, 5, 5, 0xFFFFFFFF);
        draw_rect(PLAYER_X + 7, player_y + 16, 11, 3, 0xFF302643);
    }

    // Progress bar. A full bar means the level is complete.
    draw_rect(14, 14, 452, 8, 0xFF4A405B);
    float p = world_x / LEVEL_LENGTH;
    if (p < 0) p = 0; if (p > 1) p = 1;
    draw_rect(14, 14, 452 * p, 8, won ? 0xFF55E7A5 : 0xFFFFB84A);

    if (game_over) {
        draw_rect(130, 105, 220, 48, 0xFF40223D);
        draw_rect(147, 124, 186, 8, 0xFFFF6382);
        draw_rect(177, 140, 126, 5, 0xFFFFFFFF);
    } else if (won) {
        draw_rect(130, 105, 220, 48, 0xFF21473C);
        draw_rect(157, 124, 166, 8, 0xFF55E7A5);
        draw_rect(185, 140, 110, 5, 0xFFFFFFFF);
    }

    sceGuFinish();
    sceGuSync(0, 0);
    sceDisplayWaitVblankStart();
    sceGuSwapBuffers();
}

int main(void) {
    setup_callbacks();
    init_gu();
    sceCtrlSetSamplingCycle(0);
    sceCtrlSetSamplingMode(PSP_CTRL_MODE_ANALOG);

    while (running) {
        SceCtrlData pad;
        memset(&pad, 0, sizeof(pad));
        sceCtrlReadBufferPositive(&pad, 1);
        int jump_down = (pad.Buttons & (PSP_CTRL_CROSS | PSP_CTRL_UP)) != 0;
        int jump_pressed = jump_down && !jump_was_down;
        int square_down = (pad.Buttons & PSP_CTRL_SQUARE) != 0;
        int square_pressed = square_down && !square_was_down;
        jump_was_down = jump_down;
        square_was_down = square_down;
        if (square_pressed) show_hitboxes = !show_hitboxes;

        if ((game_over || won) && jump_pressed) {
            reset_game();
        } else if (!game_over && !won) {
            ++frames;
            // Fixed-step arcade physics: deterministic, simple to tune, and light enough for the PSP.
            if (jump_pressed && grounded) {
                player_vy = -10.2f;
                grounded = 0;
            }
            player_vy += 0.58f;
            player_y += player_vy;
            if (player_y >= GROUND_Y - PLAYER_SIZE) {
                player_y = GROUND_Y - PLAYER_SIZE;
                player_vy = 0.0f;
                grounded = 1;
            }
            world_x += 3.0f;
            if (world_x >= LEVEL_LENGTH) {
                world_x = LEVEL_LENGTH;
                won = 1;
            }

            // Simple, readable hitbox test against spike bounds.
            for (unsigned int i = 0; i < SPIKE_COUNT; ++i) {
                float sx = PLAYER_X + (spikes[i] - world_x);
                float px1 = PLAYER_X + 3, px2 = PLAYER_X + PLAYER_SIZE - 3;
                float py1 = player_y + 3, py2 = player_y + PLAYER_SIZE - 2;
                if (px2 > sx + 5 && px1 < sx + 25 && py2 > GROUND_Y - 25) {
                    game_over = 1;
                    break;
                }
            }
        }

        draw_frame();
    }

    sceGuDisplay(GU_FALSE);
    sceGuTerm();
    sceKernelExitGame();
    return 0;
}
