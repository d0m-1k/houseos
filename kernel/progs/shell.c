#include <progs/shell.h>
#include <progs/snake.h>
#include <drivers/keyboard.h>
#include <kernel/kernel.h>
#ifdef ENABLE_VGA
#include <drivers/vga.h>
#endif
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <asm/port.h>
#include <asm/mm.h>
#include <drivers/filesystem/memfs.h>

#ifndef ENABLE_VGA
#define vga_print(...) ((void)0)
#define vga_put_char(...) ((void)0)
#define vga_clear(...) ((void)0)
#define vga_backspace(...) ((void)0)
#define vga_cursor_set(...) ((void)0)
#define vga_cursor_get_x(...) ((size_t)0)
#define vga_cursor_get_y(...) ((size_t)0)
#define vga_cursor_update(...) ((void)0)
#define vga_color_get(...) ((uint8_t)0)
#define vga_color_set(...) ((void)0)
#define vga_color_make(...) ((uint8_t)0)
#define VGA_COLOR_BLACK 0
#define VGA_COLOR_WHITE 0
#endif

static memfs *fs = NULL;

#define INPUT_BUFFER_SIZE 128
#define MAX_PATH_LENGTH 256

static char input_buf[INPUT_BUFFER_SIZE];
static size_t input_len = 0;
static size_t cursor_pos = 0;
static size_t input_start_x = 0;
static size_t input_start_y = 0;

static char current_dir[MAX_PATH_LENGTH] = "/";

static void path_join(const char *base, const char *rel, char *result) {
    strcpy(result, base);
    
    if (base[strlen(base) - 1] != '/' && rel[0] != '/') strcat(result, "/");
    
    strcat(result, rel);
}

static void normalize_path(const char *input_path, char *output_path) {
    if (!input_path || !output_path) return;
    
    if (input_path[0] == '/') {
        strncpy(output_path, input_path, MAX_PATH_LENGTH - 1);
        output_path[MAX_PATH_LENGTH - 1] = '\0';
        return;
    }
    
    if (strcmp(current_dir, "/") == 0) {
        output_path[0] = '/';
        strncpy(output_path + 1, input_path, MAX_PATH_LENGTH - 2);
        output_path[MAX_PATH_LENGTH - 1] = '\0';
    } else {
        path_join(current_dir, input_path, output_path);
    }
    
    char *src = output_path;
    char *dst = output_path;
    int prev_was_slash = 0;
    
    while (*src) {
        if (*src == '/') {
            if (!prev_was_slash) {
                *dst = '/';
                dst++;
                prev_was_slash = 1;
            }
        } else {
            *dst = *src;
            dst++;
            prev_was_slash = 0;
        }
        src++;
    }
    *dst = '\0';
}

static int go_up_one_level(char *path) {
    char *last_slash = strrchr(path, '/');
    if (!last_slash) return -1;
    
    if (last_slash == path) {
        *(last_slash + 1) = '\0';
    } else {
        *last_slash = '\0';
    }
    return 0;
}

static void cmd_init(void);
void register_command(const char *name, uint32_t (*func)(struct shell_args *)) {
    if (commands_count < MAX_COMMANDS) {
        commands[commands_count] = (struct shell_command){name, func};
        commands_count++;
    }
}

static struct shell_args parse_args(char *cmd) {
    struct shell_args args;
    args.argc = 0;
    
    char *p = cmd;
    int in_single_quote = 0;
    int in_double_quote = 0;
    int arg_started = 0;
    
    while (*p) {
        if (!in_single_quote && !in_double_quote && *p == ' ') {
            if (arg_started) {
                *p = '\0';
                arg_started = 0;
                args.argc++;
            }
            p++;
            continue;
        }
        
        if (*p == '\'' && !in_double_quote) {
            if (in_single_quote) {
                in_single_quote = 0;
                *p = '\0';
            } else {
                in_single_quote = 1;
                if (!arg_started && args.argc < MAX_ARGS) {
                    args.argv[args.argc] = p + 1;
                    arg_started = 1;
                }
                *p = '\0';
            }
            p++;
            continue;
        }
        
        if (*p == '"' && !in_single_quote) {
            if (in_double_quote) {
                in_double_quote = 0;
                *p = '\0';
            } else {
                in_double_quote = 1;
                if (!arg_started && args.argc < MAX_ARGS) {
                    args.argv[args.argc] = p + 1;
                    arg_started = 1;
                }
                *p = '\0';
            }
            p++;
            continue;
        }
        
        if (!in_single_quote && !in_double_quote && !arg_started && args.argc < MAX_ARGS) {
            args.argv[args.argc] = p;
            arg_started = 1;
        }
        
        p++;
    }
    
