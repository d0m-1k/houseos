#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syscall.h>

#define FILE_MODE_READ  1
#define FILE_MODE_WRITE 2
#define FILE_MODE_APPEND 4

static FILE g_stdin = { .fd = 0, .eof = 0, .error = 0, .mode = FILE_MODE_READ };
static FILE g_stdout = { .fd = 1, .eof = 0, .error = 0, .mode = FILE_MODE_WRITE };
static FILE g_stderr = { .fd = 2, .eof = 0, .error = 0, .mode = FILE_MODE_WRITE };

FILE *stdin = &g_stdin;
FILE *stdout = &g_stdout;
FILE *stderr = &g_stderr;

static int write_all(int fd, const char *buf, uint32_t len) {
    int total = 0;
    while (len > 0) {
        int32_t n = write(fd, buf, len);
        if (n <= 0) return (total > 0) ? total : -1;
        buf += n;
        len -= (uint32_t)n;
        total += n;
    }
    return total;
}

static int put_c(FILE *stream, char c) {
    if (!stream) return -1;
    return write_all(stream->fd, &c, 1);
}

static int put_s(FILE *stream, const char *s) {
    if (!stream) return -1;
    if (!s) s = "(null)";
    return write_all(stream->fd, s, (uint32_t)strlen(s));
}

int fileno(FILE *stream) {
    if (!stream) return -1;
    return stream->fd;
}

FILE *fopen(const char *path, const char *mode) {
    static FILE fpool[16];
    static uint8_t used[16];
    uint32_t flags = 0;
    int fd = -1;
    int m = 0;

    if (!path || !mode || mode[0] == '\0') return NULL;

    if (mode[0] == 'r') {
        flags = 0;
        m = FILE_MODE_READ;
    } else if (mode[0] == 'w') {
        flags = 1;
        m = FILE_MODE_WRITE;
    } else if (mode[0] == 'a') {
        flags = 1;
        m = FILE_MODE_WRITE | FILE_MODE_APPEND;
    } else {
        return NULL;
    }

    fd = open(path, flags);
    if (fd < 0) return NULL;

    for (int i = 0; i < 16; i++) {
        if (!used[i]) {
            used[i] = 1;
            fpool[i].fd = fd;
            fpool[i].eof = 0;
            fpool[i].error = 0;
            fpool[i].mode = m;
            return &fpool[i];
        }
    }

    close(fd);
    return NULL;
}

int fclose(FILE *stream) {
    if (!stream) return -1;
    if (stream == stdin || stream == stdout || stream == stderr) return 0;
    return close(stream->fd);
}

size_t fread(void *ptr, size_t size, size_t nmemb, FILE *stream) {
    size_t total;
    int32_t n;
    if (!ptr || !stream || size == 0 || nmemb == 0) return 0;
    if (!(stream->mode & FILE_MODE_READ)) return 0;

    total = size * nmemb;
    n = read(stream->fd, ptr, (uint32_t)total);
    if (n <= 0) {
        if (n == 0) stream->eof = 1;
        else stream->error = 1;
        return 0;
    }
    return (size_t)n / size;
}

size_t fwrite(const void *ptr, size_t size, size_t nmemb, FILE *stream) {
    size_t total;
    int32_t n;
    if (!ptr || !stream || size == 0 || nmemb == 0) return 0;
    if (!(stream->mode & FILE_MODE_WRITE)) return 0;

    total = size * nmemb;
    if (stream->mode & FILE_MODE_APPEND) {
        n = append(stream->fd, ptr, (uint32_t)total);
    } else {
        n = write(stream->fd, ptr, (uint32_t)total);
    }
    if (n <= 0) {
        stream->error = 1;
        return 0;
    }
    return (size_t)n / size;
}

int fgetc(FILE *stream) {
    char c = 0;
    if (fread(&c, 1, 1, stream) != 1) return -1;
    return (unsigned char)c;
}

int fputc(int c, FILE *stream) {
    char ch = (char)c;
    if (fwrite(&ch, 1, 1, stream) != 1) return -1;
    return (unsigned char)ch;
}

char *fgets(char *s, int size, FILE *stream) {
    int i = 0;
    if (!s || !stream || size <= 1) return NULL;

    while (i < size - 1) {
        int c = fgetc(stream);
        if (c < 0) break;
        s[i++] = (char)c;
        if (c == '\n') break;
    }
    if (i == 0) return NULL;
    s[i] = '\0';
    return s;
}

int fputs(const char *s, FILE *stream) {
    int n = put_s(stream, s);
    return (n < 0) ? -1 : n;
}

int puts(const char *s) {
    int n = fputs(s, stdout);
    if (n < 0) return -1;
    if (fputc('\n', stdout) < 0) return -1;
    return n + 1;
}

int vfprintf(FILE *stream, const char *fmt, va_list ap) {
    int written = 0;
    char num[32];

    if (!stream || !fmt) return -1;

    for (const char *p = fmt; *p; p++) {
        if (*p != '%') {
            if (put_c(stream, *p) < 0) return -1;
            written++;
            continue;
        }

        p++;
        if (*p == '\0') break;
        if (*p == '%') {
            if (put_c(stream, '%') < 0) return -1;
            written++;
            continue;
        }
        if (*p == 'c') {
            char c = (char)va_arg(ap, int);
            if (put_c(stream, c) < 0) return -1;
            written++;
            continue;
        }
        if (*p == 's') {
            const char *s = va_arg(ap, const char*);
            int n = put_s(stream, s);
            if (n < 0) return -1;
            written += n;
            continue;
        }
        if (*p == 'd' || *p == 'i') {
            int v = va_arg(ap, int);
            if (v < 0) {
                if (put_c(stream, '-') < 0) return -1;
                written++;
                utoa((unsigned int)(-v), num, 10);
            } else {
                utoa((unsigned int)v, num, 10);
            }
            int n = put_s(stream, num);
            if (n < 0) return -1;
            written += n;
            continue;
        }
        if (*p == 'u') {
            unsigned int v = va_arg(ap, unsigned int);
            utoa(v, num, 10);
            int n = put_s(stream, num);
            if (n < 0) return -1;
            written += n;
            continue;
        }
        if (*p == 'x') {
            unsigned int v = va_arg(ap, unsigned int);
            utoa(v, num, 16);
            int n = put_s(stream, num);
            if (n < 0) return -1;
            written += n;
            continue;
        }

        if (put_c(stream, '%') < 0 || put_c(stream, *p) < 0) return -1;
        written += 2;
    }

    return written;
}

int fprintf(FILE *stream, const char *fmt, ...) {
    va_list ap;
    int n;
    va_start(ap, fmt);
    n = vfprintf(stream, fmt, ap);
    va_end(ap);
    return n;
}

int printf(const char *fmt, ...) {
    va_list ap;
    int n;
    va_start(ap, fmt);
    n = vfprintf(stdout, fmt, ap);
    va_end(ap);
    return n;
}
