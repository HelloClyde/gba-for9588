#ifndef BBK9588_STDIO_H
#define BBK9588_STDIO_H

#include <stdarg.h>
#include <stddef.h>

#define EOF (-1)
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

typedef struct bbk9588_file FILE;
extern FILE *stdout;

#ifdef __cplusplus
extern "C" {
#endif

int printf(const char *format, ...);
int snprintf(char *output, size_t size, const char *format, ...);
int vsnprintf(char *output, size_t size, const char *format, va_list args);
int sscanf(const char *input, const char *format, ...);
int fflush(FILE *stream);

#ifdef __cplusplus
}
#endif

#endif
