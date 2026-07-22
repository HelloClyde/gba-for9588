#ifndef GBA_CONTROLS_H
#define GBA_CONTROLS_H

#include "bda_types.h"

#define GBA_CONTROLS_WIDTH 240
#define GBA_CONTROLS_HEIGHT 160

enum gba_control_mask {
    GBA_CONTROL_UP = 1u << 0,
    GBA_CONTROL_DOWN = 1u << 1,
    GBA_CONTROL_LEFT = 1u << 2,
    GBA_CONTROL_RIGHT = 1u << 3,
    GBA_CONTROL_A = 1u << 4,
    GBA_CONTROL_B = 1u << 5,
    GBA_CONTROL_L = 1u << 6,
    GBA_CONTROL_R = 1u << 7,
    GBA_CONTROL_START = 1u << 8,
    GBA_CONTROL_SELECT = 1u << 9,
    GBA_CONTROL_SOUND = 1u << 10,
    GBA_CONTROL_HELP = 1u << 11,
    GBA_CONTROL_ROM = 1u << 12,
    GBA_CONTROL_SPEED = 1u << 13,
    GBA_CONTROL_KEYMAP = 1u << 14
};

u32 gba_controls_hit_test(s32 screen_x, s32 screen_y);
void gba_controls_render(
    u16 *pixels, u32 pressed_mask, u32 audio_level, u32 frameskip_interval,
    int key_swap
);

#endif
