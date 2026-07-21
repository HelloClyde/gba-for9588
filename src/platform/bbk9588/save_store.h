#ifndef BBK9588_SAVE_STORE_H
#define BBK9588_SAVE_STORE_H

#include "bda_types.h"

#define BBK_SAVE_PATH_CAPACITY 520u

typedef struct bbk_save_store {
    char slot_path[2][BBK_SAVE_PATH_CAPACITY];
    u32 generation;
    u32 last_crc;
    u32 payload_size;
    int active_slot;
    int initialized;
} bbk_save_store_t;

int bbk_save_store_open(
    bbk_save_store_t *store, const char *rom_path, void *payload, u32 payload_size
);
int bbk_save_store_checkpoint(
    bbk_save_store_t *store, const void *payload, int force
);

#endif
