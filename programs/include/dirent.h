#pragma once

#include <stdint.h>

#define DT_UNKNOWN 0
#define DT_REG 8
#define DT_DIR 4

struct dirent {
    uint32_t d_ino;
    uint8_t d_type;
    char d_name[256];
};

typedef struct DIR DIR;

DIR *opendir(const char *path);
struct dirent *readdir(DIR *dirp);
int closedir(DIR *dirp);
void rewinddir(DIR *dirp);
