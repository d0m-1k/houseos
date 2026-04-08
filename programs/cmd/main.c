#include "commands.h"

#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <syscall.h>

typedef int (*cmd_fn_t)(int argc, char **argv, int arg0, const char *cwd);

typedef struct {
    const char *name;
    cmd_fn_t fn;
} cmd_entry_t;

static const cmd_entry_t g_cmds[] = {
    { "echo", cmd_echo },
    { "printf", cmd_printf },
    { "hexdump", cmd_hexdump },
    { "pwd", cmd_pwd },
    { "ls", cmd_ls },
    { "cat", cmd_cat },
    { "grep", cmd_grep },
    { "less", cmd_less },
    { "mkdir", cmd_mkdir },
    { "mkfifo", cmd_mkfifo },
    { "mksock", cmd_mksock },
    { "touch", cmd_touch },
    { "rm", cmd_rm },
    { "rmdir", cmd_rmdir },
    { "cp", cmd_cp },
    { "mv", cmd_mv },
    { "ln", cmd_ln },
    { "tee", cmd_tee },
    { "chvt", cmd_chvt },
    { "ttyinfo", cmd_ttyinfo },
    { "kbdinfo", cmd_kbdinfo },
    { "mouseinfo", cmd_mouseinfo },
    { "reboot", cmd_reboot },
    { "poweroff", cmd_poweroff },
    { "mount", cmd_mount },
    { "umount", cmd_umount },
    { "lsblk", cmd_lsblk },
    { "udp", cmd_udp },
    { "bootloader", cmd_bootloader },
    { "vesa", cmd_vesa },
    { "vga", cmd_vga },
    { "clear", cmd_clear },
    { "img_view", cmd_img_view },
};

static const char *cmd_basename(const char *path) {
    const char *last = path;
    if (!path || !path[0]) return "cmd";
    while (*path) {
        if (*path == '/') last = path + 1;
        path++;
    }
    return (*last) ? last : "cmd";
}

static void print_cmd_info(void) {
    fprintf(stdout, "cmd multi-call binary\n");
    fprintf(stdout, "usage: cmd <applet> [args]\n");
    fprintf(stdout, "       cmd install [dir]\n");
    fprintf(stdout, "applets:");
    for (uint32_t i = 0; i < (uint32_t)(sizeof(g_cmds) / sizeof(g_cmds[0])); i++) {
        fprintf(stdout, "%s%s", (i == 0) ? " " : ", ", g_cmds[i].name);
    }
    fprintf(stdout, "\n");
}

static int cmd_resolve_self_path(const char *cwd, int argc, char **argv, int arg0, char *out, size_t cap) {
    int fd;
    int32_t n;
    if (!out || cap < 2u) return -1;
    fd = open("/proc/self/exe", 0);
    if (fd >= 0) {
        n = read(fd, out, (uint32_t)(cap - 1u));
        close(fd);
        if (n > 0) {
            while (n > 0 && (out[n - 1] == '\n' || out[n - 1] == '\r')) n--;
            out[n] = '\0';
            if (out[0] == '/') return 0;
        }
    }
    if (arg0 < argc && argv && argv[arg0] && argv[arg0][0] == '/') {
        strncpy(out, argv[arg0], cap - 1u);
        out[cap - 1u] = '\0';
        return 0;
    }
    if (arg0 < argc && argv && argv[arg0] && strchr(argv[arg0], '/')) {
        size_t cwd_len;
        size_t path_len;
        if (!cwd || cwd[0] != '/') return -1;
        cwd_len = strlen(cwd);
        path_len = strlen(argv[arg0]);
        if (cwd_len + path_len + 2u >= cap) return -1;
        if (strcmp(cwd, "/") == 0) {
            out[0] = '/';
            memcpy(out + 1, argv[arg0], path_len + 1u);
        } else {
            memcpy(out, cwd, cwd_len);
            out[cwd_len] = '/';
            memcpy(out + cwd_len + 1u, argv[arg0], path_len + 1u);
        }
        return 0;
    }
    return -1;
}

