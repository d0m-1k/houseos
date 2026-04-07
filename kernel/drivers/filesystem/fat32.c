#include <drivers/filesystem/fat32.h>
#include <drivers/disk.h>
#include <asm/mm.h>
#include <string.h>

#define FAT32_ATTR_READ_ONLY 0x01
#define FAT32_ATTR_HIDDEN    0x02
#define FAT32_ATTR_SYSTEM    0x04
#define FAT32_ATTR_VOLUME_ID 0x08
#define FAT32_ATTR_DIR       0x10
#define FAT32_ATTR_ARCHIVE   0x20
#define FAT32_ATTR_LFN       0x0F

#define FAT32_EOC 0x0FFFFFFFu

enum {
    FAT32_DISK_ATA = 0
};

typedef struct {
    uint8_t name[11];
    uint8_t attr;
    uint8_t ntres;
    uint8_t crt_time_tenth;
    uint16_t crt_time;
    uint16_t crt_date;
    uint16_t last_access_date;
    uint16_t first_cluster_hi;
    uint16_t wrt_time;
    uint16_t wrt_date;
    uint16_t first_cluster_lo;
    uint32_t file_size;
} __attribute__((packed)) fat32_dirent_t;

typedef struct {
    uint8_t ord;
    uint16_t name1[5];
    uint8_t attr;
    uint8_t type;
    uint8_t checksum;
    uint16_t name2[6];
    uint16_t first_cluster_lo;
    uint16_t name3[2];
} __attribute__((packed)) fat32_lfn_t;

typedef struct {
    uint8_t attr;
    uint32_t first_cluster;
    uint32_t size;
    char name[256];
} fat32_node_t;

typedef struct {
    fat32_node_t node;
    uint32_t owner_dir_cluster;
    uint32_t entry_rel_sector;
    uint32_t entry_offset;
} fat32_found_t;

static int fat32_read_sectors(const fat32_fs_t *fs, uint32_t rel_lba, uint32_t count, void *buf) {
    uint32_t end;
    if (!fs || !buf || count == 0) return -1;
    if (rel_lba >= fs->part_total_sectors) return -1;
    end = rel_lba + count;
    if (end < rel_lba || end > fs->part_total_sectors) return -1;
    if (fs->disk_kind == FAT32_DISK_ATA) {
        return disk_read_kernel(fs->part_start_lba + rel_lba, count, buf);
    }
    return -1;
}

static int fat32_write_sectors(const fat32_fs_t *fs, uint32_t rel_lba, uint32_t count, const void *buf) {
    uint32_t end;
    if (!fs || !buf || count == 0) return -1;
    if (rel_lba >= fs->part_total_sectors) return -1;
    end = rel_lba + count;
    if (end < rel_lba || end > fs->part_total_sectors) return -1;
    if (fs->disk_kind == FAT32_DISK_ATA) {
        return disk_write_kernel(fs->part_start_lba + rel_lba, count, buf);
    }
    return -1;
}

static uint32_t fat32_cluster_to_rel_lba(const fat32_fs_t *fs, uint32_t cluster) {
    return fs->data_start_lba + (cluster - 2u) * (uint32_t)fs->sectors_per_cluster;
}

static uint32_t fat32_cluster_size(const fat32_fs_t *fs) {
    return (uint32_t)fs->sectors_per_cluster * (uint32_t)fs->bytes_per_sector;
}

static uint32_t fat32_max_cluster(const fat32_fs_t *fs) {
    uint32_t data_sectors;
    uint32_t data_clusters;
    if (!fs || fs->part_total_sectors <= fs->data_start_lba || fs->sectors_per_cluster == 0) return 1;
    data_sectors = fs->part_total_sectors - fs->data_start_lba;
    data_clusters = data_sectors / fs->sectors_per_cluster;
    return data_clusters + 1;
}

static int fat32_is_eoc(uint32_t cluster) {
    return (cluster >= 0x0FFFFFF8u);
}

static int ascii_tolower(int c) {
    return (c >= 'A' && c <= 'Z') ? (c - 'A' + 'a') : c;
}

static int ascii_toupper(int c) {
    return (c >= 'a' && c <= 'z') ? (c - 'a' + 'A') : c;
}

static int name_eq_ci(const char *a, const char *b) {
    uint32_t i = 0;
    if (!a || !b) return 0;
    while (a[i] && b[i]) {
        if (ascii_tolower(a[i]) != ascii_tolower(b[i])) return 0;
        i++;
    }
    return (a[i] == '\0' && b[i] == '\0') ? 1 : 0;
}

static void decode_short_name(const fat32_dirent_t *e, char *out, uint32_t cap) {
    uint32_t p = 0;
    uint32_t i;
    for (i = 0; i < 8 && e->name[i] != ' '; i++) {
        if (p + 1 >= cap) break;
        out[p++] = (char)ascii_tolower((char)e->name[i]);
    }
    if (e->name[8] != ' ') {
        if (p + 1 < cap) out[p++] = '.';
        for (i = 8; i < 11 && e->name[i] != ' '; i++) {
            if (p + 1 >= cap) break;
            out[p++] = (char)ascii_tolower((char)e->name[i]);
        }
    }
    out[p] = '\0';
}

static void lfn_extract_piece(const fat32_lfn_t *lfn, char *piece, uint32_t cap) {
    uint32_t p = 0;
    uint16_t u;
    uint32_t i;

    for (i = 0; i < 5; i++) {
        u = lfn->name1[i];
        if (u == 0x0000 || u == 0xFFFF) break;
        if (p + 1 < cap) piece[p++] = (char)(u & 0xFF);
    }
    for (i = 0; i < 6; i++) {
        u = lfn->name2[i];
        if (u == 0x0000 || u == 0xFFFF) break;
        if (p + 1 < cap) piece[p++] = (char)(u & 0xFF);
    }
    for (i = 0; i < 2; i++) {
        u = lfn->name3[i];
        if (u == 0x0000 || u == 0xFFFF) break;
        if (p + 1 < cap) piece[p++] = (char)(u & 0xFF);
    }
    piece[p] = '\0';
}

