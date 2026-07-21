#include "bda_dialogs.h"
#include "bda_filesystem.h"
#include "bda_memory.h"
#include "bda_time.h"
#include "bda_types.h"

#define HEAP_CHUNK_SIZE (256u * 1024u)
#define HEAP_CHUNK_COUNT 32u

typedef u32 (*jit_function_t)(void);

extern void bda_cache_sync(void *start, void *end);
extern u32 probe_cp0_count(void);

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

static int valid_allocation(const void *pointer)
{
    return pointer != 0 && (u32)pointer != 0xffffffffu;
}

static int probe_jit(int file)
{
    volatile u32 *code = (volatile u32 *)bda_alloc(64u);
    jit_function_t function;
    u32 first;
    u32 second;

    if (!valid_allocation((const void *)code)) {
        log_text(file, "JIT_ALLOC=FAIL");
        return 0;
    }

    code[0] = 0x2402002au;
    code[1] = 0x03e00008u;
    code[2] = 0x00000000u;
    bda_cache_sync((void *)code, (void *)(code + 3));
    function = (jit_function_t)(u32)code;
    first = function();

    code[0] = 0x2402002bu;
    bda_cache_sync((void *)code, (void *)(code + 3));
    second = function();

    log_hex(file, "JIT_ADDRESS=", (u32)code);
    log_hex(file, "JIT_FIRST=", first);
    log_hex(file, "JIT_SECOND=", second);
    bda_free((void *)code);
    return first == 42u && second == 43u;
}

static u32 probe_heap(int file, int *fragmentation_ok)
{
    u8 *chunks[HEAP_CHUNK_COUNT];
    u32 count = 0;
    u32 allocated;
    u32 index;
    int contents_ok = 1;
    u8 *large;

    for (index = 0; index < HEAP_CHUNK_COUNT; ++index) {
        u8 *memory = (u8 *)bda_alloc(HEAP_CHUNK_SIZE);
        if (!valid_allocation(memory)) {
            break;
        }
        chunks[count++] = memory;
        memory[0] = (u8)(0x40u + index);
        memory[HEAP_CHUNK_SIZE / 2u] = (u8)(0x80u + index);
        memory[HEAP_CHUNK_SIZE - 1u] = (u8)(0xc0u + index);
    }
    allocated = count;

    for (index = 0; index < count; ++index) {
        u8 *memory = chunks[index];
        if (memory[0] != (u8)(0x40u + index) ||
            memory[HEAP_CHUNK_SIZE / 2u] != (u8)(0x80u + index) ||
            memory[HEAP_CHUNK_SIZE - 1u] != (u8)(0xc0u + index)) {
            contents_ok = 0;
            break;
        }
    }

    index = allocated;
    while (index != 0u) {
        --index;
        bda_free(chunks[index]);
    }

    large = (u8 *)bda_alloc(2u * 1024u * 1024u);
    *fragmentation_ok = valid_allocation(large);
    if (*fragmentation_ok) {
        large[0] = 0x5au;
        large[2u * 1024u * 1024u - 1u] = 0xa5u;
        *fragmentation_ok = large[0] == 0x5au &&
            large[2u * 1024u * 1024u - 1u] == 0xa5u;
        bda_free(large);
    }

    log_hex(file, "HEAP_CHUNKS_256K=", allocated);
    log_hex(file, "HEAP_BYTES=", allocated * HEAP_CHUNK_SIZE);
    log_hex(file, "HEAP_CONTENTS=", (u32)contents_ok);
    log_hex(file, "HEAP_FRAGMENT_2M=", (u32)*fragmentation_ok);
    return contents_ok ? allocated : 0u;
}

static int probe_clock(int file)
{
    u32 tick_start = bda_gui_tick_count_25ms();
    u32 tick_end;
    u32 count_start;
    u32 count_end;
    u32 ticks;
    u32 count_per_tick;

    do {
        bda_sys_delay(1);
        tick_end = bda_gui_tick_count_25ms();
    } while (tick_end == tick_start);

    tick_start = tick_end;
    count_start = probe_cp0_count();
    do {
        bda_sys_delay(1);
        tick_end = bda_gui_tick_count_25ms();
    } while (bda_gui_tick_elapsed_25ms(tick_start, tick_end) < 8u);
    count_end = probe_cp0_count();
    ticks = bda_gui_tick_elapsed_25ms(tick_start, tick_end);
    count_per_tick = (count_end - count_start) / ticks;

    log_hex(file, "CLOCK_TICKS=", ticks);
    log_hex(file, "CLOCK_COUNT_DELTA=", count_end - count_start);
    log_hex(file, "CLOCK_COUNT_PER_25MS=", count_per_tick);
    return ticks >= 8u && count_end != count_start && count_per_tick != 0u;
}

int bda_main(void)
{
    int file = bda_fs_fopen_raw("M1RUNTIME.LOG", "wb");
    int fragmentation_ok = 0;
    int ok;
    u32 heap_chunks;

    if (!bda_fs_file_is_valid(file)) {
        bda_msgbox("GBA M1", "LOG FAIL");
        return 1;
    }

    log_text(file, "BBK9588 M1 RUNTIME");
    log_text(file, "LIBGCC_CLEAR_CACHE=NOOP");
    ok = probe_jit(file);
    heap_chunks = probe_heap(file, &fragmentation_ok);
    ok = ok && heap_chunks >= 24u && fragmentation_ok;
    ok = ok && probe_clock(file);
    log_text(file, ok ? "RESULT=PASS" : "RESULT=FAIL");
    (void)bda_fs_close_raw(file);

    bda_msgbox("GBA M1", ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}
