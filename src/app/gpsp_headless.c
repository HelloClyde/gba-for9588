#include "bda_dialogs.h"
#include "bda_filesystem.h"
#include "bda_time.h"
#include "bda_types.h"

#include <libretro.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#if defined(HAVE_DYNAREC)
extern u8 *rom_translation_cache;
extern u8 *ram_translation_cache;
#endif

#ifndef HEADLESS_FRAMES
#define HEADLESS_FRAMES 3u
#endif

#define HEADLESS_LOG_INTERVAL 60u
#define HEADLESS_VIDEO_WIDTH 240u
#define HEADLESS_VIDEO_HEIGHT 160u
#define HEADLESS_VIDEO_PIXELS (HEADLESS_VIDEO_WIDTH * HEADLESS_VIDEO_HEIGHT)
#define GPSP_REG_PC 15u
#define GPSP_REG_CPSR 16u

extern u32 reg[64];
extern int selected_boot_mode;

static u32 g_video_frames;
static u32 g_video_hash = 2166136261u;
static u32 g_video_nonzero;
static u32 g_video_last_hash;
static u32 g_video_changed_frames;
static u16 g_video_last_frame[HEADLESS_VIDEO_PIXELS];
static u32 g_audio_frames;
static u32 g_audio_hash = 2166136261u;

