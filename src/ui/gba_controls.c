#include "ui/gba_controls.h"

#include <string.h>

#define RGB565(r, g, b) \
    ((u16)((((u32)(r) & 0xf8u) << 8) | (((u32)(g) & 0xfcu) << 3) | ((u32)(b) >> 3)))

static void fill_rect(u16 *pixels, int left, int top, int right, int bottom, u16 color)
{
    int x;
    int y;
    if (left < 0) left = 0;
    if (top < 0) top = 0;
    if (right > GBA_CONTROLS_WIDTH) right = GBA_CONTROLS_WIDTH;
    if (bottom > GBA_CONTROLS_HEIGHT) bottom = GBA_CONTROLS_HEIGHT;
    for (y = top; y < bottom; ++y) {
        for (x = left; x < right; ++x) {
            pixels[y * GBA_CONTROLS_WIDTH + x] = color;
        }
    }
}

static void fill_circle(u16 *pixels, int center_x, int center_y, int radius, u16 color)
{
    int x;
    int y;
    int radius_squared = radius * radius;
    for (y = -radius; y <= radius; ++y) {
        for (x = -radius; x <= radius; ++x) {
            if (x * x + y * y <= radius_squared) {
                int px = center_x + x;
                int py = center_y + y;
                if (px >= 0 && px < GBA_CONTROLS_WIDTH &&
                    py >= 0 && py < GBA_CONTROLS_HEIGHT) {
                    pixels[py * GBA_CONTROLS_WIDTH + px] = color;
                }
            }
        }
    }
}

static void draw_speaker_icon(u16 *pixels, int sound_enabled, u16 color)
{
    int row;
    fill_rect(pixels, 108, 12, 113, 21, color);
    for (row = 0; row < 13; ++row) {
        int half_width = row < 7 ? row / 2 : (12 - row) / 2;
        fill_rect(
            pixels, 113, 10 + row, 114 + half_width, 11 + row, color
        );
    }
    if (sound_enabled) {
        fill_rect(pixels, 122, 12, 124, 21, color);
        fill_rect(pixels, 127, 9, 129, 24, color);
    } else {
        for (row = 0; row < 16; ++row) {
            fill_rect(pixels, 119 + row / 2, 8 + row, 121 + row / 2, 9 + row, color);
        }
    }
}

static void draw_folder_icon(u16 *pixels, u16 color)
{
    fill_rect(pixels, 149, 10, 157, 13, color);
    fill_rect(pixels, 147, 13, 172, 15, color);
    fill_rect(pixels, 147, 13, 150, 23, color);
    fill_rect(pixels, 169, 13, 172, 23, color);
    fill_rect(pixels, 147, 21, 172, 24, color);
}

static const u8 *glyph(char character)
{
    static const u8 a[7] = {0x0e, 0x11, 0x11, 0x1f, 0x11, 0x11, 0x11};
    static const u8 b[7] = {0x1e, 0x11, 0x11, 0x1e, 0x11, 0x11, 0x1e};
    static const u8 c[7] = {0x0e, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0e};
    static const u8 e[7] = {0x1f, 0x10, 0x10, 0x1e, 0x10, 0x10, 0x1f};
    static const u8 l[7] = {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1f};
    static const u8 r[7] = {0x1e, 0x11, 0x11, 0x1e, 0x14, 0x12, 0x11};
    static const u8 s[7] = {0x0f, 0x10, 0x10, 0x0e, 0x01, 0x01, 0x1e};
    static const u8 t[7] = {0x1f, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04};
    static const u8 question[7] = {0x0e, 0x11, 0x01, 0x02, 0x04, 0x00, 0x04};
    static const u8 blank[7] = {0, 0, 0, 0, 0, 0, 0};
    switch (character) {
    case 'A': return a;
    case 'B': return b;
    case 'C': return c;
    case 'E': return e;
    case 'L': return l;
    case 'R': return r;
    case 'S': return s;
    case 'T': return t;
    case '?': return question;
    default: return blank;
    }
}

static void draw_text(
    u16 *pixels, int x, int y, const char *text, int scale, u16 color
)
{
    while (*text) {
        const u8 *rows = glyph(*text++);
        int row;
        int column;
        for (row = 0; row < 7; ++row) {
            for (column = 0; column < 5; ++column) {
                if ((rows[row] & (1u << (4 - column))) != 0u) {
                    fill_rect(
                        pixels,
                        x + column * scale,
                        y + row * scale,
                        x + (column + 1) * scale,
                        y + (row + 1) * scale,
                        color
                    );
                }
            }
        }
        x += 6 * scale;
    }
}

static u16 button_color(u32 pressed_mask, u32 bit, u16 normal, u16 pressed)
{
    return (pressed_mask & bit) != 0u ? pressed : normal;
}

static int in_rect(s32 x, s32 y, s32 left, s32 top, s32 right, s32 bottom)
{
    return x >= left && x < right && y >= top && y < bottom;
}

static int in_circle(s32 x, s32 y, s32 center_x, s32 center_y, s32 radius)
{
    s32 dx = x - center_x;
    s32 dy = y - center_y;
    return dx * dx + dy * dy <= radius * radius;
}