static int fat32_fat_get(const fat32_fs_t *fs, uint32_t cluster, uint32_t *out_val) {
    uint8_t sec[512];
    uint32_t fat_off;
    uint32_t rel_sec;
    uint32_t off;
    uint32_t v;
    if (!fs || !out_val || cluster < 2) return -1;
    fat_off = cluster * 4u;
    rel_sec = fs->fat_start_lba + (fat_off / 512u);
    off = fat_off % 512u;
    if (fat32_read_sectors(fs, rel_sec, 1, sec) != 0) return -1;
    v = (uint32_t)sec[off] |
        ((uint32_t)sec[off + 1] << 8) |
        ((uint32_t)sec[off + 2] << 16) |
        ((uint32_t)sec[off + 3] << 24);
    *out_val = v & 0x0FFFFFFFu;
    return 0;
}

static int fat32_fat_set(const fat32_fs_t *fs, uint32_t cluster, uint32_t value) {
    uint8_t sec[512];
    uint32_t fat_off;
    uint32_t rel_sec_in_fat;
    uint32_t off;
    uint32_t newv;
    uint32_t fat_i;
    if (!fs || cluster < 2) return -1;

    fat_off = cluster * 4u;
    rel_sec_in_fat = fat_off / 512u;
    off = fat_off % 512u;
    newv = value & 0x0FFFFFFFu;

    for (fat_i = 0; fat_i < fs->num_fats; fat_i++) {
        uint32_t rel_sec = fs->fat_start_lba + fat_i * fs->fat_size_sectors + rel_sec_in_fat;
        uint32_t cur;
        if (fat32_read_sectors(fs, rel_sec, 1, sec) != 0) return -1;
        cur = (uint32_t)sec[off] |
              ((uint32_t)sec[off + 1] << 8) |
              ((uint32_t)sec[off + 2] << 16) |
              ((uint32_t)sec[off + 3] << 24);
        cur = (cur & 0xF0000000u) | newv;
        sec[off] = (uint8_t)(cur & 0xFF);
        sec[off + 1] = (uint8_t)((cur >> 8) & 0xFF);
        sec[off + 2] = (uint8_t)((cur >> 16) & 0xFF);
        sec[off + 3] = (uint8_t)((cur >> 24) & 0xFF);
        if (fat32_write_sectors(fs, rel_sec, 1, sec) != 0) return -1;
    }
    return 0;
}

static int fat32_zero_cluster(const fat32_fs_t *fs, uint32_t cluster) {
    uint8_t z[512];
    uint32_t rel;
    uint32_t s;
    if (!fs || cluster < 2) return -1;
    memset(z, 0, sizeof(z));
    rel = fat32_cluster_to_rel_lba(fs, cluster);
    for (s = 0; s < fs->sectors_per_cluster; s++) {
        if (fat32_write_sectors(fs, rel + s, 1, z) != 0) return -1;
    }
    return 0;
}

static int fat32_alloc_cluster(const fat32_fs_t *fs, uint32_t *out_cluster) {
    uint32_t c;
    uint32_t max_c;
    uint32_t v;
    if (!fs || !out_cluster) return -1;
    max_c = fat32_max_cluster(fs);
    for (c = 2; c <= max_c; c++) {
        if (fat32_fat_get(fs, c, &v) != 0) return -1;
        if (v == 0) {
            if (fat32_fat_set(fs, c, FAT32_EOC) != 0) return -1;
            if (fat32_zero_cluster(fs, c) != 0) return -1;
            *out_cluster = c;
            return 0;
        }
    }
    return -1;
}

static int fat32_free_chain(const fat32_fs_t *fs, uint32_t first_cluster) {
    uint32_t c;
    uint32_t guard;
    uint32_t max_c;
    if (!fs || first_cluster < 2) return 0;
    c = first_cluster;
    guard = 0;
    max_c = fat32_max_cluster(fs) + 8;

    while (c >= 2 && guard++ < max_c) {
        uint32_t next;
        if (fat32_fat_get(fs, c, &next) != 0) return -1;
        if (fat32_fat_set(fs, c, 0) != 0) return -1;
        if (fat32_is_eoc(next) || next < 2) break;
        c = next;
    }
    return 0;
}

static int fat32_chain_last(const fat32_fs_t *fs, uint32_t first_cluster, uint32_t *out_last, uint32_t *out_len) {
    uint32_t c;
    uint32_t len = 0;
    uint32_t guard = 0;
    uint32_t max_c;
    if (!fs || !out_last || !out_len || first_cluster < 2) return -1;
    c = first_cluster;
    max_c = fat32_max_cluster(fs) + 8;
    while (1) {
        uint32_t next;
        len++;
        if (fat32_fat_get(fs, c, &next) != 0) return -1;
        if (fat32_is_eoc(next) || next < 2) break;
        c = next;
        if (++guard > max_c) return -1;
    }
    *out_last = c;
    *out_len = len;
    return 0;
}