static int cmd_install_links(const char *self_path, const char *dir) {
    char newp[256];
    size_t dir_len;
    if (!self_path || self_path[0] != '/') return 1;
    if (!dir || dir[0] != '/') return 1;
    dir_len = strlen(dir);
    if (dir_len == 0) return 1;
    for (uint32_t i = 0; i < (uint32_t)(sizeof(g_cmds) / sizeof(g_cmds[0])); i++) {
        size_t base_len = strlen(dir);
        size_t pos;
        size_t name_len = strlen(g_cmds[i].name);
        if (base_len + strlen(g_cmds[i].name) + 2 >= sizeof(newp)) return 1;
        strncpy(newp, dir, sizeof(newp) - 1);
        newp[sizeof(newp) - 1] = '\0';
        if (base_len > 1 && newp[base_len - 1] == '/') newp[base_len - 1] = '\0';
        pos = strlen(newp);
        if (pos == 0) return 1;
        if (newp[pos - 1] != '/') {
            if (pos + 1 >= sizeof(newp)) return 1;
            newp[pos++] = '/';
            newp[pos] = '\0';
        }
        if (pos + name_len >= sizeof(newp)) return 1;
        memcpy(newp + pos, g_cmds[i].name, name_len + 1);
        (void)unlink(newp);
        if (link(self_path, newp) != 0) {
            fprintf(stderr, "install failed: %s\n", g_cmds[i].name);
            return 1;
        }
    }
    fprintf(stdout, "installed applets in %s\n", dir);
    return 0;
}

static int cmd_finish(int rc) {
    exit(rc);
    return rc;
}

int main(int argc, char **argv) {
    const char *cwd = "/";
    int arg0 = 0;
    const char *invoked;
    const char *command = NULL;
    char self_path[256];

    if (argc <= 0) {
        print_cmd_info();
        return cmd_finish(0);
    }
    if (argc >= 3 && strcmp(argv[0], "--pwd") == 0) {
        cwd = argv[1];
        arg0 = 2;
    }
    if (arg0 < argc) invoked = cmd_basename(argv[arg0]);
    else invoked = "cmd";
    if (strcmp(invoked, "cmd") != 0) {
        for (uint32_t i = 0; i < (uint32_t)(sizeof(g_cmds) / sizeof(g_cmds[0])); i++) {
            if (strcmp(invoked, g_cmds[i].name) == 0) {
                return cmd_finish(g_cmds[i].fn(argc, argv, arg0, cwd));
            }
        }
        fprintf(stderr, "unknown applet: %s\n", invoked);
        return cmd_finish(1);
    }

    if (arg0 >= argc) {
        print_cmd_info();
        return cmd_finish(0);
    }

    if (strcmp(argv[arg0], "cmd") == 0) {
        arg0++;
    }
    if (arg0 >= argc) {
        print_cmd_info();
        return cmd_finish(0);
    }
    command = argv[arg0];

    if (strcmp(command, "install") == 0) {
        const char *dir = (arg0 + 1 < argc) ? argv[arg0 + 1] : "/bin";
        if (cmd_resolve_self_path(cwd, argc, argv, arg0, self_path, sizeof(self_path)) != 0) {
            fprintf(stderr, "install failed: self path\n");
            return cmd_finish(1);
        }
        return cmd_finish(cmd_install_links(self_path, dir));
    }
    if (strcmp(command, "help") == 0 || strcmp(command, "--help") == 0) {
        print_cmd_info();
        return cmd_finish(0);
    }

    for (uint32_t i = 0; i < (uint32_t)(sizeof(g_cmds) / sizeof(g_cmds[0])); i++) {
        if (strcmp(command, g_cmds[i].name) == 0) {
            return cmd_finish(g_cmds[i].fn(argc, argv, arg0, cwd));
        }
    }

    fprintf(stderr, "unknown command: %s\n", command);
    return cmd_finish(1);
}
