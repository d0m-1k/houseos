#include <dylib.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <syscall.h>

#define DL_MAX_LIBS 4
#define DL_FILE_CAP (64u * 1024u)
#define DL_IMAGE_CAP (64u * 1024u)

#define ELF_MAGIC 0x464C457Fu
#define ELFCLASS32 1u
#define ELFDATA2LSB 1u
#define ET_EXEC 2u
#define ET_DYN 3u
#define EM_386 3u
#define PT_LOAD 1u
#define SHT_SYMTAB 2u
#define SHT_DYNSYM 11u

typedef struct {
    uint8_t e_ident[16];
    uint16_t e_type;
    uint16_t e_machine;
    uint32_t e_version;
    uint32_t e_entry;
    uint32_t e_phoff;
    uint32_t e_shoff;
    uint32_t e_flags;
    uint16_t e_ehsize;
    uint16_t e_phentsize;
    uint16_t e_phnum;
    uint16_t e_shentsize;
    uint16_t e_shnum;
    uint16_t e_shstrndx;
} __attribute__((packed)) elf32_ehdr_t;

typedef struct {
    uint32_t p_type;
    uint32_t p_offset;
    uint32_t p_vaddr;
    uint32_t p_paddr;
    uint32_t p_filesz;
    uint32_t p_memsz;
    uint32_t p_flags;
    uint32_t p_align;
} __attribute__((packed)) elf32_phdr_t;

typedef struct {
    uint32_t sh_name;
    uint32_t sh_type;
    uint32_t sh_flags;
    uint32_t sh_addr;
    uint32_t sh_offset;
    uint32_t sh_size;
    uint32_t sh_link;
    uint32_t sh_info;
    uint32_t sh_addralign;
    uint32_t sh_entsize;
} __attribute__((packed)) elf32_shdr_t;

typedef struct {
    uint32_t st_name;
    uint32_t st_value;
    uint32_t st_size;
    uint8_t st_info;
    uint8_t st_other;
    uint16_t st_shndx;
} __attribute__((packed)) elf32_sym_t;

typedef struct {
    uint8_t used;
    char path[96];
    uint32_t load_bias;
    uint32_t min_vaddr;
    uint32_t max_vaddr;
    uint32_t file_size;
    uint8_t file[DL_FILE_CAP];
    uint8_t image[DL_IMAGE_CAP];
} dl_slot_t;

static dl_slot_t g_slots[DL_MAX_LIBS];
static char g_dl_err[96];

static int set_err(const char *s) {
    strncpy(g_dl_err, s ? s : "dl error", sizeof(g_dl_err) - 1u);
    g_dl_err[sizeof(g_dl_err) - 1u] = '\0';
    return -1;
}

static int read_file_all(const char *path, uint8_t *out, uint32_t cap, uint32_t *sz_out) {
    int fd;
    uint32_t off = 0;
    if (!path || !out || !sz_out || cap == 0u) return -1;
    fd = open(path, 0);
    if (fd < 0) return -1;
    while (off < cap) {
        int32_t n = read(fd, out + off, cap - off);
        if (n < 0) {
            close(fd);
            return -1;
        }
        if (n == 0) break;
        off += (uint32_t)n;
    }
    close(fd);
    *sz_out = off;
    return 0;
}

static int parse_and_load(dl_slot_t *s) {
    elf32_ehdr_t *eh;
    elf32_phdr_t *ph;
    uint32_t min_v = 0xFFFFFFFFu;
    uint32_t max_v = 0u;
    uint32_t span;
    uint32_t i;

    if (!s || s->file_size < sizeof(elf32_ehdr_t)) return set_err("dl: bad file");
    eh = (elf32_ehdr_t*)s->file;
    if (*(uint32_t*)&eh->e_ident[0] != ELF_MAGIC ||
        eh->e_ident[4] != ELFCLASS32 ||
        eh->e_ident[5] != ELFDATA2LSB ||
        (eh->e_type != ET_DYN && eh->e_type != ET_EXEC) ||
        eh->e_machine != EM_386 ||
        eh->e_phentsize != sizeof(elf32_phdr_t)) {
        return set_err("dl: bad elf");
    }
    if (eh->e_phoff + (uint32_t)eh->e_phnum * sizeof(elf32_phdr_t) > s->file_size) {
        return set_err("dl: bad phdr");
    }

    ph = (elf32_phdr_t*)(s->file + eh->e_phoff);
    for (i = 0; i < eh->e_phnum; i++) {
        uint32_t end_v;
        if (ph[i].p_type != PT_LOAD) continue;
        if (ph[i].p_memsz == 0u) continue;
        if (ph[i].p_offset + ph[i].p_filesz > s->file_size) return set_err("dl: segment range");
        if (ph[i].p_vaddr < min_v) min_v = ph[i].p_vaddr;
        end_v = ph[i].p_vaddr + ph[i].p_memsz;
        if (end_v < ph[i].p_vaddr) return set_err("dl: segment overflow");
        if (end_v > max_v) max_v = end_v;
    }
    if (min_v == 0xFFFFFFFFu || max_v <= min_v) return set_err("dl: empty image");
    span = max_v - min_v;
    if (span > DL_IMAGE_CAP) return set_err("dl: image too big");

    memset(s->image, 0, span);
    for (i = 0; i < eh->e_phnum; i++) {
        uint32_t dst_off;
        if (ph[i].p_type != PT_LOAD || ph[i].p_memsz == 0u) continue;
        dst_off = ph[i].p_vaddr - min_v;
        memcpy(s->image + dst_off, s->file + ph[i].p_offset, ph[i].p_filesz);
    }

    s->min_vaddr = min_v;
    s->max_vaddr = max_v;
    s->load_bias = (uint32_t)(uintptr_t)s->image - min_v;
    return 0;
}

