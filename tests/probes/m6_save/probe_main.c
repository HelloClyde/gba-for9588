#include "platform/bbk9588/save_store.h"

#include "bda_filesystem.h"
#include "bda_memory.h"
#include "bda_types.h"

#include <string.h>

#define SAVE_SIZE (128u * 1024u)

static const char g_rom_path[] = "SAVEPRB.GBA";
static const char g_marker_path[] = "SAVEPRB.RUN";
static const char g_log_path[] = "M6SAVE.LOG";

static u32 string_length(const char *text)
{
    u32 length = 0u;
    while (text[length]) {
        ++length;
    }
    return length;
}

static void log_text(const char *text)
{
    int file = bda_fs_fopen_raw(g_log_path, "ab");
    if (!bda_fs_file_is_valid(file)) {
        return;
    }
    (void)bda_fs_write_raw(file, text, string_length(text));
    (void)bda_fs_write_raw(file, "\r\n", 2u);
    (void)bda_fs_close_raw(file);
}

static void log_value(const char *label, u32 value)
{
    static const char digits[] = "0123456789ABCDEF";
    char line[64];
    u32 offset = 0u;
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
    file = bda_fs_fopen_raw(g_log_path, "ab");
    if (bda_fs_file_is_valid(file)) {
        (void)bda_fs_write_raw(file, line, offset);
        (void)bda_fs_close_raw(file);
    }
}

static void fill_pattern(u8 *payload, u8 seed)
{
    u32 index;
    for (index = 0u; index < SAVE_SIZE; ++index) {
        payload[index] = (u8)(seed + (u8)(index * 37u));
    }
}

static int matches_pattern(const u8 *payload, u8 seed)
{
    u32 index;
    for (index = 0u; index < SAVE_SIZE; ++index) {
        if (payload[index] != (u8)(seed + (u8)(index * 37u))) {
            return 0;
        }
    }
    return 1;
}

static int marker_exists(void)
{
    int file = bda_fs_fopen_raw(g_marker_path, "rb");
    if (!bda_fs_file_is_valid(file)) {
        return 0;
    }
    (void)bda_fs_close_raw(file);
    return 1;
}

static int write_marker(void)
{
    static const char marker[] = "stage1\r\n";
    int file = bda_fs_fopen_raw(g_marker_path, "wb");
    int ok;
    if (!bda_fs_file_is_valid(file)) {
        return 0;
    }
    ok = bda_fs_write_raw(file, marker, sizeof(marker) - 1u) ==
        (int)(sizeof(marker) - 1u);
    (void)bda_fs_close_raw(file);
    return ok;
}

static int corrupt_active_slot(const bbk_save_store_t *store)
{
    static const u8 truncated = 0xa5u;
    int file = bda_fs_fopen_raw(store->slot_path[store->active_slot], "wb");
    int ok;
    if (!bda_fs_file_is_valid(file)) {
        return 0;
    }
    ok = bda_fs_write_raw(file, &truncated, 1u) == 1;
    (void)bda_fs_close_raw(file);
    return ok;
}

static int run_stage_one(u8 *payload)
{
    bbk_save_store_t store;
    bbk_save_store_t recovered;
    u32 generation_a;
    int ok = 1;

    memset(payload, 0, SAVE_SIZE);
    if (bbk_save_store_open(&store, g_rom_path, payload, SAVE_SIZE) < 0) {
        return 0;
    }
    fill_pattern(payload, 0x31u);
    ok = ok && bbk_save_store_checkpoint(&store, payload, 1) == 1;
    generation_a = store.generation;
    fill_pattern(payload, 0x92u);
    ok = ok && bbk_save_store_checkpoint(&store, payload, 1) == 1;
    ok = ok && corrupt_active_slot(&store);

    memset(payload, 0, SAVE_SIZE);
    ok = ok && bbk_save_store_open(
        &recovered, g_rom_path, payload, SAVE_SIZE
    ) == 1;
    ok = ok && recovered.generation == generation_a;
    ok = ok && matches_pattern(payload, 0x31u);
    log_value("FALLBACK_GENERATION=", recovered.generation);
    log_text(ok ? "INTERRUPT_FALLBACK=PASS" : "INTERRUPT_FALLBACK=FAIL");

    fill_pattern(payload, 0x92u);
    ok = ok && bbk_save_store_checkpoint(&recovered, payload, 1) == 1;
    ok = ok && write_marker();
    log_value("STAGE1_FINAL_GENERATION=", recovered.generation);
    log_text(ok ? "STAGE1=PASS" : "STAGE1=FAIL");
    return ok;
}

static int run_stage_two(u8 *payload)
{
    bbk_save_store_t store;
    int opened;
    int ok;
    memset(payload, 0, SAVE_SIZE);
    opened = bbk_save_store_open(&store, g_rom_path, payload, SAVE_SIZE);
    ok = opened == 1 && matches_pattern(payload, 0x92u);
    log_value("REBOOT_GENERATION=", store.generation);
    log_text(ok ? "CROSS_REBOOT=PASS" : "CROSS_REBOOT=FAIL");
    log_text(ok ? "RESULT=PASS" : "RESULT=FAIL");
    return ok;
}

int bda_main(void)
{
    u8 *payload = (u8 *)bda_alloc(SAVE_SIZE);
    int log;
    int ok;
    if (!payload || (u32)payload == 0xffffffffu) {
        return 1;
    }
    if (!marker_exists()) {
        log = bda_fs_fopen_raw(g_log_path, "wb");
        if (bda_fs_file_is_valid(log)) {
            (void)bda_fs_close_raw(log);
        }
        log_text("BBK9588 M6 SAVE STAGE1");
        ok = run_stage_one(payload);
    } else {
        log_text("BBK9588 M6 SAVE STAGE2");
        ok = run_stage_two(payload);
    }
    bda_free(payload);
    return ok ? 0 : 1;
}