static int open_log(const char *mode)
{
    return bda_fs_fopen_raw("A:\\GAMEBOY\\GPSPHL.LOG", mode);
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

static void log_hex(const char *label, u32 value)
{
    char line[96];
    int length = snprintf(line, sizeof(line), "%s0x%08X", label, value);
    if (length > 0) {
        log_text(line);
    }
}

static int write_video_frame(void)
{
    int file;
    int written;
    int closed;
    if (g_video_frames == 0u) {
        return 0;
    }
    file = bda_fs_fopen_raw("A:\\GAMEBOY\\GPSPHL.RAW", "wb");
    if (!bda_fs_file_is_valid(file)) {
        return 0;
    }
    written = bda_fs_write_raw(file, g_video_last_frame,
        (bda_size_t)sizeof(g_video_last_frame));
    closed = bda_fs_close_raw(file);
    return written == (int)sizeof(g_video_last_frame) && closed == 0;
}

static void frontend_log(enum retro_log_level level, const char *format, ...)
{
    char line[192];
    va_list args;
    (void)level;
    va_start(args, format);
    (void)vsnprintf(line, sizeof(line), format, args);
    va_end(args);
    log_text(line);
}

static const char *variable_value(const char *key)
{
    if (strcmp(key, "gpsp_drc") == 0) return "enabled";
    if (strcmp(key, "gpsp_bios") == 0) return "builtin";
    if (strcmp(key, "gpsp_boot_mode") == 0) return "game";
    if (strcmp(key, "gpsp_rtc") == 0) return "auto";
    if (strcmp(key, "gpsp_serial") == 0) return "disabled";
    if (strcmp(key, "gpsp_rumble") == 0) return "disabled";
    if (strcmp(key, "gpsp_sprlim") == 0) return "disabled";
    if (strcmp(key, "gpsp_frameskip") == 0) return "disabled";
    if (strcmp(key, "gpsp_frameskip_threshold") == 0) return "33";
    if (strcmp(key, "gpsp_frameskip_interval") == 0) return "0";
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

static void video_callback(
    const void *data, unsigned width, unsigned height, size_t pitch
)
{
    const u8 *row = (const u8 *)data;
    u32 frame_hash = 2166136261u;
    unsigned y;
    if (!data || width != HEADLESS_VIDEO_WIDTH ||
        height != HEADLESS_VIDEO_HEIGHT || pitch < width * 2u) {
        return;
    }
    ++g_video_frames;
    for (y = 0; y < height; ++y) {
        const u16 *pixels = (const u16 *)row;
        unsigned x;
        for (x = 0; x < width; ++x) {
            u16 pixel = pixels[x];
            g_video_last_frame[y * HEADLESS_VIDEO_WIDTH + x] = pixel;
            g_video_hash = (g_video_hash ^ pixel) * 16777619u;
            frame_hash = (frame_hash ^ pixel) * 16777619u;
            if (pixel != 0u) {
                ++g_video_nonzero;
            }
        }
        row += pitch;
    }
    if (g_video_frames > 1u && frame_hash != g_video_last_hash) {
        ++g_video_changed_frames;
    }
    g_video_last_hash = frame_hash;
}

static size_t audio_callback(const int16_t *data, size_t frames)
{
    size_t index;
    for (index = 0; index < frames * 2u; ++index) {
        g_audio_hash = (g_audio_hash ^ (u16)data[index]) * 16777619u;
    }
    g_audio_frames += (u32)frames;
    return frames;
}

static void input_poll_callback(void)
{
}

static int16_t input_state_callback(
    unsigned port, unsigned device, unsigned index, unsigned id
)
{
    (void)port;
    (void)device;
    (void)index;
    (void)id;
    return 0;
}

int bda_main(void)
{
    struct retro_game_info game;
    int file = open_log("wb");
    u32 frame;
    int loaded;
    int ok;
    int video_frame_written;

    if (bda_fs_file_is_valid(file)) {
        (void)bda_fs_close_raw(file);
    }
    log_text("BBK9588 GPSP HEADLESS");
#if defined(HAVE_DYNAREC)
    log_text("CORE_MODE=DYNAREC");
#else
    log_text("CORE_MODE=INTERPRETER");
#endif
    g_video_frames = 0;
    g_video_hash = 2166136261u;
    g_video_nonzero = 0;
    g_video_last_hash = 0u;
    g_video_changed_frames = 0u;
    memset(g_video_last_frame, 0, sizeof(g_video_last_frame));
    g_audio_frames = 0;
    g_audio_hash = 2166136261u;

    retro_set_environment(environment_callback);
    retro_set_video_refresh(video_callback);
    retro_set_audio_sample_batch(audio_callback);
    retro_set_input_poll(input_poll_callback);
    retro_set_input_state(input_state_callback);
    log_text("RETRO_INIT=BEGIN");
    retro_init();
    log_text("RETRO_INIT=END");
#if defined(HAVE_DYNAREC)
    log_hex("JIT_ROM_CACHE=", (u32)rom_translation_cache);
    log_hex("JIT_RAM_CACHE=", (u32)ram_translation_cache);
#endif

    memset(&game, 0, sizeof(game));
    game.path = "A:\\GAMEBOY\\TEST.GBA";
    log_text("LOAD_GAME=BEGIN");
    loaded = retro_load_game(&game);
    log_hex("LOAD_GAME=", (u32)loaded);
    if (loaded) {
        log_hex("BOOT_MODE=", (u32)selected_boot_mode);
        log_hex("LOAD_PC=", reg[GPSP_REG_PC]);
        log_hex("LOAD_CPSR=", reg[GPSP_REG_CPSR]);
        for (frame = 0; frame < HEADLESS_FRAMES; ++frame) {
            u32 tick_start = bda_gui_tick_count_25ms();
            u32 frame_ticks;
            retro_run();
            frame_ticks = bda_gui_tick_elapsed_25ms(
                tick_start, bda_gui_tick_count_25ms()
            );
            if (frame == 0u || (frame + 1u) % HEADLESS_LOG_INTERVAL == 0u ||
                frame + 1u == HEADLESS_FRAMES) {
                log_hex("FRAME_INDEX=", frame + 1u);
                log_hex("FRAME_TICKS=", frame_ticks);
                log_hex("FRAME_PC=", reg[GPSP_REG_PC]);
                log_hex("FRAME_CPSR=", reg[GPSP_REG_CPSR]);
                log_hex("FRAME_HASH=", g_video_last_hash);
            }
        }
        retro_unload_game();
    }
    retro_deinit();
    video_frame_written = write_video_frame();

    log_hex("VIDEO_FRAMES=", g_video_frames);
    log_hex("VIDEO_HASH=", g_video_hash);
    log_hex("VIDEO_LAST_HASH=", g_video_last_hash);
    log_hex("VIDEO_CHANGED_FRAMES=", g_video_changed_frames);
    log_hex("VIDEO_NONZERO=", g_video_nonzero);
    log_hex("AUDIO_FRAMES=", g_audio_frames);
    log_hex("AUDIO_HASH=", g_audio_hash);
    log_hex("GBA_PC=", reg[GPSP_REG_PC]);
    log_hex("GBA_CPSR=", reg[GPSP_REG_CPSR]);
    log_text(video_frame_written ? "VIDEO_RAW_WRITE=PASS" : "VIDEO_RAW_WRITE=FAIL");
    ok = loaded && g_video_frames == HEADLESS_FRAMES &&
        g_video_nonzero != 0u && g_audio_frames != 0u && video_frame_written;
    log_text(ok ? "RESULT=PASS" : "RESULT=FAIL");
    bda_msgbox("gpSP Headless", ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}