    if (arg_started && args.argc < MAX_ARGS) args.argc++;
    
    if (args.argc < MAX_ARGS) args.argv[args.argc] = NULL;
    else args.argv[MAX_ARGS - 1] = NULL;
    
    return args;
}

static void shell_prompt(void) {
    vga_print("> ");

    input_start_x = vga_cursor_get_x();
    input_start_y = vga_cursor_get_y();

    input_len = 0;
    cursor_pos = 0;

    memset(input_buf, 0, sizeof(input_buf));
}

static void redraw_input_line(void) {
    vga_cursor_set(input_start_x, input_start_y);
    vga_print(input_buf);
    vga_put_char(' ');
    vga_cursor_set(input_start_x + cursor_pos, input_start_y);
}

static void insert_char(char c) {
    if (input_len >= INPUT_BUFFER_SIZE - 1) return;

    memmove(&input_buf[cursor_pos + 1], &input_buf[cursor_pos], input_len - cursor_pos);

    input_buf[cursor_pos] = c;
    input_len++;
    cursor_pos++;

    redraw_input_line();
}

static void backspace_char(void) {
    if (cursor_pos == 0) return;

    memmove(&input_buf[cursor_pos - 1], &input_buf[cursor_pos], input_len - cursor_pos);
    input_buf[cursor_pos - 1] = '\0';

    input_len--;
    cursor_pos--;
    vga_backspace();

    redraw_input_line();
}

static void shell_execute(char *cmd) {
    struct shell_args a = parse_args(cmd);
    if (a.argc == 0) return;

    bool found = false;

    for (int i = 0; i < commands_count; i++) {
        if (strcmp(commands[i].name, a.argv[0]) == 0) {
            found = true;
            commands[i].func(&a);
            break;
        }
    } if (!found) {
        for (int i = 0; i < commands_count; i++) {
            if (strcmp(commands[i].name, "command_not_found") == 0) {
                commands[i].func(&a);
                break;
            }
        }
    }
}

void shell_run(memfs *filesystem) {
    fs = filesystem;
    keyboard_set_echo_mode(false);
    keyboard_clear_buffers();
    cmd_init();

    vga_print("House OS Shell (type 'help')\n");

    shell_prompt();

    while (1) {
        if (!keyboard_event_available()) continue;

        struct key_event ev = keyboard_get_event();
        if (!ev.pressed) continue;

        switch (ev.scancode) {
            case KEY_ENTER:
                vga_print("\n");
                input_buf[input_len] = '\0';
                shell_execute(input_buf);
                shell_prompt();
                break;

            case KEY_BACKSPACE:
                backspace_char();
                break;

            case KEY_LEFT:
                if (cursor_pos > 0) {
                    cursor_pos--;
                    vga_cursor_set(input_start_x + cursor_pos, input_start_y);
                }
                break;

            case KEY_RIGHT:
                if (cursor_pos < input_len) {
                    cursor_pos++;
                    vga_cursor_set(input_start_x + cursor_pos, input_start_y);
                }
                break;

            default:
                if (ev.ascii) insert_char(ev.ascii);
                break;
        }
    }
}

static uint32_t cmd_pwd(struct shell_args *a) {
    (void)a;
    vga_print(current_dir);
    vga_print("\n");
    return 0;
}

