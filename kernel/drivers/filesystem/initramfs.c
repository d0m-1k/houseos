#include <drivers/filesystem/initramfs.h>
#include <asm/mm.h>
#include <drivers/serial.h>
#include <stdint.h>
#include <string.h>

#define BOOTCFG_ADDR  0x00000600u
#define BOOTCFG_MAGIC 0x47464348u

typedef struct {
    uint32_t magic;
    uint32_t version;
    uint32_t kernel_size;
    uint32_t kernel_lba;
    uint32_t kernel_addr;
    uint32_t initramfs_size;
    uint32_t initramfs_lba;
    uint32_t initramfs_addr;
} bootcfg_early_t;

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

static int cpio_magic_ok(const uint8_t *addr) {
    const struct cpio_header *h = (const struct cpio_header*)addr;
    if (!addr) return 0;
    if (strncmp(h->magic, "070701", 6) == 0) return 1;
    if (strncmp(h->magic, "070702", 6) == 0) return 1;
    return 0;
}

static void initramfs_log_hex_u32(uint32_t v) {
    char buf[11];
    static const char h[] = "0123456789ABCDEF";
    buf[0] = '0';
    buf[1] = 'x';
    for (int i = 0; i < 8; i++) {
        uint32_t shift = (uint32_t)(28 - i * 4);
        buf[2 + i] = h[(v >> shift) & 0xFu];
    }
    buf[10] = '\0';
    serial_write(SERIAL_COM1, buf);
}

static void initramfs_log_dec_u32(uint32_t v) {
    char buf[11];
    uint32_t n = 0;
    if (v == 0) {
        serial_write(SERIAL_COM1, "0");
        return;
    }
    while (v > 0 && n < sizeof(buf)) {
        buf[n++] = (char)('0' + (v % 10u));
        v /= 10u;
    }
    while (n > 0) serial_write_char(SERIAL_COM1, buf[--n]);
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
    size_t r = 0;
    size_t w = 0;
    int last_was_slash = 0;
    if (!path || path[0] == '\0') return;

    while (path[r] != '\0') {
        if (path[r] == '/') {
            if (!last_was_slash) {
                path[w++] = '/';
                last_was_slash = 1;
            }
            r++;
            continue;
        }

        if (path[r] == '.' && (r == 0 || path[r - 1] == '/') &&
            (path[r + 1] == '/' || path[r + 1] == '\0')) {
            r++;
            continue;
        }

        path[w++] = path[r++];
        last_was_slash = 0;
    }
    path[w] = '\0';

    if (w == 0) {
        path[0] = '/';
        path[1] = '\0';
        return;
    }

    if (w > 1 && path[w - 1] == '/') path[w - 1] = '\0';

    if (path[0] != '/') {
        memmove(path + 1, path, strlen(path) + 1);
        path[0] = '/';
    }
}

uint8_t *initramfs_get_archive_addr(void) {
    bootcfg_early_t cfg;
    const uintptr_t candidates[] = {
        (uintptr_t)INITRAMFS_ADDR,
        0x00082000u,
        0x00050000u,
        0x00060000u,
        0x00070000u,
        0x00040000u,
        0x00080000u
    };

    memset(&cfg, 0, sizeof(cfg));
    memcpy(&cfg, (const void*)(uintptr_t)BOOTCFG_ADDR, sizeof(cfg));

    if (cfg.magic == BOOTCFG_MAGIC && cfg.initramfs_addr != 0u) {
        uint8_t *from_cfg = (uint8_t*)(uintptr_t)cfg.initramfs_addr;
        if (cpio_magic_ok(from_cfg)) return from_cfg;
    }

    for (size_t i = 0; i < sizeof(candidates) / sizeof(candidates[0]); i++) {
        uint8_t *probe = (uint8_t*)candidates[i];
        if (cpio_magic_ok(probe)) return probe;
    }

    return (uint8_t*)(uintptr_t)INITRAMFS_ADDR;
}

void initramfs_init(memfs *fs) {
    uint8_t *archive = initramfs_get_archive_addr();
    uint8_t *current = archive;
    uint32_t imported = 0;
    serial_write(SERIAL_COM1, "initramfs: addr=");
    initramfs_log_hex_u32((uint32_t)(uintptr_t)archive);
    serial_write(SERIAL_COM1, " magic=");
    serial_write_char(SERIAL_COM1, (char)archive[0]);
    serial_write_char(SERIAL_COM1, (char)archive[1]);
    serial_write_char(SERIAL_COM1, (char)archive[2]);
    serial_write_char(SERIAL_COM1, (char)archive[3]);
    serial_write_char(SERIAL_COM1, (char)archive[4]);
    serial_write_char(SERIAL_COM1, (char)archive[5]);
    serial_write(SERIAL_COM1, "\n");

    while (1) {
        struct cpio_header *header = (struct cpio_header *)current;
        uint32_t namesize;
        char name_buf[256];
        uint32_t name_copy;
        
        if (strncmp(header->magic, "070701", 6) != 0 &&
            strncmp(header->magic, "070702", 6) != 0) break;
        
        uint32_t filesize = hex_str_to_uint32(header->filesize);
        uint32_t mode = hex_str_to_uint32(header->mode);
        namesize = hex_str_to_uint32(header->namesize);

        if (namesize == 0 || namesize > 4096) break;

        char *name = (char *)(current + 110);
        name_copy = namesize;
        if (name_copy > sizeof(name_buf) - 1) name_copy = sizeof(name_buf) - 1;
        memcpy(name_buf, name, name_copy);
        name_buf[name_copy] = '\0';
        if (name_copy > 0 && name_buf[name_copy - 1] == '\0') {
            /* already null-terminated by archive */
        } else {
            name_buf[sizeof(name_buf) - 1] = '\0';
        }
        
        if (strcmp(name_buf, "TRAILER!!!") == 0) break;
        
        uint32_t total_size = 110 + namesize;
        uint32_t name_padding = (4 - (total_size % 4)) % 4;
        
        uint8_t *filedata = (uint8_t *)(name + namesize + name_padding);
        uint32_t data_padding = (4 - (filesize % 4)) % 4;
        
        char abs_path[256];
        make_absolute_path(abs_path, name_buf, sizeof(abs_path));
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
            imported++;
        } else if (CPIO_S_ISFIFO(mode)) {
            memfs_create_fifo(fs, abs_path);
            imported++;
        } else if (CPIO_S_ISSOCK(mode)) {
            memfs_create_socket(fs, abs_path);
            imported++;
        } else if (CPIO_S_ISREG(mode)) {
            memfs_create_file(fs, abs_path);
            if (filesize > 0) memfs_write(fs, abs_path, filedata, filesize);
            imported++;
        } else if (CPIO_S_ISLNK(mode)) {
            char link_target[256];
            if (filesize < sizeof(link_target) - 1) {
                strncpy(link_target, (char *)filedata, filesize);
                link_target[filesize] = '\0';
                memfs_create_file(fs, abs_path);
                memfs_write(fs, abs_path, link_target, strlen(link_target));
                imported++;
            }
        }
        
        current = filedata + filesize + data_padding;
    }
    serial_write(SERIAL_COM1, "initramfs: imported=");
    initramfs_log_dec_u32(imported);
    serial_write(SERIAL_COM1, "\n");
}
