#include "bda_audio.h"
#include "bda_dialogs.h"
#include "bda_filesystem.h"
#include "bda_graphics.h"
#include "bda_input.h"
#include "bda_memory.h"
#include "bda_time.h"
#include "bda_types.h"
#include "bda_window.h"
#include "bda/detail/runtime.h"

#include "platform/bbk9588/audio_output.h"
#include "platform/bbk9588/save_store.h"
#include "ui/gba_controls.h"

#include <libretro.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern volatile unsigned bbk9588_run_stage;

#define SCREEN_WIDTH 240
#define SCREEN_HEIGHT 320
#define GBA_WIDTH 240
#define GBA_HEIGHT 160
#define ESCAPE_HOLD_TICKS 32u
#define AUDIO_BACKPRESSURE_MAX_TICKS 4u
#define RAW_EVENT_MAX_PER_POLL 8u
#define BBK9588_INPUT_POLL_CYCLES 8192u
#if defined(BBK_GPSP_CPU_TEST)
#define DIAGNOSTIC_FRAME_INTERVAL 120u
#else
#define DIAGNOSTIC_FRAME_INTERVAL 600u
#endif
#define EXIT_CAUSE_NONE 0u
#define EXIT_CAUSE_ESCAPE_HOLD 1u
#define EXIT_CAUSE_FRAME_DETACH 3u

static bda_handle_t g_frame;
static bda_handle_t g_draw;
static bda_handle_t g_draw_owner;
static void *g_draw_object;
static bda_gui_picture_t g_video_picture;
static bda_gui_picture_t g_controls_picture;
static u16 *g_controls_pixels;
static bda_file_selector_t g_selector;
static bbk_audio_output_t g_audio;
static bbk_save_store_t g_save_store;
static char g_rom_path[BBK_SAVE_PATH_CAPACITY];
static char g_log_path[BBK_SAVE_PATH_CAPACITY];
static volatile u32 g_touch_mask;
static u16 g_touch_x;
static u16 g_touch_y;
static u32 g_input_mask;
static u32 g_rendered_controls_mask;
static u32 g_escape_start_tick;
static u32 g_touch_start_tick;
static u32 g_escape_max_ticks;
static u32 g_touch_start_max_ticks;
static u32 g_exit_cause;
static u32 g_video_frames;
static u32 g_save_writes;
static int g_escape_down;
static int g_touch_start_down;
static int g_raw_touch_down;
static int g_sound_touch_down;
static int g_help_touch_down;
static int g_help_pending;
static int g_modal_active;
static int g_rom_touch_down;
static int g_rom_change_pending;
static int g_controls_dirty;
static int g_full_redraw;
static int g_audio_enabled;
static int g_audio_toggle_pending;
static int g_exit_requested;
static int g_detached;
static int g_core_loaded;
static u32 g_core_frames;
static u32 g_video_callbacks;
static u32 g_video_submit_errors;
static u32 g_controls_submit_errors;
static u32 g_audio_backpressure_skips;
static u32 g_controls_render_attempts;
static u32 g_audio_callbacks;
static u32 g_progress_last_tick;
static u32 g_progress_last_core_frames;
static u32 g_progress_last_video_frames;
static u32 g_progress_last_audio_samples;
static u32 g_raw_poll_calls;
static u32 g_raw_event_count;
static u32 g_raw_event_max_batch;
static u32 g_raw_event_cap_hits;
static u32 g_raw_touch_down_count;
static u32 g_raw_touch_move_count;
static u32 g_raw_touch_up_count;
static u32 g_raw_event_ignored;
static volatile u32 g_diag_loop_stage;
static volatile u32 g_diag_loop_frame;

static const char g_help_title[] = "GBA\304\243\304\342\306\367";
static const char g_help_body[] =
    "\262\331\327\367\313\265\303\367\n"
    "\267\275\317\362\274\374\243\272\322\306\266\257\n"
    "\310\267\266\250\274\374\243\272A\274\374\n"
    "\315\313\263\366\274\374\266\314\260\264\243\272B\274\374\n"
    "\315\313\263\366\274\374\263\244\260\264\243\272"
        "\315\313\263\366\304\243\304\342\306\367\n"
    "\264\245\306\301\243\272A/B/L/R/\277\252\312\274/\321\241\324\361\n"
    "\321\357\311\371\306\367\260\264\305\245\243\272"
        "\311\371\322\364\277\252\271\330\n"
    "\316\312\272\305\260\264\305\245\243\272"
        "\312\271\323\303\313\265\303\367\n"
    "\316\304\274\376\274\320\260\264\305\245\243\272"
        "\270\374\273\273ROM\n"
    "\n"
    "ROM\304\277\302\274\243\272A:\\GAMEBOY\\\n"
    "\325\375\263\243\315\313\263\366\312\261\261\243\264\346"
        "\323\316\317\267\241\243\n"
    "\n"
    "\304\243\304\342\306\367\322\375\307\346\243\272gpSP\n"
    "\322\375\307\346\327\367\325\337\243\272Exophase\n"
    "9x88\322\306\326\262\327\367\325\337\243\272HelloClyde\n";
static const char g_rom_selector_title[] =
    "\321\241\324\361GBA ROM";
static const char g_rom_confirm_title[] = "\270\374\273\273ROM";
static const char g_rom_confirm_body[] =
    "\310\267\266\250\322\252\270\374\273\273ROM\302\360\243\277\n"
    "\265\261\307\260\323\316\317\267\275\253\317\310\261\243\264\346\241\243";
static const char g_save_error_title[] = "\261\243\264\346\312\247\260\334";
static const char g_save_error_body[] =
    "\265\261\307\260\323\316\317\267\261\243\264\346\312\247\260\334"
    "\243\254\316\264\270\374\273\273ROM\241\243";
static const char g_load_error_title[] = "ROM\274\323\324\330\312\247\260\334";
static const char g_load_restore_body[] =
    "\316\336\267\250\274\323\324\330\320\302ROM\243\254\322\321\273\326"
    "\270\264\324\255\323\316\317\267\241\243";