static int fat32_dir_find(const fat32_fs_t *fs, uint32_t dir_cluster, const char *name, fat32_found_t *out) {
    uint8_t sec[512];
    uint32_t c = dir_cluster;
    char lfn_parts[20][14];
    uint8_t lfn_seen[20];
    memset(lfn_seen, 0, sizeof(lfn_seen));

    while (c >= 2 && !fat32_is_eoc(c)) {
        uint32_t rel = fat32_cluster_to_rel_lba(fs, c);
        uint32_t s;
        for (s = 0; s < fs->sectors_per_cluster; s++) {
            uint32_t off;
            if (fat32_read_sectors(fs, rel + s, 1, sec) != 0) return -1;
            for (off = 0; off < 512; off += 32) {
                fat32_dirent_t *e = (fat32_dirent_t*)&sec[off];
                if (e->name[0] == 0x00) return -1;
                if (e->name[0] == 0xE5) {
                    memset(lfn_seen, 0, sizeof(lfn_seen));
                    continue;
                }

                if (e->attr == FAT32_ATTR_LFN) {
                    fat32_lfn_t *lfn = (fat32_lfn_t*)e;
                    uint8_t idx = (uint8_t)(lfn->ord & 0x1F);
                    if (idx > 0 && idx <= 20) {
                        lfn_extract_piece(lfn, lfn_parts[idx - 1], sizeof(lfn_parts[0]));
                        lfn_seen[idx - 1] = 1;
                    }
                    continue;
                }

                if (e->attr & FAT32_ATTR_VOLUME_ID) {
                    memset(lfn_seen, 0, sizeof(lfn_seen));
                    continue;
                }

                {
                    char decoded[256];
                    decoded[0] = '\0';
                    if (lfn_seen[0]) {
                        uint32_t p = 0;
                        uint32_t i;
                        for (i = 0; i < 20; i++) {
                            uint32_t j;
                            if (!lfn_seen[i]) break;
                            for (j = 0; lfn_parts[i][j] != '\0'; j++) {
                                if (p + 1 >= sizeof(decoded)) break;
                                decoded[p++] = lfn_parts[i][j];
                            }
                        }
                        decoded[p] = '\0';
                    } else {
                        decode_short_name(e, decoded, sizeof(decoded));
                    }

                    if (name_eq_ci(decoded, name)) {
                        if (out) {
                            out->node.attr = e->attr;
                            out->node.first_cluster = ((uint32_t)e->first_cluster_hi << 16) | (uint32_t)e->first_cluster_lo;
                            out->node.size = e->file_size;
                            strncpy(out->node.name, decoded, sizeof(out->node.name) - 1);
                            out->node.name[sizeof(out->node.name) - 1] = '\0';
                            out->owner_dir_cluster = dir_cluster;
                            out->entry_rel_sector = rel + s;
                            out->entry_offset = off;
                        }
                        return 0;
                    }
                }
                memset(lfn_seen, 0, sizeof(lfn_seen));
            }
        }
        {
            uint32_t next;
            if (fat32_fat_get(fs, c, &next) != 0) return -1;
            c = next;
        }
    }
    return -1;
}

static int fat32_resolve(const fat32_fs_t *fs, const char *path, fat32_found_t *out) {
    char seg[256];
    uint32_t i = 0;
    uint32_t p = 0;
    uint32_t cur = fs->root_cluster;
    fat32_found_t found;

    if (!fs || !path || path[0] != '/') return -1;
    if (strcmp(path, "/") == 0) {
        if (out) {
            memset(out, 0, sizeof(*out));
            out->node.attr = FAT32_ATTR_DIR;
            out->node.first_cluster = fs->root_cluster;
            out->node.size = 0;
            strcpy(out->node.name, "/");
            out->owner_dir_cluster = 0;
            out->entry_rel_sector = 0;
            out->entry_offset = 0;
        }
        return 0;
    }

    while (1) {
        if (path[i] == '/' || path[i] == '\0') {
            if (p > 0) {
                seg[p] = '\0';
                if (fat32_dir_find(fs, cur, seg, &found) != 0) return -1;
                cur = found.node.first_cluster ? found.node.first_cluster : fs->root_cluster;
                p = 0;
            }
            if (path[i] == '\0') break;
            i++;
            continue;
        }
        if (p + 1 >= sizeof(seg)) return -1;
        seg[p++] = path[i++];
    }

    if (out) *out = found;
    return 0;
}

static int fat32_resolve_parent(const fat32_fs_t *fs, const char *path, uint32_t *parent_cluster, char *name_out, uint32_t name_cap) {
    uint32_t len;
    int32_t i;
    char parent_path[256];
    fat32_found_t parent;

    if (!fs || !path || !parent_cluster || !name_out || name_cap < 2) return -1;
    if (path[0] != '/' || strcmp(path, "/") == 0) return -1;

    len = (uint32_t)strlen(path);
    while (len > 1 && path[len - 1] == '/') len--;
    if (len <= 1) return -1;

    i = (int32_t)len - 1;
    while (i > 0 && path[i] != '/') i--;
    if (i < 0 || path[i] != '/' || (uint32_t)(len - (uint32_t)i - 1) >= name_cap) return -1;

    memcpy(name_out, path + i + 1, len - (uint32_t)i - 1);
    name_out[len - (uint32_t)i - 1] = '\0';

    if (i == 0) {
        *parent_cluster = fs->root_cluster;
        return 0;
    }

    memcpy(parent_path, path, (uint32_t)i);
    parent_path[i] = '\0';
    if (fat32_resolve(fs, parent_path, &parent) != 0) return -1;
    if (!(parent.node.attr & FAT32_ATTR_DIR)) return -1;
    *parent_cluster = parent.node.first_cluster ? parent.node.first_cluster : fs->root_cluster;
    return 0;
}

static int fat32_read_dirent_at(const fat32_fs_t *fs, uint32_t rel_sector, uint32_t off, fat32_dirent_t *out) {
    uint8_t sec[512];
    if (!fs || !out || off > 480 || (off % 32) != 0) return -1;
    if (fat32_read_sectors(fs, rel_sector, 1, sec) != 0) return -1;
    memcpy(out, &sec[off], sizeof(*out));
    return 0;
}

static int fat32_write_dirent_at(const fat32_fs_t *fs, uint32_t rel_sector, uint32_t off, const fat32_dirent_t *in) {
    uint8_t sec[512];
    if (!fs || !in || off > 480 || (off % 32) != 0) return -1;
    if (fat32_read_sectors(fs, rel_sector, 1, sec) != 0) return -1;
    memcpy(&sec[off], in, sizeof(*in));
    if (fat32_write_sectors(fs, rel_sector, 1, sec) != 0) return -1;
    return 0;
}

