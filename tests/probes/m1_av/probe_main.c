#include "bda_audio.h"
#include "bda_filesystem.h"
#include "bda_graphics.h"
#include "bda_input.h"
#include "bda_memory.h"
#include "bda_time.h"
#include "bda_types.h"
#include "bda_window.h"

#define SCREEN_WIDTH 240
#define SCREEN_HEIGHT 320
#define GBA_WIDTH 240
#define GBA_HEIGHT 160
#define PCM_SAMPLES 512u
#define PCM_BYTES (PCM_SAMPLES * (u32)sizeof(s16))
#define PCM_BLOCK_TARGET 24u

static bda_handle_t g_frame;
static bda_handle_t g_draw;
static bda_handle_t g_draw_owner;
static void *g_draw_object;
static bda_gui_picture_t g_picture;
static u16 *g_pixels;
static s16 g_pcm[PCM_SAMPLES];
static u32 g_previous_keys;
static u32 g_draw_acquires;
static u32 g_draw_releases;
static volatile int g_detached;
static int g_video_ok;
static int g_audio_open;
static int g_audio_done;
static int g_original_attenuation;
static u32 g_audio_blocks;
static u32 g_audio_ready_polls;
static u32 g_audio_phase;
static u32 g_audio_finish_tick;

static u32 string_length(const char *text)
{
    u32 length = 0;
    while (text[length]) {
        ++length;
    }
    return length;
}

static int open_log(const char *mode)
{
    return bda_fs_fopen_raw("M1AV.LOG", mode);
}

static void log_text(const char *text)
{
    int file = open_log("ab");
    if (bda_fs_file_is_valid(file)) {
        (void)bda_fs_write_raw(file, text, string_length(text));
        (void)bda_fs_write_raw(file, "\r\n", 2u);
        (void)bda_fs_close_raw(file);
    }
}

static void log_hex(const char *label, u32 value)
{
    static const char digits[] = "0123456789ABCDEF";
    char line[64];
    u32 offset = 0;
    int shift;
    int file;

    while (*label) {
        line[offset++] = *label++;
    }
    line[offset++] = '0';
    line[offset++] = 'x';
    for (shift = 28; shift >= 0; shift -= 4) {
        line[offset++] = digits[(value >> shift) & 0x0fu];
    }
    line[offset++] = '\r';
    line[offset++] = '\n';
    file = open_log("ab");
    if (bda_fs_file_is_valid(file)) {
        (void)bda_fs_write_raw(file, line, offset);
        (void)bda_fs_close_raw(file);
    }
}

static u16 rgb565(u32 red, u32 green, u32 blue)
{
    return (u16)(((red & 0xf8u) << 8) | ((green & 0xfcu) << 3) |
        (blue >> 3));
}

static void fill_gba_test_pattern(void)
{
    static const u16 bars[8] = {
        0xffffu, 0xffe0u, 0x07ffu, 0x07e0u,
        0xf81fu, 0xf800u, 0x001fu, 0x0000u
    };
    int x;
    int y;

    for (y = 0; y < GBA_HEIGHT; ++y) {
        for (x = 0; x < GBA_WIDTH; ++x) {
            u16 color;
            if (y < 40) {
                color = bars[x / 30];
            } else if (y < 80) {
                color = rgb565((u32)x, (u32)(x ^ y), (u32)(239 - x));
            } else if (y < 120) {
                color = (((x / 8) + (y / 8)) & 1) ? 0xdefbu : 0x2104u;
            } else {
                color = x == 0 || x == GBA_WIDTH - 1 || y == GBA_HEIGHT - 1 ||
                    x == y || x == GBA_WIDTH - 1 - y ? 0xffffu : 0x4208u;
            }
            g_pixels[y * GBA_WIDTH + x] = color;
        }
    }
}

static void release_draw_context(void)
{
    bda_handle_t draw = g_draw;
    if (!draw || (s32)draw == -1) {
        g_draw = 0;
        g_draw_owner = 0;
        return;
    }
    g_draw = 0;
    g_draw_owner = 0;
    bda_gui_end_draw(draw);
    ++g_draw_releases;
}

static int acquire_draw_context(bda_handle_t owner)
{
    if (g_draw && g_draw_owner == owner) {
        return 1;
    }
    release_draw_context();
    g_draw = bda_gui_current_draw(owner);
    if (!g_draw || (s32)g_draw == -1) {
        g_draw = 0;
        return 0;
    }
    g_draw_owner = owner;
    ++g_draw_acquires;
    return 1;
}