static const char g_load_fatal_body[] =
    "\316\336\267\250\273\326\270\264\324\255\323\316\317\267\241\243";

static void shutdown_core(void);

static int open_log(const char *mode)
{
    if (g_log_path[0] == 0) {
        return 0;
    }
    return bda_fs_fopen_raw(g_log_path, mode);
}

static void log_text(const char *text)
{
    int file = open_log("ab");
    if (bda_fs_file_is_valid(file)) {
        (void)bda_fs_write_raw(file, text, (bda_size_t)strlen(text));
        (void)bda_fs_write_raw(file, "\r\n", 2u);
        (void)bda_fs_close_raw(file);
    }
}

static void log_value(const char *label, u32 value)
{
    char line[96];
    if (snprintf(line, sizeof(line), "%s0x%08X", label, value) > 0) {
        log_text(line);
    }
}

static void frontend_log(enum retro_log_level level, const char *format, ...)
{
    char line[192];
    size_t length;
    va_list args;
    (void)level;
    va_start(args, format);
    (void)vsnprintf(line, sizeof(line), format, args);
    va_end(args);
    length = strlen(line);
    while (length != 0u && (line[length - 1u] == '\n' || line[length - 1u] == '\r')) {
        line[--length] = 0;
    }
    if (length != 0u) {
        log_text(line);
    }
}

static const char *variable_value(const char *key)
{
    if (strcmp(key, "gpsp_drc") == 0) {
#if defined(HAVE_DYNAREC)
        return "enabled";
#else
        return "disabled";
#endif
    }
    if (strcmp(key, "gpsp_bios") == 0) return "builtin";
    if (strcmp(key, "gpsp_boot_mode") == 0) return "game";
    if (strcmp(key, "gpsp_rtc") == 0) return "auto";
    if (strcmp(key, "gpsp_serial") == 0) return "disabled";
    if (strcmp(key, "gpsp_rumble") == 0) return "disabled";
    if (strcmp(key, "gpsp_sprlim") == 0) return "disabled";
    if (strcmp(key, "gpsp_frameskip") == 0) return "fixed_interval";
    if (strcmp(key, "gpsp_frameskip_threshold") == 0) return "33";
    if (strcmp(key, "gpsp_frameskip_interval") == 0) {
#if defined(BBK_GPSP_CPU_TEST)
        return "59";
#else
        return "1";
#endif
    }
    if (strcmp(key, "gpsp_color_correction") == 0) return "disabled";
    if (strcmp(key, "gpsp_frame_mixing") == 0) return "disabled";
    if (strcmp(key, "gpsp_turbo_period") == 0) return "4";
    return 0;
}

