#ifndef BBK9588_FRONTEND_CONFIG_H
#define BBK9588_FRONTEND_CONFIG_H

#include "bda_types.h"

typedef struct bbk_frontend_config {
    u32 frameskip_interval;
    u32 audio_level;
    u32 key_swap;
    u32 generation;
    int active_slot;
    int dirty;
} bbk_frontend_config_t;

void bbk_frontend_config_defaults(bbk_frontend_config_t *config);
int bbk_frontend_config_load(bbk_frontend_config_t *config);
void bbk_frontend_config_set_frameskip(
    bbk_frontend_config_t *config, u32 frameskip_interval
);
void bbk_frontend_config_set_audio_level(
    bbk_frontend_config_t *config, u32 audio_level
);
void bbk_frontend_config_set_key_swap(
    bbk_frontend_config_t *config, int key_swap
);
int bbk_frontend_config_save(bbk_frontend_config_t *config);

#endif