static int present_video(void)
{
    void *old_object;
    int result;

    (void)bda_gui_draw_guard_begin();
    old_object = bda_gui_select_draw_object(g_draw, g_draw_object);
    result = bda_gui_render_picture(
        g_draw, 0, 0, GBA_WIDTH, GBA_HEIGHT, &g_picture
    );
    (void)bda_gui_set_text_mode(g_draw, 1u);
    (void)bda_gui_set_text_color(
        g_draw, (u32)bda_gui_rgb(g_draw, 238u, 244u, 242u)
    );
    (void)bda_gui_draw_text(g_draw, 30, 188, "GBA 240x160 RGB565", -1);
    (void)bda_gui_draw_text(g_draw, 38, 224, "PCM 22050 / 16 / MONO", -1);
    (void)bda_gui_draw_text(
        g_draw, 63, 260, g_audio_done ? "AUDIO PASS" : "AUDIO TEST", -1
    );
    (void)bda_gui_draw_text(g_draw, 78, 292, "ESC EXIT", -1);
    (void)bda_gui_select_draw_object(g_draw, old_object);
    (void)bda_gui_draw_guard_end();
    return result == 0;
}

static void fill_pcm(void)
{
    u32 index;
    for (index = 0; index < PCM_SAMPLES; ++index) {
        g_audio_phase += 440u;
        if (g_audio_phase >= BDA_AUDIO_SAMPLE_RATE_22050) {
            g_audio_phase -= BDA_AUDIO_SAMPLE_RATE_22050;
        }
        g_pcm[index] = g_audio_phase < BDA_AUDIO_SAMPLE_RATE_22050 / 2u ?
            (s16)7000 : (s16)-7000;
    }
}

static void start_audio(void)
{
    g_original_attenuation = bda_audio_get_attenuation();
    bda_audio_open_pcm(
        BDA_AUDIO_SAMPLE_RATE_22050,
        BDA_AUDIO_BITS_16,
        BDA_AUDIO_CHANNELS_MONO
    );
    bda_audio_set_attenuation(BDA_AUDIO_ATTENUATION_HALF_SCALE);
    g_audio_open = 1;
    log_hex("AUDIO_ORIGINAL_ATTENUATION=", (u32)g_original_attenuation);
}

static int service_audio(void)
{
    int written;

    if (g_audio_done) {
        return 1;
    }
    if (g_audio_blocks < PCM_BLOCK_TARGET) {
        ++g_audio_ready_polls;
        if (!bda_audio_ready()) {
            return 1;
        }
        fill_pcm();
        written = bda_audio_write(g_pcm, PCM_BYTES);
        if (written != (int)PCM_BYTES) {
            log_hex("AUDIO_WRITE_ERROR=", (u32)written);
            return 0;
        }
        ++g_audio_blocks;
        if (g_audio_blocks == PCM_BLOCK_TARGET) {
            g_audio_finish_tick = bda_gui_tick_count_25ms();
        }
        return 1;
    }

    if (bda_gui_tick_elapsed_25ms(
        g_audio_finish_tick, bda_gui_tick_count_25ms()
    ) < 20u) {
        return 1;
    }
    ++g_audio_ready_polls;
    if (!bda_audio_ready()) {
        return 1;
    }

    bda_memset(g_pcm, 0, sizeof(g_pcm));
    bda_audio_set_attenuation((u32)g_original_attenuation);
    written = bda_audio_write(g_pcm, PCM_BYTES);
    if (written != (int)PCM_BYTES) {
        log_hex("AUDIO_SILENT_WRITE_ERROR=", (u32)written);
        return 0;
    }
    bda_audio_stop();
    g_audio_open = 0;
    g_audio_done = 1;
    log_hex("AUDIO_BLOCKS=", g_audio_blocks);
    log_hex("AUDIO_READY_POLLS=", g_audio_ready_polls);
    log_text("AUDIO_STOP=PASS");
    (void)present_video();
    return 1;
}

static void stop_audio(void)
{
    if (!g_audio_open) {
        return;
    }
    bda_audio_set_attenuation((u32)g_original_attenuation);
    if (bda_audio_ready()) {
        bda_memset(g_pcm, 0, sizeof(g_pcm));
        (void)bda_audio_write(g_pcm, PCM_BYTES);
    }
    bda_audio_stop();
    g_audio_open = 0;
}

static u32 key_mask(const bda_gui_input_packet_t *packet)
{
    return bda_gui_input_packet_key_pressed(packet, BDA_KEY_ESCAPE) ? 1u : 0u;
}