static bool environment_callback(unsigned command, void *data)
{
    static const char root_directory[] = "/";
    switch (command) {
    case RETRO_ENVIRONMENT_GET_LOG_INTERFACE:
        ((struct retro_log_callback *)data)->log = frontend_log;
        return true;
    case RETRO_ENVIRONMENT_GET_CORE_OPTIONS_VERSION:
        *(unsigned *)data = 1u;
        return true;
    case RETRO_ENVIRONMENT_GET_LANGUAGE:
        *(unsigned *)data = RETRO_LANGUAGE_ENGLISH;
        return true;
    case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_INTL:
    case RETRO_ENVIRONMENT_SET_CORE_OPTIONS:
    case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT:
    case RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS:
    case RETRO_ENVIRONMENT_SET_MEMORY_MAPS:
    case RETRO_ENVIRONMENT_SET_MINIMUM_AUDIO_LATENCY:
        return true;
    case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY:
    case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY:
        *(const char **)data = root_directory;
        return true;
    case RETRO_ENVIRONMENT_GET_VARIABLE:
        ((struct retro_variable *)data)->value =
            variable_value(((struct retro_variable *)data)->key);
        return ((struct retro_variable *)data)->value != 0;
    case RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE:
        *(bool *)data = false;
        return true;
    case RETRO_ENVIRONMENT_GET_INPUT_BITMASKS:
        return true;
    case RETRO_ENVIRONMENT_GET_MESSAGE_INTERFACE_VERSION:
        *(unsigned *)data = 1u;
        return true;
    default:
        return false;
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
    return 1;
}

static void video_callback(
    const void *data, unsigned width, unsigned height, size_t pitch
)
{
    void *old_object;
    int video_result;
    if (!data || width != GBA_WIDTH || height != GBA_HEIGHT ||
        pitch < GBA_WIDTH * sizeof(u16)) {
        return;
    }
    ++g_video_callbacks;
    if (g_video_callbacks == 1u) {
        log_text("VIDEO_CB_BEGIN");
    }
    g_video_picture.source_pixels = data;
    (void)bda_gui_draw_guard_begin();
    old_object = bda_gui_select_draw_object(g_draw, g_draw_object);
    video_result = bda_gui_render_picture(
        g_draw, 0, 0, GBA_WIDTH, GBA_HEIGHT, &g_video_picture
    );
    (void)bda_gui_select_draw_object(g_draw, old_object);
    (void)bda_gui_draw_guard_end();
    if (g_video_callbacks == 1u) {
        log_value("VIDEO_CB_END=", (u32)video_result);
    }
    if (video_result == 0) {
        ++g_video_frames;
        if (g_video_frames == 1u ||
            g_video_frames % DIAGNOSTIC_FRAME_INTERVAL == 0u) {
            log_value("FRAME_HEARTBEAT=", g_video_frames);
        }
    } else {
        ++g_video_submit_errors;
    }
}

static void render_controls(void)
{
    void *old_object;
    int result;
    if (!g_controls_dirty || !g_controls_pixels || !g_draw || !g_draw_object) {
        return;
    }
    ++g_controls_render_attempts;
    if (g_controls_render_attempts == 1u) {
        log_text("CONTROLS_RENDER_BEGIN");
    }
    gba_controls_render(
        g_controls_pixels,
        g_input_mask |
            (g_sound_touch_down ? GBA_CONTROL_SOUND : 0u) |
            (g_help_touch_down ? GBA_CONTROL_HELP : 0u) |
            (g_rom_touch_down ? GBA_CONTROL_ROM : 0u),
        g_audio_enabled
    );
    g_controls_picture.source_pixels = g_controls_pixels;
    (void)bda_gui_draw_guard_begin();
    old_object = bda_gui_select_draw_object(g_draw, g_draw_object);
    result = bda_gui_render_picture(
        g_draw, 0, GBA_HEIGHT,
        GBA_CONTROLS_WIDTH, GBA_CONTROLS_HEIGHT,
        &g_controls_picture
    );
    (void)bda_gui_select_draw_object(g_draw, old_object);
    (void)bda_gui_draw_guard_end();
    if (g_controls_render_attempts == 1u) {
        log_value("CONTROLS_RENDER_END=", (u32)result);
    }
    if (result == 0) {
        g_rendered_controls_mask = g_input_mask;
        g_controls_dirty = 0;
    } else {
        ++g_controls_submit_errors;
    }
}

static void render_full_surface_if_needed(void)
{
    void *old_object;
    int result;

    if (!g_full_redraw || !g_draw || !g_draw_object) {
        return;
    }
    if (!g_video_picture.source_pixels) {
        g_full_redraw = 0;
        return;
    }
    (void)bda_gui_draw_guard_begin();
    old_object = bda_gui_select_draw_object(g_draw, g_draw_object);
    result = bda_gui_render_picture(
        g_draw, 0, 0, GBA_WIDTH, GBA_HEIGHT, &g_video_picture
    );
    (void)bda_gui_select_draw_object(g_draw, old_object);
    (void)bda_gui_draw_guard_end();
    if (result == 0) {
        g_full_redraw = 0;
    } else {
        ++g_video_submit_errors;
    }
}

static size_t audio_callback(const int16_t *data, size_t frames)
{
    size_t pushed;
    ++g_audio_callbacks;
    if (g_audio_callbacks == 1u) {
        log_value("AUDIO_CB_BEGIN=", (u32)frames);
    }
    pushed = bbk_audio_output_push_stereo(&g_audio, data, frames);
    if (g_audio_callbacks == 1u) {
        log_value("AUDIO_CB_END=", (u32)pushed);
    }
    return pushed;
}

static void clear_touch_state(void)
{
    g_raw_touch_down = 0;
    g_touch_mask = 0u;
    g_sound_touch_down = 0;
    g_help_touch_down = 0;
    g_rom_touch_down = 0;
    g_controls_dirty = 1;
}

static void apply_touch_hit(u32 hit)
{
    if ((hit & GBA_CONTROL_SOUND) != 0u) {
        g_help_touch_down = 0;
        g_rom_touch_down = 0;
        if (!g_sound_touch_down) {
            g_sound_touch_down = 1;
            g_audio_toggle_pending = 1;
        }
        g_touch_mask = 0u;
    } else if ((hit & GBA_CONTROL_HELP) != 0u) {
        g_sound_touch_down = 0;
        g_rom_touch_down = 0;
        if (!g_help_touch_down) {
            g_help_touch_down = 1;
            g_help_pending = 1;
        }
        g_touch_mask = 0u;
    } else if ((hit & GBA_CONTROL_ROM) != 0u) {
        g_sound_touch_down = 0;
        g_help_touch_down = 0;
        if (!g_rom_touch_down) {
            g_rom_touch_down = 1;
            g_rom_change_pending = 1;
        }
        g_touch_mask = 0u;
    } else {
        g_sound_touch_down = 0;
        g_help_touch_down = 0;
        g_rom_touch_down = 0;
        g_touch_mask = hit;
    }
    g_controls_dirty = 1;
}

static void update_touch_position(void)
{
    bda_gui_touch_position(&g_touch_x, &g_touch_y);
    apply_touch_hit(gba_controls_hit_test((s32)g_touch_x, (s32)g_touch_y));
}

static void poll_raw_touch_events(void)
{
    bda_gui_raw_event_t event;
    u32 batch_count = 0u;

    ++g_raw_poll_calls;
    while (batch_count < RAW_EVENT_MAX_PER_POLL) {
        if (bda_gui_raw_event_fetch(&event) < 0) {
            break;
        }
        ++batch_count;
        ++g_raw_event_count;
        switch ((u32)event.code) {
            case BDA_INPUT_EVENT_TOUCH_DOWN:
                ++g_raw_touch_down_count;
                g_raw_touch_down = 1;
                update_touch_position();
                break;
            case BDA_INPUT_EVENT_TOUCH_MOVE:
                ++g_raw_touch_move_count;
                if (g_raw_touch_down) {
                    update_touch_position();
                } else {
                    ++g_raw_event_ignored;
                }
                break;
            case BDA_INPUT_EVENT_TOUCH_UP:
                ++g_raw_touch_up_count;
                if (g_raw_touch_down) {
                    clear_touch_state();
                } else {
                    ++g_raw_event_ignored;
                }
                break;
            default:
                ++g_raw_event_ignored;
                break;
        }
    }
    if (batch_count > g_raw_event_max_batch) {
        g_raw_event_max_batch = batch_count;
    }
    if (batch_count == RAW_EVENT_MAX_PER_POLL) {
        ++g_raw_event_cap_hits;
    }
}

static u32 physical_input_mask(void)
{
    bda_gui_input_packet_t packet;
    u32 mask = 0u;
    u32 now;
    int escape;
    (void)bda_gui_input_packet(&packet);
    if (bda_gui_input_packet_key_pressed(&packet, BDA_KEY_UP)) mask |= GBA_CONTROL_UP;
    if (bda_gui_input_packet_key_pressed(&packet, BDA_KEY_DOWN)) mask |= GBA_CONTROL_DOWN;
    if (bda_gui_input_packet_key_pressed(&packet, BDA_KEY_LEFT)) mask |= GBA_CONTROL_LEFT;
    if (bda_gui_input_packet_key_pressed(&packet, BDA_KEY_RIGHT)) mask |= GBA_CONTROL_RIGHT;
    if (bda_gui_input_packet_key_pressed(&packet, BDA_KEY_ENTER)) mask |= GBA_CONTROL_A;
    escape = g_touch_mask == 0u &&
        bda_gui_input_packet_key_pressed(&packet, BDA_KEY_ESCAPE);
    now = bda_gui_tick_count_25ms();
    if (escape) {
        u32 elapsed;
        if (!g_escape_down) {
            g_escape_start_tick = now;
            g_escape_down = 1;
        }
        elapsed = bda_gui_tick_elapsed_25ms(g_escape_start_tick, now);
        if (elapsed > g_escape_max_ticks) {
            g_escape_max_ticks = elapsed;
        }
        if (elapsed >= ESCAPE_HOLD_TICKS) {
            g_exit_cause = EXIT_CAUSE_ESCAPE_HOLD;
            g_exit_requested = 1;
        } else {
            mask |= GBA_CONTROL_B;
        }
    } else {
        g_escape_down = 0;
    }
    return mask;
}

static void input_poll_callback(void)
{
    u32 previous = g_input_mask;
    u32 touch;
    u32 now = bda_gui_tick_count_25ms();

    poll_raw_touch_events();
    touch = g_touch_mask;
    if ((touch & GBA_CONTROL_START) != 0u) {
        u32 elapsed;
        if (!g_touch_start_down) {
            g_touch_start_tick = now;
            g_touch_start_down = 1;
        }
        elapsed = bda_gui_tick_elapsed_25ms(g_touch_start_tick, now);
        if (elapsed > g_touch_start_max_ticks) {
            g_touch_start_max_ticks = elapsed;
        }
    } else {
        g_touch_start_down = 0;
    }
    g_input_mask = physical_input_mask() | touch;
    if (g_input_mask != previous || g_input_mask != g_rendered_controls_mask) {
        g_controls_dirty = 1;
    }
}

static int16_t input_state_callback(
    unsigned port, unsigned device, unsigned index, unsigned id
)
{
    u32 retro_mask = 0u;
    (void)port;
    (void)device;
    (void)index;
    if (g_input_mask & GBA_CONTROL_UP) retro_mask |= 1u << RETRO_DEVICE_ID_JOYPAD_UP;
    if (g_input_mask & GBA_CONTROL_DOWN) retro_mask |= 1u << RETRO_DEVICE_ID_JOYPAD_DOWN;
    if (g_input_mask & GBA_CONTROL_LEFT) retro_mask |= 1u << RETRO_DEVICE_ID_JOYPAD_LEFT;
    if (g_input_mask & GBA_CONTROL_RIGHT) retro_mask |= 1u << RETRO_DEVICE_ID_JOYPAD_RIGHT;
    if (g_input_mask & GBA_CONTROL_A) retro_mask |= 1u << RETRO_DEVICE_ID_JOYPAD_A;
    if (g_input_mask & GBA_CONTROL_B) retro_mask |= 1u << RETRO_DEVICE_ID_JOYPAD_B;
    if (g_input_mask & GBA_CONTROL_L) retro_mask |= 1u << RETRO_DEVICE_ID_JOYPAD_L;
    if (g_input_mask & GBA_CONTROL_R) retro_mask |= 1u << RETRO_DEVICE_ID_JOYPAD_R;
    if (g_input_mask & GBA_CONTROL_START) retro_mask |= 1u << RETRO_DEVICE_ID_JOYPAD_START;
    if (g_input_mask & GBA_CONTROL_SELECT) retro_mask |= 1u << RETRO_DEVICE_ID_JOYPAD_SELECT;
    if (id == RETRO_DEVICE_ID_JOYPAD_MASK) {
        return (int16_t)retro_mask;
    }
    return id < 16u && (retro_mask & (1u << id)) != 0u;
}

static int app_window_proc(
    bda_handle_t handle, u32 message, u32 wparam, u32 lparam
)
{
    if (message == BDA_MSG_DRAW_CONTEXT_ATTACH) {
        g_frame = handle;
        (void)acquire_draw_context(handle);
        if (!g_draw_object) {
            g_draw_object = bda_gui_draw_object_create(7u);
        }
        g_controls_dirty = 1;
        g_full_redraw = 1;
    } else if (message == BDA_MSG_DRAW_CONTEXT_DETACH) {
        if (!g_draw_owner || g_draw_owner == handle) {
            release_draw_context();
        }
        clear_touch_state();
        if (!g_modal_active) {
            if (g_exit_cause == EXIT_CAUSE_NONE) {
                g_exit_cause = EXIT_CAUSE_FRAME_DETACH;
            }
            g_detached = 1;
        }
    }
    (void)wparam;
    (void)lparam;
    return bda_gui_default_proc(handle, message, wparam, lparam);
}

static int copy_rom_path(char *destination, u32 capacity, const char *source)
{
    u32 length = (u32)strlen(source);
    if (length == 0u || length + 1u > capacity) {
        return 0;
    }
    memcpy(destination, source, length + 1u);
    return 1;
}

static void set_log_path_from_rom(void)
{
    static const char filename[] = "GBA9588.LOG";
    const char *forward_slash = strrchr(g_rom_path, '/');
    const char *back_slash = strrchr(g_rom_path, '\\');
    const char *slash = forward_slash;
    size_t directory_length = slash ? (size_t)(slash - g_rom_path + 1) : 0u;
    if (back_slash && (!slash || back_slash > slash)) {
        slash = back_slash;
        directory_length = (size_t)(slash - g_rom_path + 1);
    }
    if (directory_length + sizeof(filename) > sizeof(g_log_path)) {
        return;
    }
    memcpy(g_log_path, g_rom_path, directory_length);
    memcpy(g_log_path + directory_length, filename, sizeof(filename));
}

static int select_rom(void)
{
    static const char default_path[] = "A:\\GAMEBOY\\";
    int result = bda_gui_select_file(
        &g_selector, default_path, "gba", g_rom_selector_title
    );
    if (result != BDA_FILE_SELECTOR_SELECTED) {
        return result;
    }
    if (!copy_rom_path(
            g_rom_path, (u32)sizeof(g_rom_path), g_selector.path
        )) {
        return BDA_FILE_SELECTOR_ERROR;
    }
    set_log_path_from_rom();
    return BDA_FILE_SELECTOR_SELECTED;
}

static int initialize_core(void)
{
    struct retro_game_info game;
    void *save_data;
    u32 save_size;
    memset(&game, 0, sizeof(game));
    game.path = g_rom_path;
    retro_set_environment(environment_callback);
    retro_set_video_refresh(video_callback);
    retro_set_audio_sample_batch(audio_callback);
    retro_set_input_poll(input_poll_callback);
    retro_set_input_state(input_state_callback);
    retro_init();
    if (!retro_load_game(&game)) {
        retro_deinit();
        return 0;
    }
    g_core_loaded = 1;
    save_data = retro_get_memory_data(RETRO_MEMORY_SAVE_RAM);
    save_size = (u32)retro_get_memory_size(RETRO_MEMORY_SAVE_RAM);
    if (bbk_save_store_open(
            &g_save_store, g_rom_path, save_data, save_size
        ) < 0) {
        log_text("SAVE_OPEN=ERROR");
    } else {
        log_text("SAVE_OPEN=PASS");
    }
    return 1;
}

static void reset_progress_baseline(void)
{
    u32 audio_samples = g_audio.blocks_written * BBK_AUDIO_BLOCK_SAMPLES +
        g_audio.queued_samples + g_audio.dropped_samples;
    g_progress_last_tick = bda_gui_tick_count_25ms();
    g_progress_last_core_frames = g_core_frames;
    g_progress_last_video_frames = g_video_frames;
    g_progress_last_audio_samples = audio_samples;
}

static void service_help_page(void)
{
    int audio_was_open;
    int help_result;

    if (!g_help_pending || !g_frame) {
        return;
    }
    g_help_pending = 0;
    clear_touch_state();
    g_input_mask = 0u;
    audio_was_open = g_audio.opened;

    log_text("HELP_PAGE_BEGIN");
    if (audio_was_open) {
        bbk_audio_output_stop(&g_audio);
        log_text("HELP_PAGE_AUDIO_STOP=PASS");
    }

    release_draw_context();
    log_text("HELP_PAGE_DRAW_RELEASE=PASS");
    g_modal_active = 1;
    help_result = bda_help_page(0, g_help_title, g_help_body);
    g_modal_active = 0;
    log_value("HELP_PAGE_RESULT=", (u32)help_result);

    if (g_frame) {
        (void)bda_gui_frame_activate(g_frame, 0x100u);
        log_text(
            acquire_draw_context(g_frame) ?
                "HELP_PAGE_DRAW_REACQUIRE=PASS" :
                "HELP_PAGE_DRAW_REACQUIRE=ERROR"
        );
        g_full_redraw = 1;
    }
    if (audio_was_open) {
        bbk_audio_output_start(&g_audio);
        bbk_audio_output_set_muted(&g_audio, !g_audio_enabled);
        log_text("HELP_PAGE_AUDIO_RESTART=PASS");
    }

    reset_progress_baseline();
    clear_touch_state();
    g_controls_dirty = 1;
    log_text("HELP_PAGE_END");
}

static void service_rom_change(void)
{
    char previous_rom_path[BBK_SAVE_PATH_CAPACITY];
    char previous_log_path[BBK_SAVE_PATH_CAPACITY];
    void *save_data;
    int audio_was_open;
    int confirm_result;
    int select_result;
    int save_result;

    if (!g_rom_change_pending || !g_frame || !g_core_loaded) {
        return;
    }
    g_rom_change_pending = 0;
    clear_touch_state();
    g_input_mask = 0u;
    audio_was_open = g_audio.opened;

    log_text("ROM_CHANGE_BEGIN");
    if (audio_was_open) {
        bbk_audio_output_stop(&g_audio);
        log_text("ROM_CHANGE_AUDIO_STOP=PASS");
    }
    release_draw_context();
    log_text("ROM_CHANGE_DRAW_RELEASE=PASS");

    g_modal_active = 1;
    confirm_result = bda_confirm(g_rom_confirm_title, g_rom_confirm_body);
    log_value("ROM_CHANGE_CONFIRM=", (u32)confirm_result);
    if (confirm_result == BDA_DIALOG_RESULT_YES) {
        memcpy(previous_rom_path, g_rom_path, sizeof(previous_rom_path));
        memcpy(previous_log_path, g_log_path, sizeof(previous_log_path));
        select_result = select_rom();
        if (select_result == BDA_FILE_SELECTOR_SELECTED) {
            save_data = retro_get_memory_data(RETRO_MEMORY_SAVE_RAM);
            save_result = bbk_save_store_checkpoint(
                &g_save_store, save_data, 0
            );
            if (save_result < 0) {
                memcpy(g_rom_path, previous_rom_path, sizeof(g_rom_path));
                memcpy(g_log_path, previous_log_path, sizeof(g_log_path));
                log_text("ROM_CHANGE_SAVE=ERROR");
                (void)bda_msgbox(g_save_error_title, g_save_error_body);
            } else {
                if (save_result > 0) {
                    ++g_save_writes;
                }
                log_value("ROM_CHANGE_SAVE=", (u32)save_result);
                shutdown_core();
                g_video_picture.source_pixels = 0;
                if (initialize_core()) {
                    log_text("ROM_CHANGE_LOAD=PASS");
                } else {
                    log_text("ROM_CHANGE_LOAD=ERROR");
                    memcpy(g_rom_path, previous_rom_path, sizeof(g_rom_path));
                    memcpy(g_log_path, previous_log_path, sizeof(g_log_path));
                    if (initialize_core()) {
                        log_text("ROM_CHANGE_RESTORE=PASS");
                        (void)bda_msgbox(
                            g_load_error_title, g_load_restore_body
                        );
                    } else {
                        log_text("ROM_CHANGE_RESTORE=ERROR");
                        (void)bda_msgbox(
                            g_load_error_title, g_load_fatal_body
                        );
                        g_exit_requested = 1;
                    }
                }
            }
        } else if (select_result == BDA_FILE_SELECTOR_CANCELLED) {
            log_text("ROM_CHANGE_SELECT=CANCEL");
        } else {
            log_text("ROM_CHANGE_SELECT=ERROR");
        }
    } else {
        log_text("ROM_CHANGE_CONFIRM=NO");
    }
    g_modal_active = 0;

    if (g_frame) {
        (void)bda_gui_frame_activate(g_frame, 0x100u);
        log_text(
            acquire_draw_context(g_frame) ?
                "ROM_CHANGE_DRAW_REACQUIRE=PASS" :
                "ROM_CHANGE_DRAW_REACQUIRE=ERROR"
        );
        g_full_redraw = 1;
    }
    if (audio_was_open) {
        bbk_audio_output_start(&g_audio);
        bbk_audio_output_set_muted(&g_audio, !g_audio_enabled);
        log_text("ROM_CHANGE_AUDIO_RESTART=PASS");
    }
    reset_progress_baseline();
    clear_touch_state();
    g_controls_dirty = 1;
    log_text("ROM_CHANGE_END");
}

static int initialize_window(bda_frame_desc_t *descriptor)
{
    memset(descriptor, 0, sizeof(*descriptor));
    memset(&g_video_picture, 0, sizeof(g_video_picture));
    memset(&g_controls_picture, 0, sizeof(g_controls_picture));
    g_video_picture.width = GBA_WIDTH;
    g_video_picture.height = GBA_HEIGHT;
    g_video_picture.selected_index = -1;
    g_controls_picture.width = GBA_CONTROLS_WIDTH;
    g_controls_picture.height = GBA_CONTROLS_HEIGHT;
    g_controls_picture.selected_index = -1;
#if defined(BBK_GPSP_CPU_TEST)
    descriptor->title = "GBA RAW INPUT TEST";
#else
    descriptor->title = "GBA RAW INPUT R32";
#endif
    descriptor->wndproc = app_window_proc;
    descriptor->height = SCREEN_WIDTH;
    descriptor->width = SCREEN_HEIGHT;
    g_frame = bda_gui_register_frame_desc(descriptor);
    if (!g_frame || (s32)g_frame == -1) {
        g_frame = 0;
        return 0;
    }
    (void)bda_gui_frame_activate(g_frame, 0x100u);
    (void)acquire_draw_context(g_frame);
    if (!g_draw_object) {
        g_draw_object = bda_gui_draw_object_create(7u);
    }
    if (!g_draw || !g_draw_object || (s32)(u32)g_draw_object == -1) {
        return 0;
    }
    g_controls_pixels = (u16 *)malloc(
        GBA_CONTROLS_WIDTH * GBA_CONTROLS_HEIGHT * sizeof(*g_controls_pixels)
    );
    if (!g_controls_pixels) {
        return 0;
    }
    render_controls();
    return 1;
}

static void service_audio_with_backpressure(void)
{
    u32 wait_start;
    int service_result;
    do {
        service_result = bbk_audio_output_service(&g_audio);
    } while (service_result > 0);
    wait_start = bda_gui_tick_count_25ms();
    while (!g_exit_requested &&
           bbk_audio_output_needs_backpressure(&g_audio)) {
        if (bbk_audio_output_service(&g_audio) <= 0) {
            bda_sys_delay(1u);
        }
        if (bda_gui_tick_elapsed_25ms(
                wait_start, bda_gui_tick_count_25ms()
            ) >= AUDIO_BACKPRESSURE_MAX_TICKS) {
            ++g_audio_backpressure_skips;
            break;
        }
    }
}

static void apply_audio_toggle(void)
{
    if (!g_audio_toggle_pending) {
        return;
    }
    g_audio_toggle_pending = 0;
    g_audio_enabled = !g_audio_enabled;
    bbk_audio_output_set_muted(&g_audio, !g_audio_enabled);
    log_text(g_audio_enabled ? "AUDIO_SWITCH=ON" : "AUDIO_SWITCH=OFF");
    g_controls_dirty = 1;
}

static void log_runtime_progress(void)
{
    char line[384];
    u32 now = bda_gui_tick_count_25ms();
    u32 elapsed_ticks = bda_gui_tick_elapsed_25ms(g_progress_last_tick, now);
    u32 core_delta = g_core_frames - g_progress_last_core_frames;
    u32 video_delta = g_video_frames - g_progress_last_video_frames;
    u32 audio_samples =
        g_audio.blocks_written * BBK_AUDIO_BLOCK_SAMPLES +
        g_audio.queued_samples + g_audio.dropped_samples;
    u32 audio_delta = audio_samples - g_progress_last_audio_samples;
    u32 emu_fps100 = elapsed_ticks != 0u ? core_delta * 4000u / elapsed_ticks : 0u;
    u32 video_fps100 = elapsed_ticks != 0u ? video_delta * 4000u / elapsed_ticks : 0u;
    u32 audio_hz = elapsed_ticks != 0u ? audio_delta * 40u / elapsed_ticks : 0u;

    if (snprintf(
            line, sizeof(line),
            "RUN core=%u emu_fps100=%u video_fps100=%u video_cb=%u video_ok=%u video_err=%u sound=%u audio=%u short=%u drop=%u queue=%u aud_hz=%u bp=%u raw_poll=%u raw_evt=%u raw_max=%u raw_cap=%u raw_ignored=%u touch_down=%u touch_move=%u touch_up=%u",
            g_core_frames, emu_fps100, video_fps100,
            g_video_callbacks, g_video_frames,
            g_video_submit_errors, (u32)g_audio_enabled, g_audio.blocks_written,
            g_audio.short_writes, g_audio.dropped_samples, g_audio.queued_samples,
            audio_hz, g_audio_backpressure_skips,
            g_raw_poll_calls, g_raw_event_count, g_raw_event_max_batch,
            g_raw_event_cap_hits, g_raw_event_ignored,
            g_raw_touch_down_count, g_raw_touch_move_count,
            g_raw_touch_up_count
        ) > 0) {
        log_text(line);
    }
    g_progress_last_tick = now;
    g_progress_last_core_frames = g_core_frames;
    g_progress_last_video_frames = g_video_frames;
    g_progress_last_audio_samples = audio_samples;
}

static void close_window(void)
{
    if (!g_frame) {
        free(g_controls_pixels);
        g_controls_pixels = 0;
        return;
    }
    g_touch_mask = 0u;
    (void)bda_gui_frame_stop(g_frame);
    (void)bda_gui_frame_release(g_frame);
    release_draw_context();
    bda_gui_close_frame(g_frame);
    g_frame = 0;
    free(g_controls_pixels);
    g_controls_pixels = 0;
}

static void shutdown_core(void)
{
    if (!g_core_loaded) {
        return;
    }
    retro_unload_game();
    retro_deinit();
    g_core_loaded = 0;
}

int bda_main(void)
{
    bda_frame_desc_t descriptor;
    void *save_data;
    int file;
    int select_result;
    int result = 0;

    g_log_path[0] = 0;
    memset(&g_audio, 0, sizeof(g_audio));
    memset(&g_save_store, 0, sizeof(g_save_store));
    g_frame = 0;
    g_draw = 0;
    g_draw_owner = 0;
    g_draw_object = 0;
    g_controls_pixels = 0;
    g_touch_mask = 0u;
    g_touch_x = 0u;
    g_touch_y = 0u;
    g_input_mask = 0u;
    g_rendered_controls_mask = ~0u;
    g_escape_max_ticks = 0u;
    g_touch_start_max_ticks = 0u;
    g_exit_cause = EXIT_CAUSE_NONE;
    g_escape_down = 0;
    g_touch_start_down = 0;
    g_raw_touch_down = 0;
    g_sound_touch_down = 0;
    g_help_touch_down = 0;
    g_help_pending = 0;
    g_modal_active = 0;
    g_rom_touch_down = 0;
    g_rom_change_pending = 0;
    g_controls_dirty = 1;
    g_full_redraw = 0;
    g_audio_enabled = 1;
    g_audio_toggle_pending = 0;
    g_exit_requested = 0;
    g_detached = 0;
    g_core_loaded = 0;
    g_core_frames = 0u;
    g_video_frames = 0u;
    g_video_callbacks = 0u;
    g_video_submit_errors = 0u;
    g_controls_submit_errors = 0u;
    g_audio_backpressure_skips = 0u;
    g_controls_render_attempts = 0u;
    g_audio_callbacks = 0u;
    g_save_writes = 0u;
    g_progress_last_tick = 0u;
    g_progress_last_core_frames = 0u;
    g_progress_last_video_frames = 0u;
    g_progress_last_audio_samples = 0u;
    g_raw_poll_calls = 0u;
    g_raw_event_count = 0u;
    g_raw_event_max_batch = 0u;
    g_raw_event_cap_hits = 0u;
    g_raw_touch_down_count = 0u;
    g_raw_touch_move_count = 0u;
    g_raw_touch_up_count = 0u;
    g_raw_event_ignored = 0u;
    g_diag_loop_stage = 0u;
    g_diag_loop_frame = 0u;

    select_result = select_rom();
    if (select_result == BDA_FILE_SELECTOR_CANCELLED) {
        log_text("ROM_SELECT=CANCEL");
        return 0;
    }
    if (select_result != BDA_FILE_SELECTOR_SELECTED) {
        bda_msgbox("GBA Emulator", "ROM selection failed");
        return 1;
    }
    file = open_log("wb");
    if (bda_fs_file_is_valid(file)) {
        (void)bda_fs_close_raw(file);
    }
#if defined(BBK_GPSP_CPU_TEST)
    log_text("BBK9588 GBA RAW INPUT TEST");
    log_text("BUILD_ID=DRC_R1_RAW_INPUT_8K_TEST_R32");
#else
    log_text("BBK9588 GBA RAW INPUT R32");
    log_text("BUILD_ID=DRC_R1_30FPS_RAW_INPUT_8K_AUDIO_R32");
    log_text("HOST_IRQ_WINDOW=UPDATE_GBA_GP_SAFE");
    log_text("DRC_IRQ_POLICY=OPEN_DURING_CORE_C_CALLBACKS");
    log_text("IRQ_ENABLE_ORDER=RESTORE_APP_GP_THEN_STATUS");
    log_text("IRQ_DISABLE_REGS=T0_T1_EXCEPTION_PRESERVED");
    log_text("HELP_PAGE=SDK_PARENT0_RELEASE_REACQUIRE");
    log_text("HELP_LANGUAGE=GBK_CHINESE");
    log_text("ROM_SWITCH=CONFIRM_SAVE_RELOAD");
    log_text("INPUT_ARCH=RAW_EVENT_SELF_MANAGED");
    log_value("INPUT_POLL_CYCLES=", BBK9588_INPUT_POLL_CYCLES);
    log_value("RAW_EVENT_MAX_PER_POLL=", RAW_EVENT_MAX_PER_POLL);
    log_text("PHYSICAL_KEYS=INPUT_PACKET_ONCE_PER_POLL");
    log_text("WINDOW_EVENT_PUMP=DISABLED");
    log_text("WINDOW_TIMER=DISABLED");
    log_text("AUDIO_WAIT_EVENT_PUMP=OFF_PCM_PRIORITY");
    log_text("TOUCH_LEVEL=RAW_EVENTS_8_12_11");
    log_text("TOUCH_COORDINATES=GUI_TOUCH_POSITION");
    log_text("RAW_EVENT_VALUE=IGNORED");
    log_text("STARTUP_MOVE_UP=IGNORED_UNTIL_DOWN");
    log_text("TOUCH_START_EXIT=DISABLED");
    log_text("RUNTIME_LOG_INTERVAL_FRAMES=600");
    log_text("PERIODIC_SAVE=DISABLED_EXIT_ONLY");
    log_text("SAVE_CRC=BITWISE_STABLE");
    log_text("GPIO_AND_FIXED_TOUCH_FUNCTION=DISABLED_DRC_UNSAFE");
    log_text("MILLISECOND_TIMER=DISABLED_DRC_IRQ_CONFLICT");
    log_value("DIAG_LOOP_STAGE_ADDR=", (u32)&g_diag_loop_stage);
    log_value("DIAG_LOOP_FRAME_ADDR=", (u32)&g_diag_loop_frame);
    log_value("DIAG_CORE_STAGE_ADDR=", (u32)&bbk9588_run_stage);
#endif
#if defined(HAVE_DYNAREC)
    log_text("CORE_MODE=DRC");
#else
    log_text("CORE_MODE=INTERPRETER");
#endif
    log_text("EMULATION_TARGET_FPS=59.7275");
#if defined(BBK_GPSP_CPU_TEST)
    log_text("VIDEO_TARGET_FPS=1");
    log_text("FRAMESKIP_INTERVAL=59");
#else
    log_text("VIDEO_TARGET_FPS=30");
    log_text("FRAMESKIP_INTERVAL=1");
#endif
    log_text("ROM_SELECT=PASS");
    log_value("ROM_PATH_LEN=", (u32)strlen(g_rom_path));
    log_value(
        "ROM_PATH_VOLUME=",
        g_rom_path[0] != 0 && g_rom_path[1] == ':' ? (u8)g_rom_path[0] : 0u
    );
    if (!initialize_core()) {
        log_text("ROM_LOAD=ERROR");
        log_text("ROM_DIR_REQUIRED=A:\\GAMEBOY\\");
        bda_msgbox("GBA Emulator", "Put ROM in A:\\GAMEBOY\\");
        return 2;
    }
    log_text("ROM_LOAD=PASS");
    log_text("WINDOW_BEGIN");
    if (!initialize_window(&descriptor)) {
        log_text("WINDOW=ERROR");
        close_window();
        shutdown_core();
        return 3;
    }
    log_text("WINDOW=PASS");
    log_text("AUDIO_INIT_BEGIN");
    bbk_audio_output_init(&g_audio);
    bbk_audio_output_set_muted(&g_audio, 0);
    log_text("AUDIO_INIT=PASS");
    log_text("AUDIO_START_BEGIN");
    bbk_audio_output_start(&g_audio);
    log_text("AUDIO_START=PASS");
    log_text("AUDIO_DEFAULT=ON");
    g_progress_last_tick = bda_gui_tick_count_25ms();
    log_text("LOOP_BEGIN");

    while (!g_exit_requested && !g_detached) {
        g_diag_loop_frame = g_core_frames + 1u;
        g_diag_loop_stage = 1u;
        g_diag_loop_stage = 2u;
        apply_audio_toggle();
        service_help_page();
        service_rom_change();
        if (g_exit_requested || g_detached || !g_core_loaded) {
            break;
        }
        if (g_core_frames < 2u) {
            log_value("FRAME_BEGIN=", g_core_frames + 1u);
        }
        g_diag_loop_stage = 3u;
        retro_run();
        g_diag_loop_stage = 4u;
        ++g_core_frames;
        if (g_core_frames <= 2u) {
            log_value("FRAME_END=", g_core_frames);
        }
        render_full_surface_if_needed();
        render_controls();
        g_diag_loop_stage = 5u;
        g_diag_loop_stage = 6u;
        service_audio_with_backpressure();
        g_diag_loop_stage = 7u;
        if (g_core_frames == 1u ||
            g_core_frames % DIAGNOSTIC_FRAME_INTERVAL == 0u) {
            log_runtime_progress();
        }
        g_diag_loop_stage = 8u;
    }

    g_diag_loop_stage = 0xffu;

    if (g_core_frames != g_progress_last_core_frames) {
        log_runtime_progress();
    }
    log_value("CORE_FRAMES=", g_core_frames);
    log_value("LOOP_EXIT_CAUSE=", g_exit_cause);
    log_value("LOOP_EXIT_REQUESTED=", (u32)g_exit_requested);
    log_value("LOOP_DETACHED=", (u32)g_detached);
    log_value("ESCAPE_MAX_TICKS=", g_escape_max_ticks);
    log_value("TOUCH_START_MAX_TICKS=", g_touch_start_max_ticks);
    log_value("EXIT_INPUT_MASK=", g_input_mask);
    bbk_audio_output_stop(&g_audio);
    log_text("AUDIO_STOP=PASS");
    save_data = retro_get_memory_data(RETRO_MEMORY_SAVE_RAM);
    log_text("SAVE_EXIT_BEGIN");
    {
        int save_result = bbk_save_store_checkpoint(&g_save_store, save_data, 0);
        log_value("SAVE_EXIT_RESULT=", (u32)save_result);
        if (save_result < 0) {
            result = 4;
        } else if (save_result > 0) {
            ++g_save_writes;
        }
    }
    close_window();
    shutdown_core();
    log_value("VIDEO_FRAMES=", g_video_frames);
    log_value("VIDEO_CALLBACKS=", g_video_callbacks);
    log_value("VIDEO_SUBMIT_ERRORS=", g_video_submit_errors);
    log_value("CONTROLS_SUBMIT_ERRORS=", g_controls_submit_errors);
    log_value("AUDIO_BLOCKS=", g_audio.blocks_written);
    log_value("AUDIO_DROPPED=", g_audio.dropped_samples);
    log_value("AUDIO_SHORT_WRITES=", g_audio.short_writes);
    log_value("AUDIO_BACKPRESSURE_SKIPS=", g_audio_backpressure_skips);
    log_value("AUDIO_ENABLED=", (u32)g_audio_enabled);
    log_value("RAW_POLL_CALLS=", g_raw_poll_calls);
    log_value("RAW_EVENT_COUNT=", g_raw_event_count);
    log_value("RAW_EVENT_MAX_BATCH=", g_raw_event_max_batch);
    log_value("RAW_EVENT_CAP_HITS=", g_raw_event_cap_hits);
    log_value("RAW_EVENT_IGNORED=", g_raw_event_ignored);
    log_value("RAW_TOUCH_DOWN=", g_raw_touch_down_count);
    log_value("RAW_TOUCH_MOVE=", g_raw_touch_move_count);
    log_value("RAW_TOUCH_UP=", g_raw_touch_up_count);
    log_value("SAVE_WRITES=", g_save_writes);
    log_text(result ? "RESULT=FAIL" : "RESULT=PASS");
    return result;
}