int dl_open(const char *path) {
    uint32_t i;
    dl_slot_t *s = NULL;
    g_dl_err[0] = '\0';
    if (!path || path[0] == '\0') return set_err("dl: bad path");

    for (i = 0; i < DL_MAX_LIBS; i++) {
        if (g_slots[i].used && strcmp(g_slots[i].path, path) == 0) return (int)i;
    }
    for (i = 0; i < DL_MAX_LIBS; i++) {
        if (!g_slots[i].used) {
            s = &g_slots[i];
            break;
        }
    }
    if (!s) return set_err("dl: no slots");
    memset(s, 0, sizeof(*s));
    strncpy(s->path, path, sizeof(s->path) - 1u);
    s->path[sizeof(s->path) - 1u] = '\0';
    if (read_file_all(path, s->file, sizeof(s->file), &s->file_size) != 0) return set_err("dl: read failed");
    if (parse_and_load(s) != 0) return -1;
    s->used = 1;
    return (int)(s - g_slots);
}

void *dl_sym(int handle, const char *name) {
    dl_slot_t *s;
    elf32_ehdr_t *eh;
    elf32_shdr_t *sh;
    uint32_t i;
    g_dl_err[0] = '\0';
    if (handle < 0 || handle >= DL_MAX_LIBS || !name) return NULL;
    s = &g_slots[handle];
    if (!s->used) return NULL;
    if (s->file_size < sizeof(elf32_ehdr_t)) return NULL;
    eh = (elf32_ehdr_t*)s->file;
    if (eh->e_shoff == 0u || eh->e_shnum == 0u || eh->e_shentsize != sizeof(elf32_shdr_t)) return NULL;
    if (eh->e_shoff + (uint32_t)eh->e_shnum * sizeof(elf32_shdr_t) > s->file_size) return NULL;
    sh = (elf32_shdr_t*)(s->file + eh->e_shoff);

    for (i = 0; i < eh->e_shnum; i++) {
        elf32_shdr_t *sym_sh;
        elf32_shdr_t *str_sh;
        elf32_sym_t *symtab;
        const char *strtab;
        uint32_t n;
        uint32_t j;
        if (sh[i].sh_type != SHT_SYMTAB && sh[i].sh_type != SHT_DYNSYM) continue;
        sym_sh = &sh[i];
        if (sym_sh->sh_entsize != sizeof(elf32_sym_t) || sym_sh->sh_link >= eh->e_shnum) continue;
        str_sh = &sh[sym_sh->sh_link];
        if (sym_sh->sh_offset + sym_sh->sh_size > s->file_size) continue;
        if (str_sh->sh_offset + str_sh->sh_size > s->file_size) continue;
        symtab = (elf32_sym_t*)(s->file + sym_sh->sh_offset);
        strtab = (const char*)(s->file + str_sh->sh_offset);
        n = sym_sh->sh_size / sizeof(elf32_sym_t);
        for (j = 0; j < n; j++) {
            const char *sn;
            uint32_t v;
            if (symtab[j].st_name >= str_sh->sh_size) continue;
            sn = strtab + symtab[j].st_name;
            if (strcmp(sn, name) != 0) continue;
            v = symtab[j].st_value;
            return (void*)(uintptr_t)(s->load_bias + v);
        }
    }
    set_err("dl: symbol not found");
    return NULL;
}

void dl_close(int handle) {
    if (handle < 0 || handle >= DL_MAX_LIBS) return;
    memset(&g_slots[handle], 0, sizeof(g_slots[handle]));
}

const char *dl_error(void) {
    return g_dl_err[0] ? g_dl_err : "ok";
}