static uint32_t cmd_cd(struct shell_args *a) {
    if (!fs) {
        vga_print("cd: filesystem not initialized\n");
        return 1;
    }
    
    if (a->argc < 2) {
        strcpy(current_dir, "/");
        return 0;
    }
    
    char abs_path[MAX_PATH_LENGTH];
    normalize_path(a->argv[1], abs_path);
    
    if (strcmp(a->argv[1], "..") == 0) {
        go_up_one_level(current_dir);
        return 0;
    }
    
    if (strcmp(a->argv[1], ".") == 0) {
        return 0;
    }
    
    memfs_inode *dir = lookup_path(fs, abs_path);
    if (!dir || dir->type != MEMFS_TYPE_DIR) {
        vga_print("cd: ");
        vga_print(a->argv[1]);
        vga_print(": No such directory\n");
        return 1;
    }
    
    strcpy(current_dir, abs_path);
    return 0;
}

static uint32_t cmd_command_not_found(struct shell_args *a) {
    vga_print("Command '");
    vga_print(a->argv[0]);
    vga_print("' not found\n");
    return 1;
}

static uint32_t cmd_help(struct shell_args *a) {
    vga_print("Commands: ");
    for (int i = 0; i < commands_count; i++) {
        vga_print(commands[i].name);
        vga_put_char(' ');
    }
    vga_put_char('\b\n');
    return 0;
}

static uint32_t cmd_echo(struct shell_args *a) {
    for (int i = 1; i < a->argc; i++) {
        vga_print(a->argv[i]);
        if (i+1 < a->argc) vga_put_char(' ');
    }
    vga_put_char('\n');
    return 0;
}

static uint32_t cmd_clear(struct shell_args *a) {
    vga_clear();
    return 0;
}

static uint32_t cmd_reboot(struct shell_args *a) {
    vga_print("[stub] rebooting...\n");
    outb(0x64, 0xFE);
    return 0;
}

static uint32_t cmd_color(struct shell_args *a) {
    if (a->argc == 1) {
        uint8_t c = vga_color_get();
        char buf[16];
        vga_print("Current color: 0x");
        vga_print(utoa(c, buf, 16));
        vga_print("\n");
        return 0;
    }
    if (a->argc < 3) {
        vga_print("Usage: color <fg> <bg>\n");
        return 1;
    }
    int fg = atoi(a->argv[1]);
    int bg = atoi(a->argv[2]);
    if (fg < 0 || fg > 15 || bg < 0 || bg > 15) {
        vga_print("Error: colors must be 0-15\n");
        return 1;
    }
    uint8_t new_color = vga_color_make((enum vga_color)fg, (enum vga_color)bg);
    vga_color_set(new_color);
    vga_print("Color updated\n");
    return 0;
}

static uint32_t cmd_ls(struct shell_args *a) {
    if (!fs) {
        vga_print("ls: filesystem not initialized\n");
        return 1;
    }
    
    char abs_path[MAX_PATH_LENGTH];
    if (a->argc >= 2) {
        normalize_path(a->argv[1], abs_path);
    } else {
        strcpy(abs_path, current_dir);
    }

    char *listing = memfs_ls(fs, abs_path);
    if (!listing) {
        vga_print("ls: cannot access '");
        vga_print(abs_path);
        vga_print("'\n");
        return 1;
    }

    vga_print(listing);
    vga_put_char('\n');
    vfree(listing);
    return 0;
}

static uint32_t cmd_mkdir(struct shell_args *a) {
    if (!fs) {
        vga_print("mkdir: filesystem not initialized\n");
        return 1;
    }
    if (a->argc < 2) {
        vga_print("mkdir: missing operand\n");
        return 1;
    }

    char abs_path[MAX_PATH_LENGTH];
    normalize_path(a->argv[1], abs_path);
    
    if (!memfs_create_dir(fs, abs_path)) {
        vga_print("mkdir: failed to create directory\n");
        return 1;
    }
    return 0;
}

static uint32_t cmd_touch(struct shell_args *a) {
    if (!fs) {
        vga_print("touch: filesystem not initialized\n");
        return 1;
    }
    if (a->argc < 2) {
        vga_print("touch: missing file operand\n");
        return 1;
    }

    char abs_path[MAX_PATH_LENGTH];
    normalize_path(a->argv[1], abs_path);
    
    if (!memfs_create_file(fs, abs_path)) {
        vga_print("touch: failed to create file\n");
        return 1;
    }
    return 0;
}

