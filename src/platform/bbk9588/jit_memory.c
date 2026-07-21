#include <stdint.h>
#include <stdlib.h>

void *map_jit_block(unsigned size)
{
    void *block = malloc(size);
    uintptr_t start;
    uintptr_t end;
    uintptr_t code;

    if (!block) {
        return 0;
    }
    start = (uintptr_t)block;
    end = start + size - 1u;
    code = (uintptr_t)&map_jit_block;
    if ((start >> 28) != (code >> 28) || (end >> 28) != (code >> 28)) {
        free(block);
        return 0;
    }
    return block;
}

void unmap_jit_block(void *block, unsigned size)
{
    (void)size;
    free(block);
}