static int av_window_proc(
    bda_handle_t handle, u32 message, u32 wparam, u32 lparam
)
{
    if (message == BDA_MSG_DRAW_CONTEXT_ATTACH) {
        g_frame = handle;
        (void)acquire_draw_context(handle);
        if (!g_draw_object) {
            g_draw_object = bda_gui_draw_object_create(7u);
        }
    } else if (message == BDA_MSG_DRAW_CONTEXT_DETACH) {
        if (!g_draw_owner || g_draw_owner == handle) {
            release_draw_context();
        }
        g_detached = 1;
    }
    return bda_gui_default_proc(handle, message, wparam, lparam);
}

int bda_main(void)
{
    bda_frame_desc_t descriptor;
    bda_gui_message_t message;
    bda_gui_input_packet_t packet;
    int file;
    int close_requested = 0;
    int failures = 0;
    u32 close_wait = 0;

    file = open_log("wb");
    if (bda_fs_file_is_valid(file)) {
        (void)bda_fs_close_raw(file);
    }
    log_text("BBK9588 M1 AV");
    bda_memset(&descriptor, 0, sizeof(descriptor));
    bda_memset(&message, 0, sizeof(message));
    bda_memset(&g_picture, 0, sizeof(g_picture));
    g_frame = 0;
    g_draw = 0;
    g_draw_owner = 0;
    g_draw_object = 0;
    g_pixels = (u16 *)bda_alloc(GBA_WIDTH * GBA_HEIGHT * sizeof(u16));
    g_detached = 0;
    g_draw_acquires = 0;
    g_draw_releases = 0;
    g_audio_open = 0;
    g_audio_done = 0;
    g_audio_blocks = 0;
    g_audio_ready_polls = 0;
    g_audio_phase = 0;

    if (!g_pixels || (u32)g_pixels == 0xffffffffu) {
        log_text("RESULT=PIXEL ALLOC FAIL");
        return 1;
    }
    fill_gba_test_pattern();
    g_picture.width = GBA_WIDTH;
    g_picture.height = GBA_HEIGHT;
    g_picture.stride_bytes = GBA_WIDTH * sizeof(u16);
    g_picture.source_pixels = g_pixels;
    g_picture.selected_index = -1;

    descriptor.title = "GBA M1 AV";
    descriptor.wndproc = av_window_proc;
    descriptor.height = SCREEN_WIDTH;
    descriptor.width = SCREEN_HEIGHT;
    g_frame = bda_gui_register_frame_desc(&descriptor);
    if (!g_frame || (s32)g_frame == -1) {
        bda_free(g_pixels);
        log_text("RESULT=FRAME FAIL");
        return 2;
    }
    (void)bda_gui_frame_activate(g_frame, 0x100u);
    (void)acquire_draw_context(g_frame);
    if (!g_draw_object) {
        g_draw_object = bda_gui_draw_object_create(7u);
    }
    if (!g_draw || !g_draw_object || (s32)(u32)g_draw_object == -1) {
        (void)bda_gui_frame_stop(g_frame);
        (void)bda_gui_frame_release(g_frame);
        release_draw_context();
        bda_gui_close_frame(g_frame);
        bda_free(g_pixels);
        log_text("RESULT=DRAW FAIL");
        return 3;
    }

    g_video_ok = present_video();
    log_hex("VIDEO_RENDER=", (u32)g_video_ok);
    start_audio();
    (void)bda_gui_input_packet(&packet);
    g_previous_keys = key_mask(&packet);

    while (!g_detached) {
        int pump_result = bda_gui_event_pump_frame_once(&message, g_frame);
        u32 current;
        u32 pressed;
        (void)bda_gui_input_packet(&packet);
        current = key_mask(&packet);
        pressed = current & ~g_previous_keys;
        g_previous_keys = current;

        if (!close_requested && !service_audio()) {
            ++failures;
            stop_audio();
        }
        if (!close_requested && (pressed & 1u)) {
            (void)bda_gui_frame_stop(g_frame);
            (void)bda_gui_frame_release(g_frame);
            close_requested = 1;
        }
        if (close_requested &&
            (!pump_result || g_detached || ++close_wait >= 128u)) {
            break;
        }
        bda_sys_delay(1u);
    }

    stop_audio();
    release_draw_context();
    if (g_frame) {
        if (!close_requested) {
            (void)bda_gui_frame_stop(g_frame);
            (void)bda_gui_frame_release(g_frame);
        }
        bda_gui_close_frame(g_frame);
    }
    bda_free(g_pixels);
    g_pixels = 0;

    log_hex("DRAW_ACQUIRES=", g_draw_acquires);
    log_hex("DRAW_RELEASES=", g_draw_releases);
    if (!g_video_ok || !g_audio_done || g_draw_acquires != g_draw_releases) {
        ++failures;
    }
    log_text(failures ? "RESULT=FAIL" : "RESULT=PASS");
    return failures ? 4 : 0;
}