static int fat32_mark_deleted_at(const fat32_fs_t *fs, uint32_t rel_sector, uint32_t off) {
    uint8_t sec[512];
    if (!fs || off > 511) return -1;
    if (fat32_read_sectors(fs, rel_sector, 1, sec) != 0) return -1;
    sec[off] = 0xE5;
    if (fat32_write_sectors(fs, rel_sector, 1, sec) != 0) return -1;
    return 0;
}

static int fat32_dir_find_free_slot(const fat32_fs_t *fs, uint32_t dir_cluster, uint32_t *out_rel_sec, uint32_t *out_off) {
    uint8_t sec[512];
    uint32_t c = dir_cluster;

    if (!fs || !out_rel_sec || !out_off || c < 2) return -1;

    while (c >= 2 && !fat32_is_eoc(c)) {
        uint32_t rel = fat32_cluster_to_rel_lba(fs, c);
        uint32_t s;
        for (s = 0; s < fs->sectors_per_cluster; s++) {
            uint32_t off;
            if (fat32_read_sectors(fs, rel + s, 1, sec) != 0) return -1;
            for (off = 0; off < 512; off += 32) {
                uint8_t b = sec[off];
                if (b == 0x00 || b == 0xE5) {
                    *out_rel_sec = rel + s;
                    *out_off = off;
                    return 0;
                }
            }
        }

        {
            uint32_t next;
            if (fat32_fat_get(fs, c, &next) != 0) return -1;
            if (fat32_is_eoc(next) || next < 2) {
                uint32_t newc;
                if (fat32_alloc_cluster(fs, &newc) != 0) return -1;
                if (fat32_fat_set(fs, c, newc) != 0) {
                    fat32_free_chain(fs, newc);
                    return -1;
                }
                if (fat32_fat_set(fs, newc, FAT32_EOC) != 0) {
                    fat32_free_chain(fs, newc);
                    return -1;
                }
                *out_rel_sec = fat32_cluster_to_rel_lba(fs, newc);
                *out_off = 0;
                return 0;
            }
            c = next;
        }
    }
    return -1;
}

static int fat32_is_name_char(char c) {
    if (c >= 'a' && c <= 'z') return 1;
    if (c >= 'A' && c <= 'Z') return 1;
    if (c >= '0' && c <= '9') return 1;
    if (c == '_' || c == '-' || c == '$' || c == '~') return 1;
    return 0;
}

static int fat32_make_short_name(const char *name, uint8_t out11[11]) {
    char base[9];
    char ext[4];
    uint32_t bi = 0;
    uint32_t ei = 0;
    uint32_t i;
    int have_dot = 0;

    if (!name || !out11 || name[0] == '\0') return -1;
    if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) return -1;

    for (i = 0; name[i] != '\0'; i++) {
        char c = name[i];
        if (c == '.') {
            if (have_dot) return -1;
            have_dot = 1;
            continue;
        }
        if (!fat32_is_name_char(c)) return -1;
        if (!have_dot) {
            if (bi >= 8) return -1;
            base[bi++] = (char)ascii_toupper(c);
        } else {
            if (ei >= 3) return -1;
            ext[ei++] = (char)ascii_toupper(c);
        }
    }
    if (bi == 0) return -1;

    memset(out11, ' ', 11);
    for (i = 0; i < bi; i++) out11[i] = (uint8_t)base[i];
    for (i = 0; i < ei; i++) out11[8 + i] = (uint8_t)ext[i];
    return 0;
}

static void fat32_fill_dirent(fat32_dirent_t *e, const uint8_t short_name[11], uint8_t attr, uint32_t first_cluster, uint32_t file_size) {
    memset(e, 0, sizeof(*e));
    memcpy(e->name, short_name, 11);
    e->attr = attr;
    e->first_cluster_hi = (uint16_t)((first_cluster >> 16) & 0xFFFFu);
    e->first_cluster_lo = (uint16_t)(first_cluster & 0xFFFFu);
    e->file_size = file_size;
}

static int fat32_dir_is_empty(const fat32_fs_t *fs, uint32_t dir_cluster) {
    uint8_t sec[512];
    uint32_t c = dir_cluster;
    while (c >= 2 && !fat32_is_eoc(c)) {
        uint32_t rel = fat32_cluster_to_rel_lba(fs, c);
        uint32_t s;
        for (s = 0; s < fs->sectors_per_cluster; s++) {
            uint32_t off;
            if (fat32_read_sectors(fs, rel + s, 1, sec) != 0) return 0;
            for (off = 0; off < 512; off += 32) {
                fat32_dirent_t *e = (fat32_dirent_t*)&sec[off];
                if (e->name[0] == 0x00) return 1;
                if (e->name[0] == 0xE5) continue;
                if (e->attr == FAT32_ATTR_LFN) continue;
                if (e->attr & FAT32_ATTR_VOLUME_ID) continue;

                if (e->name[0] == '.' && e->name[1] == ' ') continue;
                if (e->name[0] == '.' && e->name[1] == '.' && e->name[2] == ' ') continue;
                return 0;
            }
        }
        {
            uint32_t next;
            if (fat32_fat_get(fs, c, &next) != 0) return 0;
            c = next;
        }
    }
    return 1;
}

static int disk_name_parse(const char *name, uint8_t *kind, uint8_t *index) {
    if (!name || !kind || !index) return -1;
    if (strcmp(name, "0") == 0 || strcmp(name, "disk0") == 0) {
        *kind = FAT32_DISK_ATA;
        *index = 0;
        return 0;
    }
    return -1;
}

