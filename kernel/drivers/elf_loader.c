#include <drivers/elf_loader.h>
#include <asm/mm.h>
#include <string.h>

#define ELF_MAGIC 0x464C457F
#define ELFCLASS32 1
#define ELFDATA2LSB 1
#define ET_EXEC 2
#define EM_386 3
#define PT_LOAD 1

#define USER_ELF_MIN_VADDR 0x00400000u
#define USER_ELF_MAX_VADDR 0x04000000u

static int g_elf_last_error = 0;

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

static int range_ok(uint32_t vaddr, uint32_t size) {
    if (vaddr < USER_ELF_MIN_VADDR) return 0;
    if (size > USER_ELF_MAX_VADDR - USER_ELF_MIN_VADDR) return 0;
    if (vaddr > USER_ELF_MAX_VADDR - size) return 0;
    return 1;
}

int elf_load_from_memfs(memfs *fs, const char *path, uint32_t *entry_out) {
    g_elf_last_error = 0;
    if (!fs || !path || !entry_out) {
        g_elf_last_error = 1;
        return -1;
    }

    memfs_inode info;
    if (memfs_get_info(fs, path, &info) != 0) {
        g_elf_last_error = 2;
        return -1;
    }
    if (info.type != MEMFS_TYPE_FILE || info.file.size < sizeof(elf32_ehdr_t)) {
        g_elf_last_error = 3;
        return -1;
    }

    uint8_t *file = (uint8_t*)kmalloc(info.file.size);
    if (!file) {
        g_elf_last_error = 4;
        return -1;
    }

    if (memfs_read(fs, path, file, info.file.size) != (ssize_t)info.file.size) {
        g_elf_last_error = 5;
        kfree(file);
        return -1;
    }

    elf32_ehdr_t *eh = (elf32_ehdr_t*)file;
    if (*(uint32_t*)&eh->e_ident[0] != ELF_MAGIC ||
        eh->e_ident[4] != ELFCLASS32 ||
        eh->e_ident[5] != ELFDATA2LSB ||
        eh->e_type != ET_EXEC ||
        eh->e_machine != EM_386 ||
        eh->e_phentsize != sizeof(elf32_phdr_t)) {
        g_elf_last_error = 6;
        kfree(file);
        return -1;
    }

    if (eh->e_phoff + (uint32_t)eh->e_phnum * sizeof(elf32_phdr_t) > info.file.size) {
        g_elf_last_error = 7;
        kfree(file);
        return -1;
    }

    elf32_phdr_t *ph = (elf32_phdr_t*)(file + eh->e_phoff);
    for (uint16_t i = 0; i < eh->e_phnum; i++) {
        if (ph[i].p_type != PT_LOAD) continue;
        if (ph[i].p_memsz < ph[i].p_filesz) {
            g_elf_last_error = 8;
            kfree(file);
            return -1;
        }
        if (ph[i].p_offset + ph[i].p_filesz > info.file.size) {
            g_elf_last_error = 9;
            kfree(file);
            return -1;
        }
        if (!range_ok(ph[i].p_vaddr, ph[i].p_memsz)) {
            g_elf_last_error = 10;
            kfree(file);
            return -1;
        }

        memset((void*)(uintptr_t)ph[i].p_vaddr, 0, ph[i].p_memsz);
        memcpy((void*)(uintptr_t)ph[i].p_vaddr, file + ph[i].p_offset, ph[i].p_filesz);
    }

    if (!range_ok(eh->e_entry, 1)) {
        g_elf_last_error = 11;
        kfree(file);
        return -1;
    }

    *entry_out = eh->e_entry;
    kfree(file);
    return 0;
}

int elf_get_last_error(void) {
    return g_elf_last_error;
}