static uint32_t cmd_readdir(struct shell_args *a) {
    if (!fs) {
        vga_print("readdir: filesystem not initialized\n");
        return 1;
    }
    
    char abs_path[MAX_PATH_LENGTH];
    if (a->argc >= 2) {
        normalize_path(a->argv[1], abs_path);
    } else {
        strcpy(abs_path, current_dir);
    }

    char *names[64];
    size_t count = memfs_readdir(fs, abs_path, names, 64);

    if (count == 0) {
        vga_print("readdir: empty or invalid directory\n");
        return 1;
    }

    for (size_t i = 0; i < count; i++) {
        vga_print(names[i]);
        vga_put_char('\n');
    }
    return 0;
}

static uint32_t cmd_cat(struct shell_args *a) {
    if (!fs) {
        vga_print("cat: filesystem not initialized\n");
        return 1;
    }
    if (a->argc < 2) {
        vga_print("cat: missing file operand\n");
        return 1;
    }

    char abs_path[MAX_PATH_LENGTH];
    normalize_path(a->argv[1], abs_path);
    
    char buf[512];
    ssize_t n = memfs_read(fs, abs_path, buf, sizeof(buf)-1);

    if (n < 0) {
        vga_print("cat: cannot read file '");
        vga_print(abs_path);
        vga_print("'\n");
        return 1;
    }

    buf[n] = '\0';
    vga_print(buf);
    vga_put_char('\n');
    return 0;
}

static uint32_t cmd_write(struct shell_args *a) {
    if (!fs) {
        vga_print("write: filesystem not initialized\n");
        return 1;
    }
    if (a->argc < 3) {
        vga_print("usage: write <path> <text>\n");
        return 1;
    }

    char abs_path[MAX_PATH_LENGTH];
    normalize_path(a->argv[1], abs_path);
    
    const char *text = a->argv[2];

    if (memfs_write(fs, abs_path, text, strlen(text)) < 0) {
        vga_print("write: failed to write to '");
        vga_print(abs_path);
        vga_print("'\n");
        return 1;
    }
    return 0;
}

static uint32_t cmd_append(struct shell_args *a) {
    if (!fs) {
        vga_print("append: filesystem not initialized\n");
        return 1;
    }
    if (a->argc < 3) {
        vga_print("usage: append <path> <text>\n");
        return 1;
    }

    char abs_path[MAX_PATH_LENGTH];
    normalize_path(a->argv[1], abs_path);
    
    if (memfs_append(fs, abs_path, a->argv[2], strlen(a->argv[2])) < 0) {
        vga_print("append: failed to append to '");
        vga_print(abs_path);
        vga_print("'\n");
        return 1;
    }
    return 0;
}

static uint32_t cmd_rm(struct shell_args *a) {
    if (!fs) {
        vga_print("rm: filesystem not initialized\n");
        return 1;
    }
    if (a->argc < 2) {
        vga_print("rm: missing operand\n");
        return 1;
    }

    char abs_path[MAX_PATH_LENGTH];
    normalize_path(a->argv[1], abs_path);
    
    if (memfs_delete_file(fs, abs_path) != 0) {
        vga_print("rm: cannot remove '");
        vga_print(abs_path);
        vga_print("'\n");
        return 1;
    }
    return 0;
}

static uint32_t cmd_rmdir(struct shell_args *a) {
    if (!fs) {
        vga_print("rmdir: filesystem not initialized\n");
        return 1;
    }
    if (a->argc < 2) {
        vga_print("rmdir: missing operand\n");
        return 1;
    }

    char abs_path[MAX_PATH_LENGTH];
    normalize_path(a->argv[1], abs_path);
    
    if (memfs_delete_dir(fs, abs_path) != 0) {
        vga_print("rmdir: failed to remove '");
        vga_print(abs_path);
        vga_print("' (not empty or invalid)\n");
        return 1;
    }
    return 0;
}