u32 gba_controls_hit_test(s32 screen_x, s32 screen_y)
{
    s32 y = screen_y - 160;
    if (screen_x < 0 || screen_x >= GBA_CONTROLS_WIDTH ||
        y < 0 || y >= GBA_CONTROLS_HEIGHT) {
        return 0u;
    }
    if (in_rect(screen_x, y, 4, 5, 59, 28)) return GBA_CONTROL_L;
    if (in_rect(screen_x, y, 64, 5, 87, 28)) return GBA_CONTROL_HELP;
    if (in_rect(screen_x, y, 104, 5, 137, 28)) return GBA_CONTROL_SOUND;
    if (in_rect(screen_x, y, 143, 5, 176, 28)) return GBA_CONTROL_ROM;
    if (in_rect(screen_x, y, 181, 5, 236, 28)) return GBA_CONTROL_R;
    if (in_circle(screen_x, y, 204, 63, 25)) return GBA_CONTROL_A;
    if (in_circle(screen_x, y, 160, 96, 25)) return GBA_CONTROL_B;
    if (in_rect(screen_x, y, 72, 127, 112, 149)) return GBA_CONTROL_SELECT;
    if (in_rect(screen_x, y, 120, 127, 160, 149)) return GBA_CONTROL_START;
    if (in_rect(screen_x, y, 42, 48, 69, 78)) return GBA_CONTROL_UP;
    if (in_rect(screen_x, y, 42, 103, 69, 133)) return GBA_CONTROL_DOWN;
    if (in_rect(screen_x, y, 14, 76, 44, 105)) return GBA_CONTROL_LEFT;
    if (in_rect(screen_x, y, 67, 76, 97, 105)) return GBA_CONTROL_RIGHT;
    return 0u;
}

void gba_controls_render(u16 *pixels, u32 pressed_mask, int sound_enabled)
{
    const u16 background = RGB565(18, 23, 27);
    const u16 panel = RGB565(39, 47, 51);
    const u16 label = RGB565(236, 240, 238);
    const u16 dpad = RGB565(86, 96, 101);
    const u16 dpad_pressed = RGB565(158, 172, 176);
    const u16 a = button_color(
        pressed_mask, GBA_CONTROL_A, RGB565(210, 57, 67), RGB565(255, 125, 132)
    );
    const u16 b = button_color(
        pressed_mask, GBA_CONTROL_B, RGB565(222, 167, 43), RGB565(255, 221, 118)
    );
    memset(pixels, 0, GBA_CONTROLS_WIDTH * GBA_CONTROLS_HEIGHT * sizeof(*pixels));
    fill_rect(pixels, 0, 0, GBA_CONTROLS_WIDTH, GBA_CONTROLS_HEIGHT, background);
    fill_rect(pixels, 0, 0, GBA_CONTROLS_WIDTH, 2, RGB565(98, 113, 118));

    fill_rect(
        pixels, 4, 5, 59, 28,
        button_color(pressed_mask, GBA_CONTROL_L, RGB565(42, 135, 86), RGB565(91, 207, 139))
    );
    fill_rect(
        pixels, 181, 5, 236, 28,
        button_color(pressed_mask, GBA_CONTROL_R, RGB565(45, 113, 190), RGB565(103, 175, 247))
    );
    fill_rect(
        pixels, 64, 5, 87, 28,
        button_color(
            pressed_mask, GBA_CONTROL_HELP,
            RGB565(53, 112, 121), RGB565(109, 193, 202)
        )
    );
    fill_rect(
        pixels, 104, 5, 137, 28,
        button_color(
            pressed_mask, GBA_CONTROL_SOUND,
            sound_enabled ? RGB565(38, 132, 101) : RGB565(72, 78, 81),
            sound_enabled ? RGB565(83, 196, 151) : RGB565(130, 139, 143)
        )
    );
    fill_rect(
        pixels, 143, 5, 176, 28,
        button_color(
            pressed_mask, GBA_CONTROL_ROM,
            RGB565(164, 88, 35), RGB565(237, 151, 78)
        )
    );
    draw_text(pixels, 27, 10, "L", 2, label);
    draw_text(pixels, 70, 10, "?", 2, label);
    draw_text(pixels, 204, 10, "R", 2, label);
    draw_speaker_icon(pixels, sound_enabled, label);
    draw_folder_icon(pixels, label);

    fill_rect(pixels, 42, 48, 69, 133, panel);
    fill_rect(pixels, 14, 76, 97, 105, panel);
    fill_rect(
        pixels, 44, 50, 67, 78,
        button_color(pressed_mask, GBA_CONTROL_UP, dpad, dpad_pressed)
    );
    fill_rect(
        pixels, 44, 103, 67, 131,
        button_color(pressed_mask, GBA_CONTROL_DOWN, dpad, dpad_pressed)
    );
    fill_rect(
        pixels, 16, 78, 44, 103,
        button_color(pressed_mask, GBA_CONTROL_LEFT, dpad, dpad_pressed)
    );
    fill_rect(
        pixels, 67, 78, 95, 103,
        button_color(pressed_mask, GBA_CONTROL_RIGHT, dpad, dpad_pressed)
    );
    fill_rect(pixels, 44, 78, 67, 103, dpad);

    fill_circle(pixels, 204, 63, 27, RGB565(77, 27, 31));
    fill_circle(pixels, 204, 63, 23, a);
    fill_circle(pixels, 160, 96, 27, RGB565(81, 61, 20));
    fill_circle(pixels, 160, 96, 23, b);
    draw_text(pixels, 199, 56, "A", 2, label);
    draw_text(pixels, 155, 89, "B", 2, RGB565(25, 28, 29));

    fill_rect(
        pixels, 72, 127, 112, 149,
        button_color(pressed_mask, GBA_CONTROL_SELECT, RGB565(90, 94, 96), RGB565(174, 181, 184))
    );
    fill_rect(
        pixels, 120, 127, 160, 149,
        button_color(pressed_mask, GBA_CONTROL_START, RGB565(90, 94, 96), RGB565(174, 181, 184))
    );
    draw_text(pixels, 74, 134, "SELECT", 1, label);
    draw_text(pixels, 126, 134, "START", 1, label);
}
