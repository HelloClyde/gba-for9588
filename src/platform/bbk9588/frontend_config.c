#include "platform/bbk9588/frontend_config.h"

#include "bda_filesystem.h"

#include <string.h>

#define CONFIG_MAGIC 0x46434247u
#define CONFIG_VERSION_LEGACY 1u
#define CONFIG_VERSION 2u
#define CONFIG_AUDIO_LEVEL_MAX 4u

typedef struct frontend_config_disk {
    u32 magic;
    u32 version;
    u32 generation;
    u32 frameskip_interval;
    u32 audio_level;
    u32 key_swap;
    u32 crc;
} frontend_config_disk_t;

_Static_assert(sizeof(frontend_config_disk_t) == 28u, "config disk ABI");

static const char *const config_paths[2] = {
    "A:\\GAMEBOY\\GBA.CF0",
    "A:\\GAMEBOY\\GBA.CF1"
};

static u32 crc32_update(u32 crc, const u8 *data, u32 size)
{
    u32 index;
    for (index = 0u; index < size; ++index) {
        u32 bit;
        crc ^= data[index];
        for (bit = 0u; bit < 8u; ++bit) {
            crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
        }
    }
    return crc;
}

static u32 disk_crc(const frontend_config_disk_t *disk)
{
    return ~crc32_update(
        0xffffffffu, (const u8 *)disk,
        (u32)sizeof(*disk) - (u32)sizeof(disk->crc)
    );
}

static int generation_is_newer(u32 left, u32 right)
{
    return (s32)(left - right) > 0;
}

static int read_slot(int slot, frontend_config_disk_t *disk)
{
    int file = bda_fs_fopen_raw(config_paths[slot], "rb");
    int valid;
    if (!bda_fs_file_is_valid(file)) {
        return 0;
    }
    valid = bda_fs_read_raw(file, disk, sizeof(*disk)) == (int)sizeof(*disk) &&
        bda_fs_seek_raw(file, 0, BDA_SEEK_END) == (int)sizeof(*disk) &&
        disk->magic == CONFIG_MAGIC &&
        (disk->version == CONFIG_VERSION_LEGACY ||
            disk->version == CONFIG_VERSION) &&
        disk->frameskip_interval <= 2u &&
        disk->audio_level <=
            (disk->version == CONFIG_VERSION_LEGACY ? 1u :
                CONFIG_AUDIO_LEVEL_MAX) &&
        disk->key_swap <= 1u &&
        disk->crc == disk_crc(disk);
    (void)bda_fs_close_raw(file);
    return valid;
}

void bbk_frontend_config_defaults(bbk_frontend_config_t *config)
{
    memset(config, 0, sizeof(*config));
    config->frameskip_interval = 1u;
    config->audio_level = CONFIG_AUDIO_LEVEL_MAX;
    config->active_slot = -1;
}

int bbk_frontend_config_load(bbk_frontend_config_t *config)
{
    frontend_config_disk_t disk[2];
    int valid[2];
    int selected = -1;

    bbk_frontend_config_defaults(config);
    valid[0] = read_slot(0, &disk[0]);
    valid[1] = read_slot(1, &disk[1]);
    if (valid[0]) {
        selected = 0;
    }
    if (valid[1] && (selected < 0 ||
        generation_is_newer(disk[1].generation, disk[0].generation))) {
        selected = 1;
    }
    if (selected < 0) {
        return 0;
    }
    config->frameskip_interval = disk[selected].frameskip_interval;
    config->audio_level = disk[selected].version == CONFIG_VERSION_LEGACY ?
        (disk[selected].audio_level != 0u ? CONFIG_AUDIO_LEVEL_MAX : 0u) :
        disk[selected].audio_level;
    config->key_swap = disk[selected].key_swap;
    config->generation = disk[selected].generation;
    config->active_slot = selected;
    return 1;
}

void bbk_frontend_config_set_frameskip(
    bbk_frontend_config_t *config, u32 frameskip_interval
)
{
    if (frameskip_interval <= 2u &&
        config->frameskip_interval != frameskip_interval) {
        config->frameskip_interval = frameskip_interval;
        config->dirty = 1;
    }
}

void bbk_frontend_config_set_audio_level(
    bbk_frontend_config_t *config, u32 audio_level
)
{
    if (audio_level <= CONFIG_AUDIO_LEVEL_MAX &&
        config->audio_level != audio_level) {
        config->audio_level = audio_level;
        config->dirty = 1;
    }
}

void bbk_frontend_config_set_key_swap(
    bbk_frontend_config_t *config, int key_swap
)
{
    u32 normalized = key_swap ? 1u : 0u;
    if (config->key_swap != normalized) {
        config->key_swap = normalized;
        config->dirty = 1;
    }
}

int bbk_frontend_config_save(bbk_frontend_config_t *config)
{
    frontend_config_disk_t disk;
    frontend_config_disk_t verified;
    int target;
    int file;
    int written;

    if (!config->dirty) {
        return 0;
    }
    target = config->active_slot == 0 ? 1 : 0;
    memset(&disk, 0, sizeof(disk));
    disk.magic = CONFIG_MAGIC;
    disk.version = CONFIG_VERSION;
    disk.generation = config->generation + 1u;
    disk.frameskip_interval = config->frameskip_interval;
    disk.audio_level = config->audio_level;
    disk.key_swap = config->key_swap;
    disk.crc = disk_crc(&disk);

    file = bda_fs_fopen_raw(config_paths[target], "wb");
    if (!bda_fs_file_is_valid(file)) {
        return -1;
    }
    written = bda_fs_write_raw(file, &disk, sizeof(disk));
    (void)bda_fs_close_raw(file);
    if (written != (int)sizeof(disk) || !read_slot(target, &verified) ||
        verified.generation != disk.generation || verified.crc != disk.crc) {
        return -1;
    }
    config->generation = disk.generation;
    config->active_slot = target;
    config->dirty = 0;
    return 1;
}
