#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdarg.h>

typedef struct FILE {
    int fd;
    int eof;
    int error;
    int mode;
} FILE;

extern FILE *stdin;
extern FILE *stdout;
extern FILE *stderr;

int fileno(FILE *stream);
FILE *fopen(const char *path, const char *mode);
int fclose(FILE *stream);
size_t fread(void *ptr, size_t size, size_t nmemb, FILE *stream);
size_t fwrite(const void *ptr, size_t size, size_t nmemb, FILE *stream);
int fgetc(FILE *stream);
int fputc(int c, FILE *stream);
char *fgets(char *s, int size, FILE *stream);
int fputs(const char *s, FILE *stream);
int puts(const char *s);
int vfprintf(FILE *stream, const char *fmt, va_list ap);
int fprintf(FILE *stream, const char *fmt, ...);
int printf(const char *fmt, ...);
