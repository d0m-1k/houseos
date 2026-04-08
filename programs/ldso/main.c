#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <syscall.h>

#define USER_VADDR_BASE 0x00400000u
#define USER_VADDR_SIZE 0x00400000u
#define USER_STACK_TOP  (USER_VADDR_BASE + USER_VADDR_SIZE - 16u)

#define USER_LIB_START 0x006c0000u
#define USER_LIB_END   0x007a0000u

#define LOADER_MAX_ARGS 64
#define LOADER_MAX_OBJS 16
#define LOADER_MAX_NEEDED 16
#define LOADER_FILE_CAP (128u * 1024u)

#define ELF_MAGIC 0x464C457Fu
#define ELFCLASS32 1u
#define ELFDATA2LSB 1u
#define ET_EXEC 2u
#define ET_DYN 3u
#define EM_386 3u

#define PT_LOAD 1u
#define PT_DYNAMIC 2u

#define DT_NULL 0u
#define DT_NEEDED 1u
#define DT_HASH 4u
#define DT_STRTAB 5u
#define DT_SYMTAB 6u
#define DT_STRSZ 10u
#define DT_SYMENT 11u
#define DT_REL 17u
#define DT_RELSZ 18u
#define DT_RELENT 19u
#define DT_PLTREL 20u
#define DT_JMPREL 23u
#define DT_PLTRELSZ 2u

#define R_386_NONE 0u
#define R_386_32 1u
#define R_386_PC32 2u
#define R_386_GLOB_DAT 6u
#define R_386_JMP_SLOT 7u
#define R_386_RELATIVE 8u

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
    int32_t d_tag;
    union {
        uint32_t d_val;
        uint32_t d_ptr;
    } d_un;
} __attribute__((packed)) elf32_dyn_t;

typedef struct {
    uint32_t st_name;
    uint32_t st_value;
    uint32_t st_size;
    uint8_t st_info;
    uint8_t st_other;
    uint16_t st_shndx;
} __attribute__((packed)) elf32_sym_t;

typedef struct {
    uint32_t r_offset;
    uint32_t r_info;
} __attribute__((packed)) elf32_rel_t;

typedef struct {
    uint8_t used;
    uint8_t is_main;
    char path[96];
    char soname[64];

    uint16_t e_type;
    uint32_t entry;
    uint32_t load_bias;
    uint32_t min_vaddr;
    uint32_t max_vaddr;

    elf32_sym_t *symtab;
    const char *strtab;
    uint32_t strsz;
    uint32_t symcnt;

    elf32_rel_t *rel;
    uint32_t rel_count;
    elf32_rel_t *jmprel;
    uint32_t jmprel_count;

    uint32_t needed[LOADER_MAX_NEEDED];
    uint32_t needed_count;
} ld_obj_t;

static ld_obj_t g_objs[LOADER_MAX_OBJS];
static uint32_t g_obj_count = 0;
static uint32_t g_next_lib_base = USER_LIB_START;
static uint8_t g_file_buf[LOADER_FILE_CAP];

