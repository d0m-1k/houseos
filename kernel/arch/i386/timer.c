#include <asm/timer.h>
#include <asm/idt.h>
#include <asm/task.h>
#include <asm/port.h>
#include <asm/processor.h>
#include <drivers/vesa.h>
#include <drivers/fonts/font_renderer.h>
#include <string.h>

static volatile uint32_t ticks = 0;

void timer_handler(void) {
    ticks++;

    for (int i = 0; i < task_count; i++) {
        task_t *task = &tasks[i];
        if (task->state == TASK_BLOCKED && task->wake_tick <= ticks) task->state = TASK_READY;
    }

    if (current_task != NULL) schedule();
}

void timer_init(void) {
    uint32_t divisor = 1193180 / 100; // 100 Гц
    outb(0x43, 0x36);
    outb(0x40, divisor & 0xFF);
    outb(0x40, (divisor >> 8) & 0xFF);
}

uint32_t timer_get_ticks(void) {
    return ticks;
}

void sleep(uint32_t ms) {
    if (ms == 0) return;

    uint32_t ticks_to_wait = (ms * 100) / 1000;
    if (ticks_to_wait == 0) ticks_to_wait = 1;

    uint32_t deadline = ticks + ticks_to_wait;

    cli();
    current_task->state = TASK_BLOCKED;
    current_task->wake_tick = deadline;
    schedule();
    sti();
}
