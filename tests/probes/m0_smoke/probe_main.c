#include "bda_dialogs.h"
#include "bda_filesystem.h"
#include "bda_memory.h"
#include "bda_time.h"
#include "bda_types.h"

extern u32 probe_asm_magic(void);
extern u32 probe_cp0_count(void);
extern u32 probe_c_mix(u32 value);
extern u32 probe_cpp_constructor_value(void);
extern u32 probe_cpp_add(u32 value);

static volatile u32 g_zero_bss[64];
static volatile u32 g_initialized_data = 0x13579bdfu;

static u32 string_length(const char *text)
{
    u32 length = 0;
    while (text[length]) {
        ++length;
    }
    return length;
}

static void log_text(int file, const char *text)
{
    (void)bda_fs_write_raw(file, text, string_length(text));
    (void)bda_fs_write_raw(file, "\r\n", 2u);
}

static void log_hex(int file, const char *label, u32 value)
{
    static const char digits[] = "0123456789ABCDEF";
    char line[64];
    u32 offset = 0;
    int shift;

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
    (void)bda_fs_write_raw(file, line, offset);
}

int bda_main(void)
{
    int file = bda_fs_fopen_raw("M0SMOKE.LOG", "wb");
    u32 count_start = probe_cp0_count();
    u32 count_end;
    u8 *heap;
    u32 index;
    int ok = bda_fs_file_is_valid(file);

    if (ok) {
        log_text(file, "BBK9588 M0 SMOKE");
    }

    for (index = 0; index < sizeof(g_zero_bss) / sizeof(g_zero_bss[0]); ++index) {
        if (g_zero_bss[index] != 0u) {
            ok = 0;
        }
    }
    ok = ok && g_initialized_data == 0x13579bdfu;
    ok = ok && probe_asm_magic() == 0x95884740u;
    ok = ok && probe_cpp_constructor_value() == 0x95884740u;
    ok = ok && probe_cpp_add(1u) == 0x95884741u;
    ok = ok && probe_c_mix(0x12345678u) == 0x87985aa5u;

    heap = (u8 *)bda_alloc(4096u);
    ok = ok && heap != 0;
    if (heap) {
        heap[0] = 0x5au;
        heap[4095] = 0xa5u;
        ok = ok && heap[0] == 0x5au && heap[4095] == 0xa5u;
        bda_free(heap);
    }

    bda_sys_delay(1);
    count_end = probe_cp0_count();
    ok = ok && count_end != count_start;

    if (bda_fs_file_is_valid(file)) {
        log_hex(file, "CP0_START=", count_start);
        log_hex(file, "CP0_END=", count_end);
        log_text(file, ok ? "RESULT=PASS" : "RESULT=FAIL");
        (void)bda_fs_close_raw(file);
    }
    bda_msgbox("GBA M0", ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}
