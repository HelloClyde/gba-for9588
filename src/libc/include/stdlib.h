#ifndef BBK9588_STDLIB_H
#define BBK9588_STDLIB_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

void *malloc(size_t size);
void *calloc(size_t count, size_t size);
void *realloc(void *pointer, size_t size);
void free(void *pointer);
long strtol(const char *text, char **end, int base);
int atoi(const char *text);
void abort(void);

#ifdef __cplusplus
}
#endif

#endif
