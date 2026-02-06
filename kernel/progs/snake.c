#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <progs/snake.h>
#include <drivers/vga.h>
#include <drivers/keyboard.h>

#define MAP_X  0
#define MAP_Y  1
#define MAP_W  VGA_WIDTH
#define MAP_H  (VGA_HEIGHT-1)

#define INFO_Y 0

#define MAX_SNAKE (VGA_WIDTH * VGA_HEIGHT)

struct point {
    int x;
    int y;
};

enum dir {
    DIR_UP,
    DIR_DOWN,
    DIR_LEFT,
    DIR_RIGHT
};

static struct point snake[MAX_SNAKE];
static size_t snake_len;
static enum dir direction;
static struct point food;
static bool running;
static int score = 0;
static unsigned long base_delay = 10000000;

static void cmd_init(void) {
    register_command("snake", snake_run);
}

static void sleep_cycles(unsigned long cycles) {
    for (volatile unsigned long i = 0; i < cycles; i++);
}

static unsigned int seed = 123456789;

static void srand(unsigned int s) {
    seed = s;
}

static int rand(void) {
    seed ^= seed << 13;
    seed ^= seed >> 17;
    seed ^= seed << 5;
    return (int)(seed & 0x7FFFFFFF);
}

static void draw_border(void) {
    for (int x = 0; x < MAP_W; x++) {
        vga_cursor_set(x, MAP_Y);
        vga_put_char('#');
    }

    for (int x = 0; x < MAP_W; x++) {
        vga_cursor_set(x, MAP_Y + MAP_H - 1);
        vga_put_char('#');
    }

    for (int y = 1; y < MAP_H - 1; y++) {
        vga_cursor_set(0, MAP_Y + y);
        vga_put_char('#');

        vga_cursor_set(MAP_W - 1, MAP_Y + y);
        vga_put_char('#');
    }
}

static void clear_field(void) {
    for (int y = 1; y < MAP_H - 1; y++) {
        for (int x = 1; x < MAP_W - 1; x++) {
            vga_cursor_set(x, MAP_Y + y);
            vga_put_char(' ');
        }
    }
}

static void spawn_food(void) {
    food.x = 1 + (rand() % (MAP_W - 2));
    food.y = 1 + (rand() % (MAP_H - 2));
}

static void draw_food(void) {
    vga_cursor_set(food.x, food.y);
    vga_put_char('@');
}

static void draw_snake(void) {
    for (size_t i = 0; i < snake_len; i++) {
        vga_cursor_set(snake[i].x, snake[i].y);
        vga_put_char(i == 0 ? 'O' : 'o');
    }
}

static void draw_info(void) {
    vga_cursor_set(0, INFO_Y);
    vga_print("SNAKE | Score: ");

    char buf[16];
    itoa(score, buf, 10);
    vga_print(buf);

    vga_print(" | ESC = exit");
}

static bool hit_wall(struct point p) {
    return p.x <= 0 || p.x >= MAP_W - 1 || p.y <= 1 || p.y >= MAP_H - 1;
}

static bool hit_self(struct point p) {
    for (size_t i = 1; i < snake_len; i++) {
        if (snake[i].x == p.x && snake[i].y == p.y) return true;
    }
    return false;
}

static unsigned long current_delay(void) {
    if (direction == DIR_UP || direction == DIR_DOWN) return base_delay*3/2;
    return base_delay;
}

static void move_snake(void) {
    struct point next = snake[0];

    switch (direction) {
        case DIR_UP:    next.y--; break;
        case DIR_DOWN:  next.y++; break;
        case DIR_LEFT:  next.x--; break;
        case DIR_RIGHT: next.x++; break;
    }

    if (hit_wall(next) || hit_self(next)) {
        running = false;
        return;
    }

    for (size_t i = snake_len; i > 0; i--) {
        snake[i] = snake[i - 1];
    }

    snake[0] = next;

    if (next.x == food.x && next.y == food.y) {
        snake_len++;
        score += 10;
        spawn_food();
    } else {
        vga_cursor_set(snake[snake_len].x, snake[snake_len].y);
        vga_put_char(' ');
    }
}

static void handle_input(void) {
    if (!keyboard_event_available()) return;

    struct key_event ev = keyboard_get_event();
    if (!ev.pressed) return;

    switch (ev.scancode) {
        case KEY_UP:
            if (direction != DIR_DOWN)
                direction = DIR_UP;
            break;
        case KEY_DOWN:
            if (direction != DIR_UP)
                direction = DIR_DOWN;
            break;
        case KEY_LEFT:
            if (direction != DIR_RIGHT)
                direction = DIR_LEFT;
            break;
        case KEY_RIGHT:
            if (direction != DIR_LEFT)
                direction = DIR_RIGHT;
            break;
        case KEY_ESC:
            running = false;
            break;
    }
}

static void snake_init(void) {
    vga_clear();

    keyboard_set_echo_mode(false);
    vga_cursor_disable();

    draw_border();
    clear_field();

    snake_len = 3;
    snake[0] = (struct point){MAP_W/2, MAP_H/2};
    snake[1] = (struct point){MAP_W/2 - 1, MAP_H/2};
    snake[2] = (struct point){MAP_W/2 - 2, MAP_H/2};

    direction = DIR_RIGHT;
    spawn_food();
    running = true;
    score = 0;

    srand(123456);
}

static void parse_args(struct shell_args *a) {
    if (a->argc >= 2) {
        base_delay = (unsigned long)atoi(a->argv[1]);
    }

    if (a->argc >= 4) {
        int bg = atoi(a->argv[2]);
        int fg = atoi(a->argv[3]);
        vga_color_make(fg, bg);
    }
}

uint32_t snake_run(struct shell_args *a) {
    parse_args(a);
    snake_init();
    cmd_init();

    while (running) {
        handle_input();
        move_snake();

        draw_food();
        draw_snake();
        draw_info();
        
        vga_cursor_update();

        sleep_cycles(current_delay());
    }

    keyboard_set_echo_mode(true);
    vga_cursor_enable(14, 15);

    vga_cursor_set(MAP_W/2 - 5, MAP_H/2);
    vga_print("GAME OVER!");
    return 0;
    
    vga_cursor_set(0, VGA_HEIGHT-1);
}