static int disk_read_abs(uint8_t kind, uint8_t index, uint32_t lba, uint32_t count, void *buf) {
    if (kind == FAT32_DISK_ATA) {
        if (index != 0) return -1;
        return disk_read_kernel(lba, count, buf);
    }
    return -1;
}

static int disk_get_total(uint8_t kind, uint8_t index, uint32_t *total, uint32_t *sector_size) {
    uint32_t start = 0;
    uint32_t count = 0;
    if (!total || !sector_size) return -1;
    if (kind == FAT32_DISK_ATA) {
        if (index != 0) return -1;
        if (disk_get_partition_info(0, &start, &count) != 0) return -1;
        *total = count;
        *sector_size = 512;
        return 0;
    }
    return -1;
}

static uint32_t rd_le32(const uint8_t *p) {
    return ((uint32_t)p[0]) |
           ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

static int disk_partition_bounds(
    uint8_t kind, uint8_t index, uint32_t partition_index,
    uint32_t *start_out, uint32_t *count_out
) {
    uint32_t total = 0;
    uint32_t sector_size = 0;
    uint8_t mbr[512];
    uint32_t start;
    uint32_t count;
    uint32_t end;
    const uint8_t *e;
    if (!start_out || !count_out) return -1;
    if (disk_get_total(kind, index, &total, &sector_size) != 0) return -1;
    if (sector_size != 512 || total == 0) return -1;

    if (partition_index == 0) {
        *start_out = 0;
        *count_out = total;
        return 0;
    }
    if (partition_index > 4) return -1;
    if (disk_read_abs(kind, index, 0, 1, mbr) != 0) return -1;
    if (mbr[510] != 0x55 || mbr[511] != 0xAA) return -1;
    e = &mbr[446 + (partition_index - 1u) * 16u];
    start = rd_le32(e + 8);
    count = rd_le32(e + 12);
    if (count == 0) return -1;
    end = start + count;
    if (end < start || end > total) return -1;
    *start_out = start;
    *count_out = count;
    return 0;
}

int fat32_init_named(fat32_fs_t *fs, const char *disk_name, uint32_t partition_index) {
    uint8_t bs[512];
    uint32_t start = 0;
    uint32_t count = 0;
    uint8_t kind = FAT32_DISK_ATA;
    uint8_t index = 0;

    if (!fs || !disk_name) return -1;
    memset(fs, 0, sizeof(*fs));
    if (disk_name_parse(disk_name, &kind, &index) != 0) return -1;
    if (disk_partition_bounds(kind, index, partition_index, &start, &count) != 0) return -1;
    if (count == 0) return -1;

    fs->disk_kind = kind;
    fs->disk_index = index;
    fs->part_start_lba = start;
    fs->part_total_sectors = count;

    if (disk_read_abs(kind, index, start, 1, bs) != 0) return -1;

    fs->bytes_per_sector = (uint16_t)(bs[11] | (bs[12] << 8));
    fs->sectors_per_cluster = bs[13];
    fs->reserved_sectors = (uint16_t)(bs[14] | (bs[15] << 8));
    fs->num_fats = bs[16];
    fs->fat_size_sectors = (uint32_t)(bs[36] | (bs[37] << 8) | (bs[38] << 16) | (bs[39] << 24));
    fs->root_cluster = (uint32_t)(bs[44] | (bs[45] << 8) | (bs[46] << 16) | (bs[47] << 24));

    if (fs->bytes_per_sector != 512 || fs->sectors_per_cluster == 0 || fs->num_fats == 0 || fs->fat_size_sectors == 0) return -1;

    fs->fat_start_lba = fs->reserved_sectors;
    fs->data_start_lba = fs->reserved_sectors + (uint32_t)fs->num_fats * fs->fat_size_sectors;
    if (fs->data_start_lba >= fs->part_total_sectors) return -1;
    if (fs->root_cluster < 2) return -1;
    return 0;
}

int fat32_init(fat32_fs_t *fs, uint32_t partition_index) {
    return fat32_init_named(fs, "disk0", partition_index);
}

int fat32_init_devpath(fat32_fs_t *fs, const char *dev_path) {
    const char *pfx = "/dev/disk/";
    const char *name;
    char disk_name[16];
    uint32_t i = 0;
    uint32_t part = 0;
    const char *ppos = NULL;
    if (!fs || !dev_path) return -1;
    if (strncmp(dev_path, pfx, strlen(pfx)) != 0) return -1;
    name = dev_path + strlen(pfx);
    if (!name[0]) return -1;
    while (name[i] && name[i] != 'p' && i + 1 < sizeof(disk_name)) {
        disk_name[i] = name[i];
        i++;
    }
    disk_name[i] = '\0';
    if (i == 0) return -1;

    ppos = strchr(name, 'p');
    if (ppos && ppos[1]) {
        const char *s = ppos + 1;
        part = 0;
        while (*s) {
            if (*s < '0' || *s > '9') return -1;
            part = part * 10u + (uint32_t)(*s - '0');
            s++;
        }
    }
    return fat32_init_named(fs, disk_name, part);
}

static int fat32_open_op(void *fs_ctx, const char *path) {
    fat32_found_t n;
    return (fat32_resolve((fat32_fs_t*)fs_ctx, path, &n) == 0) ? 0 : -1;
}

static int fat32_close_op(void *fs_ctx, int fd) {
    (void)fs_ctx;
    (void)fd;
    return 0;
}

static ssize_t fat32_read_op(void *fs_ctx, const char *path, void *buf, size_t size) {
    fat32_fs_t *fs = (fat32_fs_t*)fs_ctx;
    fat32_found_t n;
    uint32_t c;
    uint32_t copied = 0;
    uint8_t sec[512];

    if (!fs || !buf) return -1;
    if (fat32_resolve(fs, path, &n) != 0) return -1;
    if (n.node.attr & FAT32_ATTR_DIR) return -1;

    c = n.node.first_cluster;
    if (c < 2 && n.node.size == 0) return 0;

    while (c >= 2 && !fat32_is_eoc(c) && copied < size && copied < n.node.size) {
        uint32_t rel = fat32_cluster_to_rel_lba(fs, c);
        uint32_t s;
        for (s = 0; s < fs->sectors_per_cluster; s++) {
            uint32_t left_file;
            uint32_t left_buf;
            uint32_t take;
            if (copied >= size || copied >= n.node.size) break;
            if (fat32_read_sectors(fs, rel + s, 1, sec) != 0) return -1;
            left_file = n.node.size - copied;
            left_buf = (uint32_t)size - copied;
            take = (left_file < left_buf) ? left_file : left_buf;
            if (take > 512) take = 512;
            memcpy((uint8_t*)buf + copied, sec, take);
            copied += take;
        }
        if (fat32_fat_get(fs, c, &c) != 0) return -1;
    }

    return (ssize_t)copied;
}

static ssize_t fat32_write_op(void *fs_ctx, const char *path, const void *buf, size_t size) {
    fat32_fs_t *fs = (fat32_fs_t*)fs_ctx;
    fat32_found_t n;
    uint32_t old_first;
    uint32_t new_first = 0;
    uint32_t prev = 0;
    uint32_t need_clusters;
    uint32_t cluster_sz;
    uint32_t left;
    const uint8_t *in = (const uint8_t*)buf;
    uint8_t sec[512];
    uint32_t c;

    if (!fs || (!buf && size > 0)) return -1;
    if (fat32_resolve(fs, path, &n) != 0) return -1;
    if (n.node.attr & FAT32_ATTR_DIR) return -1;

    old_first = n.node.first_cluster;
    cluster_sz = fat32_cluster_size(fs);
    need_clusters = (size == 0) ? 0 : (uint32_t)((size + cluster_sz - 1) / cluster_sz);

    for (c = 0; c < need_clusters; c++) {
        uint32_t nc;
        if (fat32_alloc_cluster(fs, &nc) != 0) {
            if (new_first >= 2) fat32_free_chain(fs, new_first);
            return -1;
        }
        if (prev >= 2 && fat32_fat_set(fs, prev, nc) != 0) {
            fat32_free_chain(fs, new_first);
            return -1;
        }
        if (new_first < 2) new_first = nc;
        prev = nc;
    }

    left = (uint32_t)size;
    c = new_first;
    while (left > 0 && c >= 2 && !fat32_is_eoc(c)) {
        uint32_t rel = fat32_cluster_to_rel_lba(fs, c);
        uint32_t s;
        for (s = 0; s < fs->sectors_per_cluster; s++) {
            uint32_t take;
            if (left == 0) {
                memset(sec, 0, sizeof(sec));
                if (fat32_write_sectors(fs, rel + s, 1, sec) != 0) {
                    if (new_first >= 2) fat32_free_chain(fs, new_first);
                    return -1;
                }
                continue;
            }
            take = (left >= 512u) ? 512u : left;
            memset(sec, 0, sizeof(sec));
            memcpy(sec, in, take);
            if (fat32_write_sectors(fs, rel + s, 1, sec) != 0) {
                if (new_first >= 2) fat32_free_chain(fs, new_first);
                return -1;
            }
            in += take;
            left -= take;
        }
        if (left == 0) break;
        if (fat32_fat_get(fs, c, &c) != 0) {
            if (new_first >= 2) fat32_free_chain(fs, new_first);
            return -1;
        }
    }

    {
        fat32_dirent_t e;
        if (fat32_read_dirent_at(fs, n.entry_rel_sector, n.entry_offset, &e) != 0) return -1;
        e.first_cluster_hi = (uint16_t)((new_first >> 16) & 0xFFFFu);
        e.first_cluster_lo = (uint16_t)(new_first & 0xFFFFu);
        e.file_size = (uint32_t)size;
        if (fat32_write_dirent_at(fs, n.entry_rel_sector, n.entry_offset, &e) != 0) return -1;
    }
    
    
    if (old_first >= 2) (void)fat32_free_chain(fs, old_first);

    return (ssize_t)size;
}

static ssize_t fat32_append_op(void *fs_ctx, const char *path, const void *buf, size_t size) {
    fat32_fs_t *fs = (fat32_fs_t*)fs_ctx;
    fat32_found_t n;
    uint32_t cluster_sz;
    uint32_t old_size;
    uint32_t written = 0;
    const uint8_t *in = (const uint8_t*)buf;

    if (!fs || (!buf && size > 0)) return -1;
    if (size == 0) return 0;
    if (fat32_resolve(fs, path, &n) != 0) return -1;
    if (n.node.attr & FAT32_ATTR_DIR) return -1;

    old_size = n.node.size;
    cluster_sz = fat32_cluster_size(fs);

    if (n.node.first_cluster < 2 || old_size == 0) return fat32_write_op(fs_ctx, path, buf, size);

    {
        uint32_t last;
        uint32_t chain_len;
        uint32_t used_in_last;

        if (fat32_chain_last(fs, n.node.first_cluster, &last, &chain_len) != 0) return -1;
        (void)chain_len;

        used_in_last = old_size % cluster_sz;
        if (used_in_last != 0) {
            uint32_t rel = fat32_cluster_to_rel_lba(fs, last);
            uint32_t sec_idx = used_in_last / 512u;
            uint32_t sec_off = used_in_last % 512u;
            uint8_t sec[512];
            uint32_t take;

            if (fat32_read_sectors(fs, rel + sec_idx, 1, sec) != 0) return -1;
            take = (uint32_t)size;
            if (take > 512u - sec_off) take = 512u - sec_off;
            memcpy(sec + sec_off, in, take);
            if (fat32_write_sectors(fs, rel + sec_idx, 1, sec) != 0) return -1;
            in += take;
            size -= take;
            written += take;

            while (size > 0 && ++sec_idx < fs->sectors_per_cluster) {
                take = (uint32_t)size;
                if (take > 512u) take = 512u;
                memset(sec, 0, sizeof(sec));
                memcpy(sec, in, take);
                if (fat32_write_sectors(fs, rel + sec_idx, 1, sec) != 0) return -1;
                in += take;
                size -= take;
                written += take;
            }
        }

        while (size > 0) {
            uint32_t nc;
            uint32_t rel;
            uint32_t s;
            uint8_t sec[512];

            if (fat32_alloc_cluster(fs, &nc) != 0) break;
            if (fat32_fat_set(fs, last, nc) != 0) {
                fat32_free_chain(fs, nc);
                break;
            }
            if (fat32_fat_set(fs, nc, FAT32_EOC) != 0) {
                fat32_free_chain(fs, nc);
                break;
            }
            last = nc;

            rel = fat32_cluster_to_rel_lba(fs, nc);
            for (s = 0; s < fs->sectors_per_cluster; s++) {
                uint32_t take;
                if (size == 0) {
                    memset(sec, 0, sizeof(sec));
                    if (fat32_write_sectors(fs, rel + s, 1, sec) != 0) break;
                    continue;
                }
                take = (uint32_t)size;
                if (take > 512u) take = 512u;
                memset(sec, 0, sizeof(sec));
                memcpy(sec, in, take);
                if (fat32_write_sectors(fs, rel + s, 1, sec) != 0) break;
                in += take;
                size -= take;
                written += take;
            }
        }

        if (written > 0) {
            fat32_dirent_t e;
            if (fat32_read_dirent_at(fs, n.entry_rel_sector, n.entry_offset, &e) != 0) return -1;
            e.file_size = old_size + written;
            if (fat32_write_dirent_at(fs, n.entry_rel_sector, n.entry_offset, &e) != 0) return -1;
        }
    }

    return (ssize_t)written;
}

static int fat32_ioctl_op(void *fs_ctx, const char *path, uint32_t request, void *arg) {
    (void)fs_ctx;
    (void)path;
    (void)request;
    (void)arg;
    return -1;
}

static int fat32_mkdir_op(void *fs_ctx, const char *path) {
    fat32_fs_t *fs = (fat32_fs_t*)fs_ctx;
    uint32_t parent_cluster;
    char name[256];
    uint8_t short_name[11];
    fat32_found_t exists;
    uint32_t rel_sec;
    uint32_t off;
    uint32_t new_dir_cluster;
    fat32_dirent_t e;
    uint8_t cluster_buf[512];

    if (!fs || !path) return -1;
    if (fat32_resolve_parent(fs, path, &parent_cluster, name, sizeof(name)) != 0) return -1;
    if (fat32_make_short_name(name, short_name) != 0) return -1;
    if (fat32_dir_find(fs, parent_cluster, name, &exists) == 0) return -1;
    if (fat32_dir_find_free_slot(fs, parent_cluster, &rel_sec, &off) != 0) return -1;

    if (fat32_alloc_cluster(fs, &new_dir_cluster) != 0) return -1;

    memset(cluster_buf, 0, sizeof(cluster_buf));
    fat32_fill_dirent((fat32_dirent_t*)&cluster_buf[0], (const uint8_t*)".          ", FAT32_ATTR_DIR, new_dir_cluster, 0);
    fat32_fill_dirent((fat32_dirent_t*)&cluster_buf[32], (const uint8_t*)"..         ", FAT32_ATTR_DIR, parent_cluster, 0);
    if (fat32_write_sectors(fs, fat32_cluster_to_rel_lba(fs, new_dir_cluster), 1, cluster_buf) != 0) {
        fat32_free_chain(fs, new_dir_cluster);
        return -1;
    }

    fat32_fill_dirent(&e, short_name, FAT32_ATTR_DIR, new_dir_cluster, 0);
    if (fat32_write_dirent_at(fs, rel_sec, off, &e) != 0) {
        fat32_free_chain(fs, new_dir_cluster);
        return -1;
    }

    return 0;
}

static int fat32_create_file_op(void *fs_ctx, const char *path) {
    fat32_fs_t *fs = (fat32_fs_t*)fs_ctx;
    uint32_t parent_cluster;
    char name[256];
    uint8_t short_name[11];
    fat32_found_t exists;
    uint32_t rel_sec;
    uint32_t off;
    fat32_dirent_t e;

    if (!fs || !path) return -1;
    if (fat32_resolve_parent(fs, path, &parent_cluster, name, sizeof(name)) != 0) return -1;
    if (fat32_make_short_name(name, short_name) != 0) return -1;
    if (fat32_dir_find(fs, parent_cluster, name, &exists) == 0) return 0;
    if (fat32_dir_find_free_slot(fs, parent_cluster, &rel_sec, &off) != 0) return -1;

    fat32_fill_dirent(&e, short_name, FAT32_ATTR_ARCHIVE, 0, 0);
    if (fat32_write_dirent_at(fs, rel_sec, off, &e) != 0) return -1;
    return 0;
}

static int fat32_link_op(void *fs_ctx, const char *oldpath, const char *newpath) {
    (void)fs_ctx;
    (void)oldpath;
    (void)newpath;
    return -1;
}

static int fat32_unlink_op(void *fs_ctx, const char *path) {
    fat32_fs_t *fs = (fat32_fs_t*)fs_ctx;
    fat32_found_t n;
    if (!fs || !path) return -1;
    if (fat32_resolve(fs, path, &n) != 0) return -1;
    if (n.node.attr & FAT32_ATTR_DIR) return -1;
    if (fat32_mark_deleted_at(fs, n.entry_rel_sector, n.entry_offset) != 0) return -1;
    if (n.node.first_cluster >= 2) {
        if (fat32_free_chain(fs, n.node.first_cluster) != 0) return -1;
    }
    return 0;
}

static int fat32_rmdir_op(void *fs_ctx, const char *path) {
    fat32_fs_t *fs = (fat32_fs_t*)fs_ctx;
    fat32_found_t n;
    uint32_t dir_cluster;

    if (!fs || !path || strcmp(path, "/") == 0) return -1;
    if (fat32_resolve(fs, path, &n) != 0) return -1;
    if (!(n.node.attr & FAT32_ATTR_DIR)) return -1;

    dir_cluster = n.node.first_cluster ? n.node.first_cluster : fs->root_cluster;
    if (!fat32_dir_is_empty(fs, dir_cluster)) return -1;

    if (fat32_mark_deleted_at(fs, n.entry_rel_sector, n.entry_offset) != 0) return -1;
    if (dir_cluster >= 2 && dir_cluster != fs->root_cluster) {
        if (fat32_free_chain(fs, dir_cluster) != 0) return -1;
    }
    return 0;
}

static int fat32_mkfifo_op(void *fs_ctx, const char *path) {
    (void)fs_ctx;
    (void)path;
    return -1;
}

static int fat32_mksock_op(void *fs_ctx, const char *path) {
    (void)fs_ctx;
    (void)path;
    return -1;
}

static ssize_t fat32_list_op(void *fs_ctx, const char *path, char *out, size_t out_size) {
    fat32_fs_t *fs = (fat32_fs_t*)fs_ctx;
    fat32_found_t dir;
    uint8_t sec[512];
    uint32_t c;
    uint32_t pos = 0;
    char lfn_parts[20][14];
    uint8_t lfn_seen[20];

    if (!fs || !out || out_size == 0) return -1;
    if (fat32_resolve(fs, path, &dir) != 0) return -1;
    if (!(dir.node.attr & FAT32_ATTR_DIR)) return -1;

    c = dir.node.first_cluster ? dir.node.first_cluster : fs->root_cluster;
    out[0] = '\0';
    memset(lfn_seen, 0, sizeof(lfn_seen));

    while (c >= 2 && !fat32_is_eoc(c)) {
        uint32_t rel = fat32_cluster_to_rel_lba(fs, c);
        uint32_t s;
        for (s = 0; s < fs->sectors_per_cluster; s++) {
            uint32_t off;
            if (fat32_read_sectors(fs, rel + s, 1, sec) != 0) return -1;
            for (off = 0; off < 512; off += 32) {
                fat32_dirent_t *e = (fat32_dirent_t*)&sec[off];
                if (e->name[0] == 0x00) {
                    out[pos] = '\0';
                    return (ssize_t)pos;
                }
                if (e->name[0] == 0xE5) {
                    memset(lfn_seen, 0, sizeof(lfn_seen));
                    continue;
                }
                if (e->attr == FAT32_ATTR_LFN) {
                    fat32_lfn_t *lfn = (fat32_lfn_t*)e;
                    uint8_t idx = (uint8_t)(lfn->ord & 0x1F);
                    if (idx > 0 && idx <= 20) {
                        lfn_extract_piece(lfn, lfn_parts[idx - 1], sizeof(lfn_parts[0]));
                        lfn_seen[idx - 1] = 1;
                    }
                    continue;
                }
                if (e->attr & FAT32_ATTR_VOLUME_ID) {
                    memset(lfn_seen, 0, sizeof(lfn_seen));
                    continue;
                }

                {
                    char name[256];
                    uint32_t nlen;
                    if (lfn_seen[0]) {
                        uint32_t p = 0;
                        uint32_t i;
                        for (i = 0; i < 20; i++) {
                            uint32_t j;
                            if (!lfn_seen[i]) break;
                            for (j = 0; lfn_parts[i][j] != '\0'; j++) {
                                if (p + 1 >= sizeof(name)) break;
                                name[p++] = lfn_parts[i][j];
                            }
                        }
                        name[p] = '\0';
                    } else {
                        decode_short_name(e, name, sizeof(name));
                    }
                    memset(lfn_seen, 0, sizeof(lfn_seen));

                    if (name_eq_ci(name, ".") || name_eq_ci(name, "..")) continue;

                    nlen = (uint32_t)strlen(name);
                    if (pos + nlen + 2 >= out_size) {
                        out[pos] = '\0';
                        return (ssize_t)pos;
                    }
                    memcpy(out + pos, name, nlen);
                    pos += nlen;
                    if (e->attr & FAT32_ATTR_DIR) out[pos++] = '/';
                    out[pos++] = '\n';
                    out[pos] = '\0';
                }
            }
        }
        if (fat32_fat_get(fs, c, &c) != 0) return -1;
    }

    out[pos] = '\0';
    return (ssize_t)pos;
}

static int fat32_get_info_op(void *fs_ctx, const char *path, vfs_info_t *out) {
    fat32_fs_t *fs = (fat32_fs_t*)fs_ctx;
    fat32_found_t n;

    if (!fs || !out) return -1;
    if (fat32_resolve(fs, path, &n) != 0) return -1;

    out->type = (n.node.attr & FAT32_ATTR_DIR) ? VFS_NODE_DIR : VFS_NODE_FILE;
    out->mode = 0;
    out->uid = 0;
    out->gid = 0;
    out->size = n.node.size;
    return 0;
}

const vfs_ops_t g_fat32_vfs_ops = {
    fat32_open_op,
    fat32_close_op,
    fat32_read_op,
    fat32_write_op,
    fat32_append_op,
    fat32_ioctl_op,
    fat32_mkdir_op,
    fat32_create_file_op,
    fat32_link_op,
    fat32_unlink_op,
    fat32_rmdir_op,
    fat32_mkfifo_op,
    fat32_mksock_op,
    fat32_list_op,
    fat32_get_info_op
};
