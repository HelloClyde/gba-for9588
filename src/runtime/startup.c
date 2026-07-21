#include "bda_types.h"

typedef void (*bda_init_fn_t)(void);

extern bda_init_fn_t __init_array_start[];
extern bda_init_fn_t __init_array_end[];

void bda_run_init_array(void)
{
    bda_init_fn_t *entry;

    for (entry = __init_array_start; entry < __init_array_end; ++entry) {
        (*entry)();
    }
}
