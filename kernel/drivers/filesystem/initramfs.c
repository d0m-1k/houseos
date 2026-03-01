#include <drivers/filesystem/initramfs.h>
#include <asm/mm.h>
#include <stdint.h>
#include <string.h>

static uint32_t hex_str_to_uint32(const char *hex) {
    uint32_t result = 0;
    for (int i = 0; i < 8; i++) {
        char c = hex[i];
        result <<= 4;
        if (c >= '0' && c <= '9') result |= (c - '0');
        else if (c >= 'a' && c <= 'f') result |= (c - 'a' + 10);
        else if (c >= 'A' && c <= 'F') result |= (c - 'A' + 10);
    }
    return result;
}

static void make_absolute_path(char *dest, const char *src, size_t max_len) {
    if (src[0] == '/') {
        strncpy(dest, src, max_len - 1);
    } else {
        dest[0] = '/';
        strncpy(dest + 1, src, max_len - 2);
    }
    dest[max_len - 1] = '\0';
}

static void clean_path(char *path) {
    if (strncmp(path, "./", 2) == 0) memmove(path, path + 2, strlen(path) - 1);
    
    size_t len = strlen(path);
    if (len > 1 && path[len - 1] == '/') path[len - 1] = '\0';
}

void initramfs_init(memfs *fs) {
    uint8_t *archive = (uint8_t *)INITRAMFS_ADDR;
    const uintptr_t candidates[] = { INITRAMFS_ADDR, 0x40000, 0x80000 };
    for (size_t i = 0; i < sizeof(candidates) / sizeof(candidates[0]); i++) {
        struct cpio_header *h = (struct cpio_header *)(uintptr_t)candidates[i];
        if (!strncmp(h->magic, "070701", 6) || !strncmp(h->magic, "070702", 6)) {
            archive = (uint8_t *)(uintptr_t)candidates[i];
            break;
        }
    }
    uint8_t *current = archive;
    
    while (1) {
        struct cpio_header *header = (struct cpio_header *)current;
        
        if (strncmp(header->magic, "070701", 6) != 0 &&
            strncmp(header->magic, "070702", 6) != 0) break;
        
        uint32_t filesize = hex_str_to_uint32(header->filesize);
        uint32_t mode = hex_str_to_uint32(header->mode);
        
        char *name = (char *)(current + 110);
        
        if (strcmp(name, "TRAILER!!!") == 0) break;
        
        uint32_t name_len = strlen(name) + 1;
        uint32_t total_size = 110 + name_len;
        uint32_t name_padding = (4 - (total_size % 4)) % 4;
        
        uint8_t *filedata = (uint8_t *)(name + name_len + name_padding);
        uint32_t data_padding = (4 - (filesize % 4)) % 4;
        
        char abs_path[256];
        make_absolute_path(abs_path, name, sizeof(abs_path));
        clean_path(abs_path);
        
        if (strcmp(abs_path, "/.") == 0 || strcmp(abs_path, "/..") == 0) {
            current = filedata + filesize + data_padding;
            continue;
        }
        
        if (strcmp(abs_path, "/") == 0) {
            current = filedata + filesize + data_padding;
            continue;
        }
        
        if (CPIO_S_ISDIR(mode)) {
            memfs_create_dir(fs, abs_path);
        } else if (CPIO_S_ISFIFO(mode)) {
            memfs_create_fifo(fs, abs_path);
        } else if (CPIO_S_ISSOCK(mode)) {
            memfs_create_socket(fs, abs_path);
        } else if (CPIO_S_ISREG(mode)) {
            memfs_create_file(fs, abs_path);
            if (filesize > 0) memfs_write(fs, abs_path, filedata, filesize);
        } else if (CPIO_S_ISLNK(mode)) {
            char link_target[256];
            if (filesize < sizeof(link_target) - 1) {
                strncpy(link_target, (char *)filedata, filesize);
                link_target[filesize] = '\0';
                memfs_create_file(fs, abs_path);
                memfs_write(fs, abs_path, link_target, strlen(link_target));
            }
        }
        
        current = filedata + filesize + data_padding;
    }
}
