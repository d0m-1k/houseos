#include <progs/shell.h>
#include <progs/snake.h>
#include <drivers/keyboard.h>
#include <drivers/vga.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#define INPUT_BUFFER_SIZE 128

static char input_buf[INPUT_BUFFER_SIZE];
static size_t input_len = 0;
static size_t cursor_pos = 0;
static size_t input_start_x = 0;
static size_t input_start_y = 0;

static struct shell_args parse_args(char *cmd) {
    struct shell_args args;
    args.argc = 0;

    char *p = cmd;

    while (*p) {
        while (*p == ' ') *p++ = '\0';

        if (*p == '\0') break;

        if (args.argc < MAX_ARGS)
            args.argv[args.argc++] = p;

        while (*p && *p != ' ') p++;
    }

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
    vga_update();
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

    if (a.argc == 0)
        return;

    if (strcmp(a.argv[0], "clear") == 0) {
        vga_clear();
    } else if (strcmp(a.argv[0], "help") == 0) {
        vga_print("Commands: help, echo, clear, reboot, color, snake\n");
    } else if (strcmp(a.argv[0], "echo") == 0) {
        for (int i = 1; i < a.argc; i++) {
            vga_print(a.argv[i]);
            if (i+1 < a.argc) vga_put_char(' ');
        }
        vga_put_char('\n');
    } else if (strcmp(a.argv[0], "reboot") == 0) {
        vga_print("[stub] rebooting...\n");
    } else if (strcmp(a.argv[0], "color") == 0) {
        if (a.argc == 1) {
            uint8_t c = vga_color_get();
            char buf[16];
            vga_print("Current color: 0x");
            vga_print(utoa(c, buf, 16));
            vga_print("\n");
            return;
        }

        if (a.argc < 3) {
            vga_print("Usage: color <fg> <bg>\n");
            return;
        }

        int fg = atoi(a.argv[1]);
        int bg = atoi(a.argv[2]);

        if (fg < 0 || fg > 15 || bg < 0 || bg > 15) {
            vga_print("Error: colors must be 0-15\n");
            return;
        }

        uint8_t new_color = vga_color_make((enum vga_color)fg, (enum vga_color)bg);
        vga_color_set(new_color);
        vga_print("Color updated\n");
    } else if (strcmp(a.argv[0], "snake") == 0) {
        snake_run(a);
    } else {
        vga_print("Unknown command: ");
        vga_print(a.argv[0]);
        vga_print("\n");
    }
    vga_update();
}

void shell_run(void) {
    keyboard_set_echo_mode(false);
    keyboard_clear_buffers();

    vga_print("House OS Shell (type 'help')\n");

    shell_prompt();

    vga_update();

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
                vga_update();
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
