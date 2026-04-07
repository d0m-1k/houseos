#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <syscall.h>

#define USER_VADDR_BASE 0x00400000u
#define USER_VADDR_SIZE 0x00400000u
#define USER_STACK_TOP  (USER_VADDR_BASE + USER_VADDR_SIZE - 16u)
#define LOADER_MAX_ARGS 64

#define ELF_MAGIC 0x464C457Fu
#define ELFCLASS32 1u
#define ELFDATA2LSB 1u
#define ET_EXEC 2u
#define EM_386 3u

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

static uint32_t read_elf_entry(const char *path) {
    int fd;
    elf32_ehdr_t eh;
    int32_t n;
    fd = open(path, 0);
    if (fd < 0) return 0u;
    n = read(fd, &eh, sizeof(eh));
    close(fd);
    if (n != (int32_t)sizeof(eh)) return 0u;
    if (*(uint32_t*)&eh.e_ident[0] != ELF_MAGIC ||
        eh.e_ident[4] != ELFCLASS32 ||
        eh.e_ident[5] != ELFDATA2LSB ||
        eh.e_type != ET_EXEC ||
        eh.e_machine != EM_386) {
        return 0u;
    }
    if (eh.e_entry < USER_VADDR_BASE || eh.e_entry >= USER_VADDR_BASE + USER_VADDR_SIZE) return 0u;
    return eh.e_entry;
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
    uint32_t entry;
    uint32_t user_esp = USER_STACK_TOP;

    if (argc >= 2 && (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0)) {
        fprintf(stderr, "ld-house.so: userspace ELF interpreter\n");
        fprintf(stderr, "usage: /lib/ld-house.so <program> [args...]\n");
        return 0;
    }
    if (argc < 2) {
        fprintf(stderr, "ld-house.so: missing target program\n");
        return 127;
    }

    entry = read_elf_entry(argv[1]);
    if (entry == 0u) {
        fprintf(stderr, "ld-house.so: bad target ELF: %s\n", argv[1]);
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

    jump_to_entry(entry, user_esp);
}