static uint32_t cmd_stat(struct shell_args *a) {
    if (!fs) {
        vga_print("stat: filesystem not initialized\n");
        return 1;
    }
    if (a->argc < 2) {
        vga_print("stat: missing operand\n");
        return 1;
    }

    char abs_path[MAX_PATH_LENGTH];
    normalize_path(a->argv[1], abs_path);
    
    memfs_inode info;
    if (memfs_get_info(fs, abs_path, &info) != 0) {
        vga_print("stat: '");
        vga_print(abs_path);
        vga_print("' not found\n");
        return 1;
    }

    vga_print("Path: ");
    vga_print(abs_path);
    vga_print("\n");
    
    vga_print("Name: ");
    vga_print(info.name);
    vga_print("\n");

    vga_print("Type: ");
    if (info.type == MEMFS_TYPE_FILE)
        vga_print("file\n");
    else if (info.type == MEMFS_TYPE_DIR)
        vga_print("dir\n");
    else
        vga_print("other\n");

    if (info.type == MEMFS_TYPE_FILE) {
        vga_print("Size: ");
        char buf[32];
        itoa(info.file.size, buf, 10);
        vga_print(buf);
        vga_print(" bytes\n");
    }

    vga_print("Links: ");
    char buf[32];
    itoa(info.link_count, buf, 10);
    vga_print(buf);
    vga_print("\n");
    
    return 0;
}

static uint32_t cmd_fsinfo(struct shell_args *a) {
    (void)a;
    
    if (!fs) {
        vga_print("Filesystem not initialized\n");
        return 1;
    }
    
    vga_print("MemFS Information:\n");
    vga_print("  Current directory: ");
    vga_print(current_dir);
    vga_print("\n");
    vga_print("  Inode count: ");
    
    char buf[32];
    itoa(fs->inode_count, buf, 10);
    vga_print(buf);
    vga_print("\n");
    
    vga_print("  Used memory: ");
    itoa(fs->used_memory, buf, 10);
    vga_print(buf);
    vga_print(" bytes\n");
    
    return 0;
}

static uint32_t cmd_hexdump(struct shell_args *a) {
    if (a->argc != 3) {
        vga_print("Usage: hexdump <address> <size>\n");
        vga_print("Note: arguments are decimal numbers\n");
        return 1;
    }
    
    uint32_t address = atoi(a->argv[1]);
    uint32_t size = atoi(a->argv[2]);
    
    if (size == 0) {
        vga_print("hexdump: size must be greater than 0\n");
        return 1;
    }
    
    const unsigned char *p = (const unsigned char *)address;
    const char *hex = "0123456789ABCDEF";
    
    for (uint32_t offset = 0; offset < size; offset += 16) {
        for (int i = 7; i >= 0; i--) vga_put_char(hex[((address + offset) >> (i * 4)) & 0xF]);
        vga_print(": ");
        
        for (int i = 0; i < 16; i++) {
            if (offset + i < size) {
                vga_put_char(hex[(p[offset + i] >> 4) & 0xF]);
                vga_put_char(hex[p[offset + i] & 0xF]);
            } else vga_print("  ");
            
            vga_put_char(' ');
            if (i == 7) vga_put_char(' ');
        }
        
        vga_print("| ");
        
        for (int i = 0; i < 16; i++) {
            if (offset + i < size) {
                unsigned char c = p[offset + i];
                vga_put_char((c >= 32 && c <= 126) ? c : '.');
            } else vga_put_char(' ');
        }
        
        vga_print(" |\n");
    }
    
    return 0;
}
static void cmd_init(void) {
    register_command("command_not_found", cmd_command_not_found);
    register_command("help", cmd_help);
    register_command("echo", cmd_echo);
    register_command("clear", cmd_clear);
    register_command("reboot", cmd_reboot);
    register_command("color", cmd_color);
    
    register_command("pwd", cmd_pwd);
    register_command("cd", cmd_cd);
    register_command("fsinfo", cmd_fsinfo);

    register_command("snake", snake_run);

    register_command("ls", cmd_ls);
    register_command("mkdir", cmd_mkdir);
    register_command("touch", cmd_touch);
    register_command("readdir", cmd_readdir);
    register_command("cat", cmd_cat);
    register_command("write", cmd_write);
    register_command("append", cmd_append);
    register_command("rm", cmd_rm);
    register_command("rmdir", cmd_rmdir);
    register_command("stat", cmd_stat);

    register_command("hexdump", cmd_hexdump);
}