static uint32_t align_up(uint32_t v, uint32_t a) {
    return (v + (a - 1u)) & ~(a - 1u);
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

static int parse_ehdr(const uint8_t *file, uint32_t file_sz, elf32_ehdr_t **eh_out, elf32_phdr_t **ph_out) {
    elf32_ehdr_t *eh;
    if (!file || file_sz < sizeof(elf32_ehdr_t) || !eh_out || !ph_out) return -1;
    eh = (elf32_ehdr_t*)file;
    if (*(uint32_t*)&eh->e_ident[0] != ELF_MAGIC ||
        eh->e_ident[4] != ELFCLASS32 ||
        eh->e_ident[5] != ELFDATA2LSB ||
        eh->e_machine != EM_386 ||
        eh->e_phentsize != sizeof(elf32_phdr_t)) {
        return -1;
    }
    if (eh->e_type != ET_EXEC && eh->e_type != ET_DYN) return -1;
    if (eh->e_phoff + (uint32_t)eh->e_phnum * sizeof(elf32_phdr_t) > file_sz) return -1;
    *eh_out = eh;
    *ph_out = (elf32_phdr_t*)(file + eh->e_phoff);
    return 0;
}

static uint32_t obj_rt_addr(const ld_obj_t *o, uint32_t vaddr) {
    if (o->e_type == ET_DYN) return o->load_bias + vaddr;
    return vaddr;
}

static void *obj_rt_ptr(const ld_obj_t *o, uint32_t vaddr) {
    return (void*)(uintptr_t)obj_rt_addr(o, vaddr);
}

static int lookup_obj_by_soname_or_path(const char *path, const char *soname) {
    uint32_t i;
    for (i = 0; i < g_obj_count; i++) {
        if (!g_objs[i].used) continue;
        if (path && strcmp(g_objs[i].path, path) == 0) return (int)i;
        if (soname && soname[0] && strcmp(g_objs[i].soname, soname) == 0) return (int)i;
    }
    return -1;
}

static int alloc_obj_slot(void) {
    if (g_obj_count >= LOADER_MAX_OBJS) return -1;
    memset(&g_objs[g_obj_count], 0, sizeof(g_objs[g_obj_count]));
    g_objs[g_obj_count].used = 1;
    return (int)g_obj_count++;
}

static int load_shared_object(const char *path, const char *soname_hint) {
    uint32_t file_sz = 0;
    elf32_ehdr_t *eh;
    elf32_phdr_t *ph;
    uint32_t i;
    uint32_t min_v = 0xffffffffu;
    uint32_t max_v = 0u;
    uint32_t span;
    uint32_t base;
    uint32_t dyn_vaddr = 0u;
    uint32_t dyn_off = 0u;
    uint32_t dyn_sz = 0u;
    int slot;
    ld_obj_t *o;

    if (!path) return -1;

    slot = lookup_obj_by_soname_or_path(path, soname_hint);
    if (slot >= 0) return slot;

    if (read_file_all(path, g_file_buf, sizeof(g_file_buf), &file_sz) != 0) {
        fprintf(stderr, "ld-house.so: failed to read %s\n", path);
        return -1;
    }
    if (parse_ehdr(g_file_buf, file_sz, &eh, &ph) != 0 || eh->e_type != ET_DYN) {
        fprintf(stderr, "ld-house.so: bad shared object %s\n", path);
        return -1;
    }

    for (i = 0; i < eh->e_phnum; i++) {
        uint32_t end_v;
        if (ph[i].p_type == PT_DYNAMIC) {
            dyn_vaddr = ph[i].p_vaddr;
            dyn_off = ph[i].p_offset;
            dyn_sz = ph[i].p_filesz;
        }
        if (ph[i].p_type != PT_LOAD || ph[i].p_memsz == 0u) continue;
        if (ph[i].p_offset + ph[i].p_filesz > file_sz) return -1;
        if (ph[i].p_vaddr < min_v) min_v = ph[i].p_vaddr;
        end_v = ph[i].p_vaddr + ph[i].p_memsz;
        if (end_v < ph[i].p_vaddr) return -1;
        if (end_v > max_v) max_v = end_v;
    }
    if (min_v == 0xffffffffu || max_v <= min_v || dyn_vaddr == 0u) return -1;

    span = align_up(max_v - min_v, 0x1000u);
    base = align_up(g_next_lib_base, 0x1000u);
    if (base + span > USER_LIB_END) {
        fprintf(stderr, "ld-house.so: out of user VA space for %s\n", path);
        return -1;
    }
    g_next_lib_base = base + span;

    slot = alloc_obj_slot();
    if (slot < 0) return -1;
    o = &g_objs[slot];

    strncpy(o->path, path, sizeof(o->path) - 1u);
    o->path[sizeof(o->path) - 1u] = '\0';
    if (soname_hint && soname_hint[0]) {
        strncpy(o->soname, soname_hint, sizeof(o->soname) - 1u);
        o->soname[sizeof(o->soname) - 1u] = '\0';
    }
    o->e_type = ET_DYN;
    o->load_bias = base - min_v;
    o->min_vaddr = min_v;
    o->max_vaddr = max_v;

    memset((void*)(uintptr_t)base, 0, span);
    for (i = 0; i < eh->e_phnum; i++) {
        uint32_t dst;
        if (ph[i].p_type != PT_LOAD || ph[i].p_memsz == 0u) continue;
        dst = o->load_bias + ph[i].p_vaddr;
        memcpy((void*)(uintptr_t)dst, g_file_buf + ph[i].p_offset, ph[i].p_filesz);
        if (ph[i].p_memsz > ph[i].p_filesz) {
            memset((void*)(uintptr_t)(dst + ph[i].p_filesz), 0, ph[i].p_memsz - ph[i].p_filesz);
        }
    }

    {
        elf32_dyn_t *dyn;
        uint32_t dyn_cnt;
        uint32_t max_sym_idx = 0u;
        uint32_t symtab_v = 0u;
        uint32_t strtab_v = 0u;

        if (dyn_off + dyn_sz > file_sz || dyn_sz < sizeof(elf32_dyn_t)) {
            fprintf(stderr, "ld-house.so: bad dynamic range: %s\n", path);
            return -1;
        }

        o->symtab = NULL;
        o->strtab = NULL;
        o->strsz = 0u;
        o->symcnt = 0u;
        o->rel = NULL;
        o->rel_count = 0u;
        o->jmprel = NULL;
        o->jmprel_count = 0u;
        o->needed_count = 0u;

        dyn = (elf32_dyn_t*)(g_file_buf + dyn_off);
        dyn_cnt = dyn_sz / (uint32_t)sizeof(elf32_dyn_t);
        for (i = 0; i < dyn_cnt; i++) {
            if (dyn[i].d_tag == (int32_t)DT_NULL) break;
            switch ((uint32_t)dyn[i].d_tag) {
                case DT_NEEDED:
                    if (o->needed_count < LOADER_MAX_NEEDED) o->needed[o->needed_count++] = dyn[i].d_un.d_val;
                    break;
                case DT_STRTAB:
                    strtab_v = dyn[i].d_un.d_ptr;
                    o->strtab = (const char*)obj_rt_ptr(o, dyn[i].d_un.d_ptr);
                    break;
                case DT_SYMTAB:
                    symtab_v = dyn[i].d_un.d_ptr;
                    o->symtab = (elf32_sym_t*)obj_rt_ptr(o, dyn[i].d_un.d_ptr);
                    break;
                case DT_STRSZ:
                    o->strsz = dyn[i].d_un.d_val;
                    break;
                case DT_REL:
                    o->rel = (elf32_rel_t*)obj_rt_ptr(o, dyn[i].d_un.d_ptr);
                    break;
                case DT_RELSZ:
                    o->rel_count = dyn[i].d_un.d_val / (uint32_t)sizeof(elf32_rel_t);
                    break;
                case DT_JMPREL:
                    o->jmprel = (elf32_rel_t*)obj_rt_ptr(o, dyn[i].d_un.d_ptr);
                    break;
                case DT_PLTRELSZ:
                    o->jmprel_count = dyn[i].d_un.d_val / (uint32_t)sizeof(elf32_rel_t);
                    break;
                case DT_HASH: {
                    uint32_t j;
                    uint32_t hash_off = 0u;
                    int found = 0;
                    for (j = 0; j < eh->e_phnum; j++) {
                        if (ph[j].p_type != PT_LOAD || ph[j].p_filesz == 0u) continue;
                        if (dyn[i].d_un.d_ptr < ph[j].p_vaddr) continue;
                        if (dyn[i].d_un.d_ptr + 8u > ph[j].p_vaddr + ph[j].p_filesz) continue;
                        hash_off = ph[j].p_offset + (dyn[i].d_un.d_ptr - ph[j].p_vaddr);
                        found = 1;
                        break;
                    }
                    if (found && hash_off + 8u <= file_sz) {
                        uint32_t *h = (uint32_t*)(g_file_buf + hash_off);
                        o->symcnt = h[1];
                    }
                    break;
                }
                default:
                    break;
            }
        }
        if (!o->strtab || !o->symtab) {
            fprintf(stderr, "ld-house.so: dynamic incomplete: %s\n", path);
            return -1;
        }
        if (o->symcnt == 0u && strtab_v > symtab_v) {
            o->symcnt = (strtab_v - symtab_v) / (uint32_t)sizeof(elf32_sym_t);
        }
        if (o->symcnt == 0u) {
            for (i = 0; i < o->rel_count; i++) {
                uint32_t si = o->rel[i].r_info >> 8;
                if (si > max_sym_idx) max_sym_idx = si;
            }
            for (i = 0; i < o->jmprel_count; i++) {
                uint32_t si = o->jmprel[i].r_info >> 8;
                if (si > max_sym_idx) max_sym_idx = si;
            }
            o->symcnt = max_sym_idx + 1u;
        }
    }
    return slot;
}

static int add_main_object(const char *path) {
    uint32_t file_sz = 0;
    elf32_ehdr_t *eh;
    elf32_phdr_t *ph;
    uint32_t i;
    uint32_t dyn_vaddr = 0u;
    uint32_t dyn_off = 0u;
    uint32_t dyn_sz = 0u;
    int slot;
    ld_obj_t *o;

    if (read_file_all(path, g_file_buf, sizeof(g_file_buf), &file_sz) != 0) return -1;
    if (parse_ehdr(g_file_buf, file_sz, &eh, &ph) != 0 || eh->e_type != ET_EXEC) return -1;

    for (i = 0; i < eh->e_phnum; i++) {
        if (ph[i].p_type == PT_DYNAMIC) {
            dyn_vaddr = ph[i].p_vaddr;
            dyn_off = ph[i].p_offset;
            dyn_sz = ph[i].p_filesz;
            break;
        }
    }

    slot = alloc_obj_slot();
    if (slot < 0) return -1;
    o = &g_objs[slot];

    o->is_main = 1;
    o->e_type = ET_EXEC;
    o->entry = eh->e_entry;
    o->load_bias = 0u;
    strncpy(o->path, path, sizeof(o->path) - 1u);
    o->path[sizeof(o->path) - 1u] = '\0';
    strncpy(o->soname, "<main>", sizeof(o->soname) - 1u);
    o->soname[sizeof(o->soname) - 1u] = '\0';

    if (dyn_vaddr != 0u) {
        elf32_dyn_t *dyn;
        uint32_t dyn_cnt;
        uint32_t max_sym_idx = 0u;

        if (dyn_off + dyn_sz > file_sz || dyn_sz < sizeof(elf32_dyn_t)) {
            fprintf(stderr, "ld-house.so: bad main dynamic range: %s\n", path);
            return -1;
        }

        o->symtab = NULL;
        o->strtab = NULL;
        o->strsz = 0u;
        o->symcnt = 0u;
        o->rel = NULL;
        o->rel_count = 0u;
        o->jmprel = NULL;
        o->jmprel_count = 0u;
        o->needed_count = 0u;

        dyn = (elf32_dyn_t*)(g_file_buf + dyn_off);
        dyn_cnt = dyn_sz / (uint32_t)sizeof(elf32_dyn_t);
        for (i = 0; i < dyn_cnt; i++) {
            if (dyn[i].d_tag == (int32_t)DT_NULL) break;
            switch ((uint32_t)dyn[i].d_tag) {
                case DT_NEEDED:
                    if (o->needed_count < LOADER_MAX_NEEDED) o->needed[o->needed_count++] = dyn[i].d_un.d_val;
                    break;
                case DT_STRTAB:
                    o->strtab = (const char*)obj_rt_ptr(o, dyn[i].d_un.d_ptr);
                    break;
                case DT_SYMTAB:
                    o->symtab = (elf32_sym_t*)obj_rt_ptr(o, dyn[i].d_un.d_ptr);
                    break;
                case DT_STRSZ:
                    o->strsz = dyn[i].d_un.d_val;
                    break;
                case DT_REL:
                    o->rel = (elf32_rel_t*)obj_rt_ptr(o, dyn[i].d_un.d_ptr);
                    break;
                case DT_RELSZ:
                    o->rel_count = dyn[i].d_un.d_val / (uint32_t)sizeof(elf32_rel_t);
                    break;
                case DT_JMPREL:
                    o->jmprel = (elf32_rel_t*)obj_rt_ptr(o, dyn[i].d_un.d_ptr);
                    break;
                case DT_PLTRELSZ:
                    o->jmprel_count = dyn[i].d_un.d_val / (uint32_t)sizeof(elf32_rel_t);
                    break;
                case DT_HASH: {
                    uint32_t *h = (uint32_t*)obj_rt_ptr(o, dyn[i].d_un.d_ptr);
                    o->symcnt = h[1];
                    break;
                }
                default:
                    break;
            }
        }

        if (!o->strtab || !o->symtab) {
            fprintf(stderr, "ld-house.so: main dynamic incomplete: %s\n", path);
            return -1;
        }
        if (o->symcnt == 0u) {
            for (i = 0; i < o->rel_count; i++) {
                uint32_t si = o->rel[i].r_info >> 8;
                if (si > max_sym_idx) max_sym_idx = si;
            }
            for (i = 0; i < o->jmprel_count; i++) {
                uint32_t si = o->jmprel[i].r_info >> 8;
                if (si > max_sym_idx) max_sym_idx = si;
            }
            o->symcnt = max_sym_idx + 1u;
        }
    }

    return slot;
}

static uint32_t sym_addr_from_obj(const ld_obj_t *o, uint32_t idx) {
    elf32_sym_t *s;
    if (!o || !o->symtab || idx >= o->symcnt) return 0u;
    s = &o->symtab[idx];
    if (s->st_shndx == 0u || s->st_value == 0u) return 0u;
    return obj_rt_addr(o, s->st_value);
}

static uint32_t resolve_global_symbol(const char *name) {
    uint32_t oi;
    if (!name || !name[0]) return 0u;
    for (oi = 0; oi < g_obj_count; oi++) {
        const ld_obj_t *o = &g_objs[oi];
        uint32_t si;
        if (!o->used || !o->symtab || !o->strtab) continue;
        for (si = 1u; si < o->symcnt; si++) {
            elf32_sym_t *s = &o->symtab[si];
            const char *sn;
            if (s->st_shndx == 0u || s->st_name >= o->strsz) continue;
            sn = o->strtab + s->st_name;
            if (strcmp(sn, name) != 0) continue;
            if (s->st_value == 0u) continue;
            return obj_rt_addr(o, s->st_value);
        }
    }
    return 0u;
}

static int apply_rel_table(ld_obj_t *o, elf32_rel_t *rel, uint32_t count) {
    uint32_t i;
    if (!o || !rel || count == 0u) return 0;
    for (i = 0; i < count; i++) {
        uint32_t type = rel[i].r_info & 0xffu;
        uint32_t sym = rel[i].r_info >> 8;
        uint32_t *place = (uint32_t*)(uintptr_t)obj_rt_addr(o, rel[i].r_offset);
        uint32_t A = *place;
        uint32_t P = (uint32_t)(uintptr_t)place;
        uint32_t S = 0u;

        if (type == R_386_NONE) continue;

        if (type != R_386_RELATIVE && sym != 0u) {
            S = sym_addr_from_obj(o, sym);
            if (S == 0u) {
                const char *name = NULL;
                if (o->strtab && o->symtab && sym < o->symcnt) {
                    uint32_t noff = o->symtab[sym].st_name;
                    if (noff < o->strsz) name = o->strtab + noff;
                }
                S = resolve_global_symbol(name);
                if (S == 0u) {
                    fprintf(stderr, "ld-house.so: unresolved symbol in %s: %s\n", o->path, name ? name : "?");
                    return -1;
                }
            }
        }

        switch (type) {
            case R_386_32:
                *place = S + A;
                break;
            case R_386_PC32:
                *place = S + A - P;
                break;
            case R_386_GLOB_DAT:
            case R_386_JMP_SLOT:
                *place = S;
                break;
            case R_386_RELATIVE:
                *place = o->load_bias + A;
                break;
            default:
                fprintf(stderr, "ld-house.so: unsupported reloc type %u in %s\n", type, o->path);
                return -1;
        }
    }
    return 0;
}

static int load_all_needed(void) {
    uint32_t cursor = 0;
    while (cursor < g_obj_count) {
        ld_obj_t *o = &g_objs[cursor];
        uint32_t i;
        for (i = 0; i < o->needed_count; i++) {
            char path[128];
            const char *name;
            int slot;
            if (!o->strtab || o->needed[i] >= o->strsz) return -1;
            name = o->strtab + o->needed[i];

            if (!name[0]) continue;
            if (name[0] == '/') {
                strncpy(path, name, sizeof(path) - 1u);
                path[sizeof(path) - 1u] = '\0';
            } else {
                uint32_t n = 0;
                const char *prefix = "/lib/";
                while (prefix[n] && n + 1u < sizeof(path)) {
                    path[n] = prefix[n];
                    n++;
                }
                while (*name && n + 1u < sizeof(path)) {
                    path[n++] = *name++;
                }
                path[n] = '\0';
            }

            slot = load_shared_object(path, o->strtab + o->needed[i]);
            if (slot < 0) return -1;
        }
        cursor++;
    }
    return 0;
}

static int relocate_all(void) {
    uint32_t i;
    for (i = 0; i < g_obj_count; i++) {
        ld_obj_t *o = &g_objs[i];
        if (apply_rel_table(o, o->rel, o->rel_count) != 0) return -1;
    }
    for (i = 0; i < g_obj_count; i++) {
        ld_obj_t *o = &g_objs[i];
        if (apply_rel_table(o, o->jmprel, o->jmprel_count) != 0) return -1;
    }
    return 0;
}

__attribute__((noreturn)) static void jump_to_entry(uint32_t entry, uint32_t user_esp) {
    __asm__ __volatile__(
        "mov %0, %%esp\n\t"
        "xor %%ebp, %%ebp\n\t"
        "jmp *%1\n\t"
        :
        : "r"(user_esp), "r"(entry)
        : "memory"
    );
    __builtin_unreachable();
}

static int build_stack_from_argv(int argc, char **argv, uint32_t *esp_out) {
    uint32_t arg_ptr[LOADER_MAX_ARGS];
    uint32_t sp;
    int a;
    if (!esp_out || argc < 0 || argc > LOADER_MAX_ARGS) return -1;
    sp = USER_STACK_TOP & ~3u;

    for (a = argc - 1; a >= 0; a--) {
        uint32_t len;
        if (!argv[a]) return -1;
        len = (uint32_t)strlen(argv[a]) + 1u;
        if (sp < USER_VADDR_BASE + len + 256u) return -1;
        sp -= len;
        memcpy((void*)(uintptr_t)sp, argv[a], len);
        arg_ptr[a] = sp;
    }

    sp &= ~3u;
    sp -= 4u;
    *(uint32_t*)(uintptr_t)sp = 0u;
    for (a = argc - 1; a >= 0; a--) {
        sp -= 4u;
        *(uint32_t*)(uintptr_t)sp = arg_ptr[a];
    }
    sp -= 4u;
    *(uint32_t*)(uintptr_t)sp = (uint32_t)argc;

    *esp_out = sp;
    return 0;
}

int main(int argc, char **argv) {
    char *target_argv[LOADER_MAX_ARGS];
    int target_argc = 0;
    uint32_t user_esp = USER_STACK_TOP;
    int main_slot;

    if (argc >= 2 && (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0)) {
        fprintf(stderr, "ld-house.so: userspace ELF interpreter\n");
        fprintf(stderr, "usage: /lib/ld-house.so <program> [args...]\n");
        return 0;
    }
    if (argc < 2) {
        fprintf(stderr, "ld-house.so: missing target program\n");
        return 127;
    }

    g_obj_count = 0;
    g_next_lib_base = USER_LIB_START;

    main_slot = add_main_object(argv[1]);
    if (main_slot < 0) {
        fprintf(stderr, "ld-house.so: bad target ELF: %s\n", argv[1]);
        return 127;
    }

    if (load_all_needed() != 0) {
        fprintf(stderr, "ld-house.so: failed while loading dependencies\n");
        return 127;
    }

    if (relocate_all() != 0) {
        fprintf(stderr, "ld-house.so: relocation failed\n");
        return 127;
    }

    for (int i = 1; i < argc; i++) {
        if (target_argc >= LOADER_MAX_ARGS) {
            fprintf(stderr, "ld-house.so: too many args\n");
            return 127;
        }
        target_argv[target_argc++] = argv[i];
    }

    if (build_stack_from_argv(target_argc, target_argv, &user_esp) != 0) {
        fprintf(stderr, "ld-house.so: failed to build user stack\n");
        return 127;
    }

    jump_to_entry(g_objs[main_slot].entry, user_esp);
}
