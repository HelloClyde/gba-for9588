#include "platform/bbk9588/save_store.h"

#include "bda_filesystem.h"

#include <string.h>

#define SAVE_MAGIC 0x38565347u
#define SAVE_VERSION 1u

typedef struct save_header {
    u32 magic;
    u32 version;
    u32 generation;
    u32 payload_size;
    u32 payload_crc;
    u32 reserved;
} save_header_t;

static u32 crc32_update(u32 crc, const u8 *data, u32 size)
{
    u32 index;
    for (index = 0; index < size; ++index) {
        u32 bit;
        crc ^= data[index];
        for (bit = 0; bit < 8u; ++bit) {
            crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
        }
    }
    return crc;
}

static u32 payload_crc32(const void *payload, u32 size)
{
    return ~crc32_update(0xffffffffu, (const u8 *)payload, size);
}

static int build_slot_paths(bbk_save_store_t *store, const char *rom_path)
{
    u32 length = (u32)strlen(rom_path);
    u32 base_length = length;
    u32 index;
    if (length + 4u >= BBK_SAVE_PATH_CAPACITY) {
        return 0;
    }
    for (index = length; index != 0u; --index) {
        char character = rom_path[index - 1u];
        if (character == '.') {
            base_length = index - 1u;
            break;
        }
        if (character == '/' || character == '\\') {
            break;
        }
    }
    if (base_length + 4u >= BBK_SAVE_PATH_CAPACITY) {
        return 0;
    }
    memcpy(store->slot_path[0], rom_path, base_length);
    memcpy(store->slot_path[1], rom_path, base_length);
    memcpy(store->slot_path[0] + base_length, ".SV0", 5u);
    memcpy(store->slot_path[1] + base_length, ".SV1", 5u);
    return 1;
}

static int read_header(int file, save_header_t *header)
{
    return bda_fs_read_raw(file, header, sizeof(*header)) == (int)sizeof(*header);
}

static int validate_slot(
    const char *path, u32 expected_size, u32 *generation, u32 *payload_crc
)
{
    u8 chunk[512];
    save_header_t header;
    u32 remaining;
    u32 crc = 0xffffffffu;
    int file = bda_fs_fopen_raw(path, "rb");
    if (!bda_fs_file_is_valid(file)) {
        return 0;
    }
    if (!read_header(file, &header) ||
        header.magic != SAVE_MAGIC ||
        header.version != SAVE_VERSION ||
        header.payload_size != expected_size ||
        bda_fs_seek_raw(file, 0, BDA_SEEK_END) !=
            (int)(sizeof(header) + expected_size) ||
        bda_fs_seek_raw(file, sizeof(header), BDA_SEEK_SET) != (int)sizeof(header)) {
        (void)bda_fs_close_raw(file);
        return 0;
    }
    remaining = expected_size;
    while (remaining != 0u) {
        u32 amount = remaining < sizeof(chunk) ? remaining : (u32)sizeof(chunk);
        if (bda_fs_read_raw(file, chunk, amount) != (int)amount) {
            (void)bda_fs_close_raw(file);
            return 0;
        }
        crc = crc32_update(crc, chunk, amount);
        remaining -= amount;
    }
    (void)bda_fs_close_raw(file);
    crc = ~crc;
    if (crc != header.payload_crc) {
        return 0;
    }
    *generation = header.generation;
    *payload_crc = header.payload_crc;
    return 1;
}

static int load_slot(const char *path, void *payload, u32 payload_size)
{
    save_header_t header;
    int file = bda_fs_fopen_raw(path, "rb");
    int ok;
    if (!bda_fs_file_is_valid(file)) {
        return 0;
    }
    ok = read_header(file, &header) &&
        bda_fs_read_raw(file, payload, payload_size) == (int)payload_size;
    (void)bda_fs_close_raw(file);
    return ok;
}

static int generation_is_newer(u32 left, u32 right)
{
    return (s32)(left - right) > 0;
}

int bbk_save_store_open(
    bbk_save_store_t *store, const char *rom_path, void *payload, u32 payload_size
)
{
    u32 generation[2] = {0u, 0u};
    u32 crc[2] = {0u, 0u};
    int valid[2];
    int selected = -1;
    memset(store, 0, sizeof(*store));
    store->active_slot = -1;
    store->payload_size = payload_size;
    if (!payload || payload_size == 0u || !build_slot_paths(store, rom_path)) {
        return -1;
    }
    valid[0] = validate_slot(store->slot_path[0], payload_size, &generation[0], &crc[0]);
    valid[1] = validate_slot(store->slot_path[1], payload_size, &generation[1], &crc[1]);
    if (valid[0]) selected = 0;
    if (valid[1] && (selected < 0 || generation_is_newer(generation[1], generation[0]))) {
        selected = 1;
    }
    if (selected >= 0) {
        if (!load_slot(store->slot_path[selected], payload, payload_size)) {
            return -1;
        }
        store->active_slot = selected;
        store->generation = generation[selected];
        store->last_crc = crc[selected];
    } else {
        store->last_crc = payload_crc32(payload, payload_size);
    }
    store->initialized = 1;
    return selected >= 0 ? 1 : 0;
}

int bbk_save_store_checkpoint(
    bbk_save_store_t *store, const void *payload, int force
)
{
    save_header_t header;
    u32 verified_generation;
    u32 verified_crc;
    u32 crc;
    int target;
    int file;
    int ok;
    if (!store->initialized || !payload) {
        return -1;
    }
    crc = payload_crc32(payload, store->payload_size);
    if (!force && crc == store->last_crc) {
        return 0;
    }
    target = store->active_slot == 0 ? 1 : 0;
    header.magic = SAVE_MAGIC;
    header.version = SAVE_VERSION;
    header.generation = store->generation + 1u;
    header.payload_size = store->payload_size;
    header.payload_crc = crc;
    header.reserved = 0u;
    file = bda_fs_fopen_raw(store->slot_path[target], "wb");
    if (!bda_fs_file_is_valid(file)) {
        return -1;
    }
    ok = bda_fs_write_raw(file, &header, sizeof(header)) == (int)sizeof(header) &&
        bda_fs_write_raw(file, payload, store->payload_size) == (int)store->payload_size;
    (void)bda_fs_close_raw(file);
    if (!ok || !validate_slot(
            store->slot_path[target], store->payload_size,
            &verified_generation, &verified_crc
        ) || verified_generation != header.generation || verified_crc != crc) {
        return -1;
    }
    store->active_slot = target;
    store->generation = header.generation;
    store->last_crc = crc;
    return 1;
}
